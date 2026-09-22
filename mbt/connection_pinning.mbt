// connection_pinning.mbt — port of SNNModels.jl/src/connections/{pinning_synapse,pinning_sparse_synapse}.jl.
//
// PINning (Pyramidal-INterneuron) synaptic plasticity from
// https://www.ncbi.nlm.nih.gov/pubmed/26971945 — a rate-based
// homeostatic rule that drives the post-synaptic rate toward a target
// rate `f[i]` while keeping the weight matrix P (the inverse of the
// pre/post rate covariance) stable.
//
// Forward (rate-based, like RateSynapse):
//   q[i] += P[s] * rJ[j]
//   g[i] += W[s] * rJ[j]
//
// Plasticity (one call per "effective" interval; weight updates use
// only the rate trace, no spike timing):
//   C = 1 / (1 + dot(q, rI))
//   for each connection s in rowptr[j]:colptr[j+1]:
//     i = I[s]
//     P[s] += -C * q[i] * q[j]
//     W[s] += C * (f[i] - g[i]) * q[j]
//
// We follow the sparse variant (PINningSparseSynapse) since the dense
// PINningSynapse uses BLAS ger! which is out of scope; the update
// formulas are identical, just iterated manually instead of via BLAS.

///|
/// PINningSparseSynapse — port of
/// refs/SNNModels.jl/src/connections/pinning_sparse_synapse.jl.
/// Rate-mode synaptic plasticity rule for rate-model populations
/// (WilsonCowan currently; any population with an `r` Array[Float]
/// field works since arrays are passed by reference).
///
/// Fields:
///   pre / post : WilsonCowan   (or any rate population with `r`)
///   matrix : SparseMatrixCSR   (CSR of the W weight matrix)
///   rI, rJ : aliases for post.r / pre.r (set at construction;
///       MoonBit arrays are reference types so these track rate updates)
///   g : Array[Float]           (post-synaptic conductance buffer;
///       aliased to post.g for direct write)
///   P : Array[Float]           (same length as W; the inverse
///       <r_i * r_j>^{-1} matrix)
///   q : Array[Float]           (length post.n; P * rJ projection)
///   f : Array[Float]           (length post.n; post-synaptic target rate)
pub(all) struct PINningSparseSynapse {
  pre : WilsonCowan
  post : WilsonCowan
  matrix : SparseMatrixCSR
  // P matrix is stored as a parallel Array[Float] to matrix.vals
  // (same length, indexed by the same rowptr/colptr).
  p_vals : Array[Float]
  rI : Array[Float]
  rJ : Array[Float]
  g : Array[Float]
  q : Array[Float]
  f : Array[Float]
}

///|
/// Build a PINningSparseSynapse with random Normal weights and
/// identity inverse-covariance matrix P = α * I (initial guess for
/// P is uncorrelated firing).
///
/// Args:
///   pre / post : WilsonCowan populations
///   mu : Float          — weight scale (Julia uses `μ / sqrt(p * pre.N) * sprandn(...)`)
///   p : Float           — connection probability
///   alpha : Float       — initial P diagonal scale
///   rng : Xoshiro
pub fn PINningSparseSynapse::new(
  pre : WilsonCowan,
  post : WilsonCowan,
  mu : Float,
  p : Float,
  alpha : Float,
  rng : Xoshiro,
) -> PINningSparseSynapse {
  let n_pre = Float::from_int(pre.n)
  let denom = (n_pre * p).sqrt()
  let weight_std = if denom > 0.0F { mu / denom } else { mu }
  let matrix = SparseMatrixCSR::random(
    pre.n, post.n, 0.0F, weight_std, p, rng,
  )
  // P[i] is initialised as α at positions where colptr[s] == rowptr[s]
  // (i.e. self-connection only). For non-self edges, P[s] = 0.
  // Julia: `P = α * (I .== J)` — for each connection s, check if
  // colptr[s] == rowptr-pre-start.
  let n_conn = matrix.vals.length()
  let p_vals : Array[Float] = Array::make(n_conn, 0.0F)
  let mut j = 0
  while j < pre.n {
    let start = matrix.rowptr[j]
    let end_ = matrix.rowptr[j + 1]
    let mut s = start
    while s < end_ {
      if matrix.colptr[s] == j {
        p_vals[s] = alpha
      }
      s = s + 1
    }
    j = j + 1
  }
  // Allocate q and f (length post.n).
  let q : Array[Float] = Array::make(post.n, 0.0F)
  let f : Array[Float] = Array::make(post.n, 0.0F)
  // Alias rI / rJ to pre.r / post.r (MoonBit arrays are reference types).
  let rI = post.r
  let rJ = pre.r
  // g aliased to post.g for the synaptic conductance forward path.
  let g = post.g
  { pre, post, matrix, p_vals, rI, rJ, g, q, f }
}

