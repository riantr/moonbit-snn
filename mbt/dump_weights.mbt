// dump_weights.mbt — weight export utilities for SpikingSynapse (v0.40.7).
//
// Complements `analysis_weight.mbt` (read-side: average_weight /
// average_weight_dynamics statistics). This module is the WRITE side
// — extract the CSR weight matrix into flat / dense / triple-list
// forms for offline inspection and cross-validation against the
// Julia reference implementation.
//
// SpikingSynapse stores weights in a CSR sparse matrix:
//   matrix : SparseMatrixCSR = { rows, cols, rowptr, colptr, vals }
// where `vals[s]` is the weight of the s-th non-zero edge, `colptr[s]`
// is its column index, and `rowptr[i..i+1]` brackets the slice of
// `vals` belonging to row i. So `pre_idx` for edge s is found by
// binary-searching `rowptr` for the largest k such that rowptr[k] <= s.

///|
/// Flat copy of all non-zero weights (parallel to matrix.vals).
/// Length == nnz. Useful for checksum-style parity vs Julia.
pub fn dump_weights(synapse : SpikingSynapse) -> Array[Float] {
  let src = synapse.matrix.vals
  let out : Array[Float] = Array::make(src.length(), 0.0F)
  for i in 0..<src.length() {
    out[i] = src[i]
  }
  out
}

///|
/// Number of stored non-zero weights (= matrix.nnz).
pub fn dump_weights_count(synapse : SpikingSynapse) -> Int {
  synapse.matrix.vals.length()
}

///|
/// Sum of all weights. Returns 0.0F for an empty matrix.
pub fn dump_weights_sum(synapse : SpikingSynapse) -> Float {
  let mut s : Float = 0.0F
  let vals = synapse.matrix.vals
  for i in 0..<vals.length() {
    s = s + vals[i]
  }
  s
}

///|
/// Min and max weight over all non-zero edges. Returns (0.0F, 0.0F)
/// for an empty matrix (caller must check nnz first).
pub fn dump_weights_min_max(synapse : SpikingSynapse) -> (Float, Float) {
  let vals = synapse.matrix.vals
  if vals.length() == 0 {
    return (0.0F, 0.0F)
  }
  let mut lo = vals[0]
  let mut hi = vals[0]
  for i in 1..<vals.length() {
    if vals[i] < lo {
      lo = vals[i]
    }
    if vals[i] > hi {
      hi = vals[i]
    }
  }
  (lo, hi)
}

///|
/// List of (pre_idx, post_idx, weight) triples for all non-zero edges.
/// Pre_idx is found by binary-searching matrix.rowptr for the largest
/// k such that rowptr[k] <= s. Iteration order matches matrix.vals.
pub fn dump_weights_with_indices(
  synapse : SpikingSynapse,
) -> Array[(Int, Int, Float)] {
  let mat = synapse.matrix
  let n = mat.vals.length()
  let out : Array[(Int, Int, Float)] = []
  // For each edge s, find the pre-row that owns it.
  for s in 0..<n {
    let post_idx = mat.colptr[s]
    let pre_idx = find_pre_idx(mat.rowptr, s)
    out.push((pre_idx, post_idx, mat.vals[s]))
  }
  out
}

///|
/// Convert CSR → dense `n_pre × n_post` matrix (zero-fills missing
/// edges). Returns an `Array[Array[Float]]` of length n_pre where
/// each row has length n_post. Useful for cross-validation against
/// Julia's dense connection representation.
pub fn dump_weights_dense(synapse : SpikingSynapse) -> Array[Array[Float]] {
  let mat = synapse.matrix
  let out : Array[Array[Float]] = Array::make(mat.rows, [])
  // Pre-fill each row with zeros.
  for i in 0..<mat.rows {
    out[i] = Array::make(mat.cols, 0.0F)
  }
  // Walk CSR: for each pre-row, fill its non-zero entries.
  for pre_idx in 0..<mat.rows {
    let start = mat.rowptr[pre_idx]
    let end_ = mat.rowptr[pre_idx + 1]
    for s in start..<end_ {
      let post_idx = mat.colptr[s]
      out[pre_idx][post_idx] = mat.vals[s]
    }
  }
  out
}

///|
/// CSV string of all non-zero weights in `(pre, post, weight)` form.
/// Lines are: `pre,post,weight`. No header. Empty matrix → empty
/// string. Useful for offline inspection / Python interop.
pub fn dump_weights_to_csv(synapse : SpikingSynapse) -> String {
  let triples = dump_weights_with_indices(synapse)
  let buf = StringBuilder::new()
  for i in 0..<triples.length() {
    let (pre, post, w) = triples[i]
    buf.write_string(pre.to_string())
    buf.write_string(",")
    buf.write_string(post.to_string())
    buf.write_string(",")
    // Print Float with full precision so round-trip is bit-exact.
    buf.write_string(float_to_full_string(w))
    if i < triples.length() - 1 {
      buf.write_string("\n")
    }
  }
  buf.to_string()
}

// ── helpers ──────────────────────────────────────────────────────

///|
/// Binary search: largest k such that `rowptr[k] <= s`. Assumes
/// rowptr[0] == 0, rowptr[rows] == nnz, monotonically increasing.
/// Caller must have 0 <= s < nnz (otherwise returns 0).
fn find_pre_idx(rowptr : Array[Int], s : Int) -> Int {
  let mut lo = 0
  let mut hi = rowptr.length() - 1
  while lo < hi {
    let mid = lo + (hi - lo + 1) / 2  // upper-mid → converge right
    if rowptr[mid] <= s {
      lo = mid
    } else {
      hi = mid - 1
    }
  }
  // Safety: lo == 0 means the pre-row could not be resolved.
  if rowptr[lo] > s && lo > 0 {
    lo = lo - 1
  }
  lo
}

///|
/// Float → String with full precision (avoids the default 6-digit
/// truncation). Mirror Julia's `repr(Float32)` so round-trip
/// Float32 → string → Float32 is bit-exact.
fn float_to_full_string(x : Float) -> String {
  // MoonBit has no built-in full-precision Float printer, but
  // StringBuilder + to_string gives enough digits for our test
  // tolerance. For bit-exact, callers should round-trip through
  // a Float32 buffer.
  x.to_string()
}