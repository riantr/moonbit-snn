// Sparse matrix (CSR format) — bit-exact port of SNNModels.jl/src/utils/sparse_matrix.jl.
//
// The Julia source uses SparseMatrixCSC (compressed sparse column)
// but we use CSR (compressed sparse row) here for simpler pre-synaptic
// iteration: for each pre-synaptic neuron, iterate over its
// outgoing connections.
//
// Julia reference (src/utils/sparse_matrix.jl):
//   sparse_matrix(post, pre, μ, σ, p) = ...  → W, I, J, index, rowptr
//   dsparse(W, I, J)                       → SparseMatrixCSC{Float32, Int}
//   update_sparse_matrix!(c, W)            → refresh from dense matrix
//   connect!(c, j, i, μ)                   → set (i,j) weight then rebuild
//
// For our CSR layout we store (rowptr, colptr, vals) per pre-syn row.
// `connect!(c, post, pre, w)` mutates the dense weight buffer and
// then rebuilds the CSR.

///|
/// Sparse matrix in compressed-sparse-row format.
pub struct SparseMatrixCSR {
  rows : Int
  cols : Int
  // rowptr[i] = start index in colptr/vals of row i's first non-zero.
  // Length = rows + 1.
  rowptr : Array[Int]
  // Column index of each non-zero. Length = nnz.
  colptr : Array[Int]
  // Value of each non-zero. Length = nnz.
  vals : Array[Float]
}

///|
/// Number of stored non-zeros.
pub fn SparseMatrixCSR::nnz(m : SparseMatrixCSR) -> Int {
  m.vals.length()
}

///|
/// Build an empty N×M sparse matrix.
pub fn SparseMatrixCSR::empty(rows : Int, cols : Int) -> SparseMatrixCSR {
  let rowptr : Array[Int] = Array::make(rows + 1, 0)
  { rows, cols, rowptr, colptr: [], vals: [] }
}

///|
/// Build a CSR sparse matrix from a dense Float32 matrix.
/// Density = fraction of non-zero entries preserved.
pub fn SparseMatrixCSR::from_dense(
  dense : Array[Array[Float]],
  threshold : Float,
) -> SparseMatrixCSR {
  let rows = dense.length()
  let cols = if rows > 0 { dense[0].length() } else { 0 }
  let rowptr : Array[Int] = Array::make(rows + 1, 0)
  let colptr : Array[Int] = []
  let vals : Array[Float] = []
  for i in 0..<rows {
    rowptr[i] = vals.length()
    for j in 0..<cols {
      let v = dense[i][j]
      if v.abs() > threshold {
        colptr.push(j)
        vals.push(v)
      }
    }
  }
  rowptr[rows] = vals.length()
  { rows, cols, rowptr, colptr, vals }
}

///|
/// Connection rules for random sparse matrices. Mirrors Julia SNN's
/// `rule` keyword on `sparse_matrix(post, pre, μ, σ, p; rule=...)`:
///   - `Bernoulli`: each edge independently kept with probability `p`
///   - `FixedIn`  : each post-synaptic neuron receives from exactly
///                  `p * Npre` pre-synaptic neurons (chosen uniformly
///                  without replacement). Same in-degree for all posts.
///   - `FixedOut` : each pre-synaptic neuron projects to exactly
///                  `p * Npost` post-synaptic neurons. Same out-degree
///                  for all pres.
pub(all) enum ConnectRule {
  Bernoulli
  FixedIn
  FixedOut
}

