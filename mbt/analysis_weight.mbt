// analysis_weight.mbt — average synaptic weight between two neuron sets.
//
// Port of `refs/SNNUtils.jl/src/analysis/weights.jl`:
//   - `average_weight(pre, post, synapse)` — mean of all synapse weights
//     whose pre-neuron ∈ `pre` and post-neuron ∈ `post`.
//   - `average_weight_dynamics(pre, post, synapse, record)` — same but
//     averaged across a per-timestep weight record matrix.
//
// Note: Julia's `average_weight` uses `@unpack rowptr, colptr, I, J, index, W = synapse`
// where `rowptr` indexes POSTSYNAPTIC neurons (transposed CSR layout).
// Our `SpikingSynapse.matrix` is in standard row-major CSR (rows = pre),
// so we iterate pre → outgoing edges → filter post-neuron ∈ post_pop.
//
// Returns Float (mean weight). Empty intersection returns 0.0F.

///|
/// Check if `arr` (Array[Int]) contains `v`. Linear scan.
fn contains_int_local(arr : Array[Int], v : Int) -> Bool {
  let mut i = 0
  let n = arr.length()
  let mut found = false
  while i < n {
    if arr[i] == v {
      found = true
    }
    i = i + 1
  }
  found
}

///|
/// Mean of all synaptic weights whose pre-neuron ∈ `pre_pop_neurons`
/// AND post-neuron ∈ `post_pop_neurons`. Returns 0.0F if the set is
/// empty. Walks the CSR by pre-neuron (row-major).
pub fn average_weight(
  pre_pop_neurons : Array[Int],
  post_pop_neurons : Array[Int],
  synapse : SpikingSynapse,
) -> Float {
  let mut sum : Float = 0.0F
  let mut count : Int = 0
  let pre_n = synapse.pre.n
  let post_n = synapse.post.n
  let mat = synapse.matrix
  // Walk each pre neuron j. If j ∈ pre_pop_neurons, scan its outgoing edges.
  for j in 0..<pre_n {
    if contains_int_local(pre_pop_neurons, j) {
      // mat.rowptr[j] = start of row j's edges in colptr/vals.
      let start = mat.rowptr[j]
      let end_ = mat.rowptr[j + 1]
      let mut s = start
      while s < end_ {
        let post_idx = mat.colptr[s]
        if post_idx < post_n && contains_int_local(post_pop_neurons, post_idx) {
          sum = sum + mat.vals[s]
          count = count + 1
        }
        s = s + 1
      }
    }
  }
  if count == 0 {
    0.0F
  } else {
    sum / Float::from_int(count)
  }
}

///|
/// Mean of all synapse weights in `record[:, t]` (a per-timestep weight
/// record matrix of shape [n_connections, n_steps]) whose pre-neuron ∈
/// `pre_pop_neurons` AND post-neuron ∈ `post_pop_neurons`. Returns
/// `Array[Float]` of length `n_steps`.
///
/// `record` is structured as a flat row-major Array[Float] of length
/// `n_connections * n_steps` (MoonBit doesn't support true 2D arrays);
/// index via `record_get(record, n_steps, s, t)` for row s, col t.
pub fn average_weight_dynamics(
  pre_pop_neurons : Array[Int],
  post_pop_neurons : Array[Int],
  synapse : SpikingSynapse,
  record : Array[Float],
  n_steps : Int,
) -> Array[Float] {
  let pre_n = synapse.pre.n
  let post_n = synapse.post.n
  let mat = synapse.matrix
  // Collect the indices of matching edges first.
  let matched : Array[Int] = []
  for j in 0..<pre_n {
    if contains_int_local(pre_pop_neurons, j) {
      let start = mat.rowptr[j]
      let end_ = mat.rowptr[j + 1]
      let mut s = start
      while s < end_ {
        let post_idx = mat.colptr[s]
        if post_idx < post_n && contains_int_local(post_pop_neurons, post_idx) {
          matched.push(s)
        }
        s = s + 1
      }
    }
  }
  // Per-step mean across matched edges.
  let result : Array[Float] = Array::make(n_steps, 0.0F)
  let n_matched = matched.length()
  if n_matched == 0 {
    return result
  }
  for t in 0..<n_steps {
    let mut step_sum : Float = 0.0F
    for k in 0..<n_matched {
      let s = matched[k]
      step_sum = step_sum + record[s * n_steps + t]
    }
    result[t] = step_sum / Float::from_int(n_matched)
  }
  result
}

///|
/// Return the 0-based edge indices (into `mat.vals` / `mat.colptr`) of
/// all synapse weights whose pre ∈ `pre_pop_neurons` AND post ∈
/// `post_pop_neurons`. Mirrors Julia's `weights_indices` from
/// `refs/SNNUtils.jl/src/analysis/performance.jl`.
pub fn weights_indices(
  pre_pop_neurons : Array[Int],
  post_pop_neurons : Array[Int],
  synapse : SpikingSynapse,
) -> Array[Int] {
  let pre_n = synapse.pre.n
  let post_n = synapse.post.n
  let mat = synapse.matrix
  let result : Array[Int] = []
  for j in 0..<pre_n {
    if contains_int_local(pre_pop_neurons, j) {
      let start = mat.rowptr[j]
      let end_ = mat.rowptr[j + 1]
      let mut s = start
      while s < end_ {
        let post_idx = mat.colptr[s]
        if post_idx < post_n && contains_int_local(post_pop_neurons, post_idx) {
          result.push(s)
        }
        s = s + 1
      }
    }
  }
  result
}

///|
/// Multiply every weight in the synapse matrix whose pre ∈
/// `pre_pop_neurons` AND post ∈ `post_pop_neurons` by `factor`.
/// Mutates `synapse.matrix.vals` in place. Mirrors Julia's
/// `update_weight!` from
/// `refs/SNNUtils.jl/src/analysis/performance.jl` (default factor 1.2).
pub fn update_weight(
  pre_pop_neurons : Array[Int],
  post_pop_neurons : Array[Int],
  factor : Float,
  synapse : SpikingSynapse,
) -> Unit {
  let matched = weights_indices(pre_pop_neurons, post_pop_neurons, synapse)
  let mut k = 0
  while k < matched.length() {
    let s = matched[k]
    synapse.matrix.vals[s] = synapse.matrix.vals[s] * factor
    k = k + 1
  }
}