///|
/// Set the target rate `f[i]` for post-neuron i. After learning, the
/// PINning rule drives `g[i]` toward `f[i]`.
pub fn PINningSparseSynapse::set_target(
  c : PINningSparseSynapse,
  i : Int,
  target : Float,
) -> Unit {
  c.f[i] = target
}

///|
/// Forward: for each pre j, walk rowptr[j]..rowptr[j+1] and update
///   q[colptr[s]] += P[s] * rJ[j]
///   g[colptr[s]] += W[s] * rJ[j]
/// This is the rate-mode analogue of `forward_rate_synapse`. Resets
/// q and g to zero first (matches Julia's `fill!(q, zero)` and
/// `fill!(g, zero)` then update).
pub fn forward_pinning_synapse(c : PINningSparseSynapse) -> Unit {
  // Reset q and g to zero.
  let mut i = 0
  while i < c.q.length() {
    c.q[i] = 0.0F
    c.g[i] = 0.0F
    i = i + 1
  }
  let n_pre = c.pre.n
  let mut j = 0
  while j < n_pre {
    let rJj = c.rJ[j]
    let start = c.matrix.rowptr[j]
    let end_ = c.matrix.rowptr[j + 1]
    let mut s = start
    while s < end_ {
      let post_idx = c.matrix.colptr[s]
      c.q[post_idx] = c.q[post_idx] + c.p_vals[s] * rJj
      c.g[post_idx] = c.g[post_idx] + c.matrix.vals[s] * rJj
      s = s + 1
    }
    j = j + 1
  }
}

///|
/// Plasticity (one effective update step). Mirrors Julia's
/// `plasticity!` for PINningSparseSynapse:
///   C = 1 / (1 + dot(q, rI))
///   for s in rowptr[j]:rowptr[j+1]:
///     i = I[s]
///     P[s] += -C * q[i] * q[j]
///     W[s] += C * (f[i] - g[i]) * q[j]
///
/// Args:
///   c : PINningSparseSynapse
///   dt : Float (kept for API parity with `plasticity!(c, param, dt, T)`;
///       the rate-mode rule doesn't actually use dt since it's
///       event-driven on `forward_pinning_synapse` calls)
///   T : Float (current time, kept for parity; not used in the
///       pure rate-mode update)
pub fn pinning_plasticity(
  c : PINningSparseSynapse,
  dt : Float,
  t_now : Float,
) -> Unit {
  let _ = dt
  let _ = t_now
  // C = 1 / (1 + dot(q, rI))
  let mut dot_q_rI : Float = 0.0F
  let mut k = 0
  while k < c.q.length() {
    dot_q_rI = dot_q_rI + c.q[k] * c.rI[k]
    k = k + 1
  }
  let c_scale : Float = 1.0F / (1.0F + dot_q_rI)
  let n_pre = c.pre.n
  let mut j = 0
  while j < n_pre {
    let q_j = c.q[j]
    let start = c.matrix.rowptr[j]
    let end_ = c.matrix.rowptr[j + 1]
    let mut s = start
    while s < end_ {
      let i = c.matrix.colptr[s]
      c.p_vals[s] = c.p_vals[s] + -c_scale * c.q[i] * q_j
      c.matrix.vals[s] = c.matrix.vals[s] + c_scale * (c.f[i] - c.g[i]) * q_j
      s = s + 1
    }
    j = j + 1
  }
}