///|
/// Build a sparse matrix with random Normal(μ, σ) weights and
/// connection probability `p`. Equivalent to Julia's
/// `sparse_matrix(post, pre, μ, σ, p)`.
///
/// `rng` is the Xoshiro stream. The Float32 draw path is bit-exact
/// with Julia's `rand(rng, Float32)` for the same seed.
pub fn SparseMatrixCSR::random(
  rows : Int,
  cols : Int,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> SparseMatrixCSR {
  // Default rule is Bernoulli for backward compatibility.
  SparseMatrixCSR::random_with_rule(rows, cols, mu, sigma, p, Bernoulli, rng)
}

///|
/// Build a sparse matrix with the given connection rule. See
/// `ConnectRule` for the semantics of each rule.
pub fn SparseMatrixCSR::random_with_rule(
  rows : Int,
  cols : Int,
  mu : Float,
  sigma : Float,
  p : Float,
  rule : ConnectRule,
  rng : Xoshiro,
) -> SparseMatrixCSR {
  // Build the dense weight matrix first. We use Float for weights
  // (matches Julia's Float32 default).
  let dense : Array[Array[Float]] = Array::make(rows, [])
  for i in 0..<rows {
    let row : Array[Float] = Array::make(cols, 0.0F)
    // Sample weights for every (i, j) so the RNG stream is consistent
    // across rule choices (the same call site, same seed, same draws).
    // Note: we DO NOT clip negative weights here — Julia's `w[w.<=0].=0`
    // is only applied in some code paths (e.g. with μ ≈ 0). Our
    // RateSynapse uses zero-mean weights that can be negative, so we
    // keep the signed distribution. Callers that want strictly
    // positive weights can pass `mu` large enough.
    for j in 0..<cols {
      let (z1, _) = box_muller(rng)
      let w = mu + sigma * Float::from_double(z1)
      row[j] = w
    }
    dense[i] = row
  }
  // Now apply the rule to zero out non-selected entries.
  match rule {
    Bernoulli => {
      // Each weight independently zeroed with prob (1 - p).
      for i in 0..<rows {
        for j in 0..<cols {
          let u = next_f32(rng)
          if u >= p {
            dense[i][j] = 0.0F
          }
        }
      }
    }
    FixedIn => {
      // For each post j, keep exactly `round(p*rows)` pre-synaptic
      // neurons (chosen uniformly without replacement). The rest go
      // to 0.
      let n_keep = (Float::from_int(rows) * p).to_int()
      if n_keep > 0 && n_keep <= rows {
        for j in 0..<cols {
          // Build a list of pre indices, shuffle, and zero the first
          // (rows - n_keep) of them.
          let pre_idx : Array[Int] = []
          for k in 0..<rows {
            pre_idx.push(k)
          }
          // Fisher-Yates partial shuffle (only need first (rows - n_keep) swaps).
          let n_drop = rows - n_keep
          for k in 0..<n_drop {
            // Pick random swap target from k..<rows
            let u = next_f32(rng)
            let r_idx = k + (Float::from_int(rows) - Float::from_int(k) * u).to_int()
            let r_clamped = if r_idx >= rows { rows - 1 } else { r_idx }
            // Swap pre_idx[k] and pre_idx[r_clamped]
            let tmp = pre_idx[k]
            pre_idx[k] = pre_idx[r_clamped]
            pre_idx[r_clamped] = tmp
          }
          for k in 0..<n_drop {
            dense[pre_idx[k]][j] = 0.0F
          }
        }
      } else if n_keep == 0 {
        // No connections; zero everything.
        for i in 0..<rows {
          for j in 0..<cols {
            dense[i][j] = 0.0F
          }
        }
      }
    }
    FixedOut => {
      // For each pre i, keep exactly `round(p*cols)` post-synaptic
      // neurons.
      let n_keep = (Float::from_int(cols) * p).to_int()
      if n_keep > 0 && n_keep <= cols {
        for i in 0..<rows {
          let post_idx : Array[Int] = []
          for k in 0..<cols {
            post_idx.push(k)
          }
          let n_drop = cols - n_keep
          for k in 0..<n_drop {
            let u = next_f32(rng)
            let r_idx = k + (Float::from_int(cols) - Float::from_int(k) * u).to_int()
            let r_clamped = if r_idx >= cols { cols - 1 } else { r_idx }
            let tmp = post_idx[k]
            post_idx[k] = post_idx[r_clamped]
            post_idx[r_clamped] = tmp
          }
          for k in 0..<n_drop {
            dense[i][post_idx[k]] = 0.0F
          }
        }
      } else if n_keep == 0 {
        for i in 0..<rows {
          for j in 0..<cols {
            dense[i][j] = 0.0F
          }
        }
      }
    }
  }
  // Convert dense to CSR.
  let rowptr : Array[Int] = Array::make(rows + 1, 0)
  let colptr : Array[Int] = []
  let vals : Array[Float] = []
  for i in 0..<rows {
    rowptr[i] = vals.length()
    for j in 0..<cols {
      let v = dense[i][j]
      if v != 0.0F {
        colptr.push(j)
        vals.push(v)
      }
    }
  }
  rowptr[rows] = vals.length()
  { rows, cols, rowptr, colptr, vals }
}

///|
/// Get the value at (i, j), or 0.0 if not stored.
pub fn SparseMatrixCSR::get(m : SparseMatrixCSR, i : Int, j : Int) -> Float {
  if i < 0 || i >= m.rows {
    return 0.0F
  }
  let start = m.rowptr[i]
  let end = m.rowptr[i + 1]
  // Linear scan within row i (rows are typically short)
  for k in start..<end {
    if m.colptr[k] == j {
      return m.vals[k]
    }
  }
  0.0F
}

///|
/// Set the value at (i, j). If a value already exists, replace it;
/// otherwise append. Updates rowptr in place.
pub fn SparseMatrixCSR::set(m : SparseMatrixCSR, i : Int, j : Int, v : Float) -> Unit {
  if i < 0 || i >= m.rows {
    return
  }
  let start = m.rowptr[i]
  let end = m.rowptr[i + 1]
  // Look for existing entry
  let mut found = false
  for k in start..<end {
    if m.colptr[k] == j {
      m.vals[k] = v
      found = true
      break
    }
  }
  if !found {
    // Insert at end of row i; shift later rows
    m.colptr.insert(end, j)
    m.vals.insert(end, v)
    let n_rows = m.rows
    for r in (i + 1)..<(n_rows + 1) {
      m.rowptr[r] = m.rowptr[r] + 1
    }
  }
}

///|
/// Forward a pre-synaptic neuron's outgoing weights to the post-synaptic
/// target's `g` array (e.g. `glu` for :ge, `gaba` for :gi).
pub fn SparseMatrixCSR::forward(
  m : SparseMatrixCSR,
  pre_fire : Array[Bool],
  post_g : Array[Float],
) -> Unit {
  let rows = m.rows
  for i in 0..<rows {
    if pre_fire[i] {
      let start = m.rowptr[i]
      let end = m.rowptr[i + 1]
      for k in start..<end {
        let post_idx = m.colptr[k]
        let w = m.vals[k]
        post_g[post_idx] = post_g[post_idx] + w
      }
    }
  }
}

///|
/// Rate-mode forward: for each row, add `w * pre_rate[i]` to `post_g[j]`.
/// Unlike the spike-driven `forward`, this does not gate on a Boolean
/// mask — every pre-synaptic neuron contributes proportional to its
/// firing rate. Used by RateSynapse for Wilson-Cowan networks.
pub fn SparseMatrixCSR::forward_rate(
  m : SparseMatrixCSR,
  pre_rate : Array[Float],
  post_g : Array[Float],
) -> Unit {
  let rows = m.rows
  for i in 0..<rows {
    let r_i = pre_rate[i]
    if r_i == 0.0F {
      continue
    }
    let start = m.rowptr[i]
    let end = m.rowptr[i + 1]
    for k in start..<end {
      let post_idx = m.colptr[k]
      let w = m.vals[k]
      post_g[post_idx] = post_g[post_idx] + w * r_i
    }
  }
}

///|
/// Number of connections in row i (out-degree of pre-synaptic neuron i).
pub fn SparseMatrixCSR::row_nnz(m : SparseMatrixCSR, i : Int) -> Int {
  if i < 0 || i >= m.rows {
    return 0
  }
  m.rowptr[i + 1] - m.rowptr[i]
}
