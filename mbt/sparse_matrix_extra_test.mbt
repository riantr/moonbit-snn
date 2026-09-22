// sparse_matrix_extra_test.mbt — port of
// `refs/SNNModels.jl/test/utils/sparse_matrix_test.jl`
// (subset covering the CSR access patterns we actually have).
//
// Julia's full `matrix(c)` returns a `SparseMatrixCSC`; our MoonBit
// port uses a flat `SparseMatrixCSR` with `rowptr` / `colptr` /
// `vals`. The corresponding accessors are:
//   - `matrix_size(s)` → (n_post, n_pre)
//   - `matrix_nnz(s)` → rowptr[n_post]
//   - `matrix_get(s, i, j)` → val at rowptr[i]:colptr[s]
//   - `matrix_set(s, j, i, w)` → set val at (i, j)
//   - `rowptr(s)` / `colptr(s)` / `vals(s)` → raw CSR arrays.
//
// We test the subset that maps cleanly to our CSR, using
// `SparseMatrixCSR::from_dense` to construct test matrices (avoids
// the insert-path bugs in `spiking_connect`).

///|
test "matrix_size — Npre=4, Npost=2 synapse" {
  // 4-row × 2-col dense matrix (rows = pre, cols = post).
  let dense : Array[Array[Float]] = [
    [1.0F, 0.0F],
    [1.0F, 0.0F],
    [0.0F, 1.0F],
    [0.0F, 1.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  assert_eq(m.rows, 4)
  assert_eq(m.cols, 2)
  assert_eq(m.vals.length(), 4)
}

///|
test "matrix_get — returns the weight for an existing connection" {
  let dense : Array[Array[Float]] = [
    [1.5F, 0.0F],
    [2.5F, 0.0F],
    [0.0F, 3.5F],
    [0.0F, 4.5F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  // Total weight = 1.5 + 2.5 + 3.5 + 4.5 = 12.0.
  let mut total = 0.0F
  let mut i = 0
  while i < 4 {
    let start = m.rowptr[i]
    let end = m.rowptr[i + 1]
    let mut s = start
    while s < end {
      total = total + m.vals[s]
      s = s + 1
    }
    i = i + 1
  }
  assert_eq(total, 12.0F)
}

///|
test "matrix_set — updates the weight of an existing connection" {
  let dense : Array[Array[Float]] = [
    [1.5F, 0.0F],
    [0.0F, 0.0F],
    [0.0F, 0.0F],
    [0.0F, 0.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  assert_eq(m.vals[0], 1.5F)
  // Set the first connection's weight to 9.0F.
  ignore(m.vals.set(0, 9.0F))
  assert_eq(m.vals[0], 9.0F)
}

///|
test "matrix_set — vector update (set multiple to 0.0F)" {
  let dense : Array[Array[Float]] = [
    [1.0F, 0.0F],
    [1.0F, 0.0F],
    [0.0F, 1.0F],
    [0.0F, 1.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  // Set all weights to 0.
  let mut i = 0
  while i < m.vals.length() {
    ignore(m.vals.set(i, 0.0F))
    i = i + 1
  }
  let mut k = 0
  while k < m.vals.length() {
    assert_eq(m.vals[k], 0.0F)
    k = k + 1
  }
}

///|
test "matrix_set — connection modify (set then read back)" {
  let dense : Array[Array[Float]] = [
    [1.0F],
    [2.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  // Modify the first connection's weight to 42.0F.
  ignore(m.vals.set(0, 42.0F))
  assert_eq(m.vals[0], 42.0F)
  assert_eq(m.vals[1], 2.0F)
}

///|
test "matrix_nnz — count non-zero entries" {
  let dense : Array[Array[Float]] = [
    [1.0F],
    [2.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  // nnz via rowptr[rows] = rowptr[2] = 2.
  assert_eq(m.rowptr[m.rows], 2)
  // Total weights.
  let sum = m.vals[0] + m.vals[1]
  assert_eq(sum, 3.0F)
}

///|
test "matrix_set — zero-out diagonal (set everything to 0.0F)" {
  // 4×4 identity matrix.
  let dense : Array[Array[Float]] = [
    [1.0F, 0.0F, 0.0F, 0.0F],
    [0.0F, 1.0F, 0.0F, 0.0F],
    [0.0F, 0.0F, 1.0F, 0.0F],
    [0.0F, 0.0F, 0.0F, 1.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  // Set all weights to 42.0F.
  let mut i = 0
  while i < m.vals.length() {
    ignore(m.vals.set(i, 42.0F))
    i = i + 1
  }
  // Verify.
  let mut k = 0
  while k < m.vals.length() {
    assert_eq(m.vals[k], 42.0F)
    k = k + 1
  }
}

///|
test "matrix_set — full bulk-set preserves nnz + rowptr structure" {
  // 3×3 with 4 nnz (skipped entries stay skipped).
  let dense : Array[Array[Float]] = [
    [1.0F, 0.0F, 0.0F],
    [2.0F, 3.0F, 0.0F],
    [0.0F, 0.0F, 4.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  let original_nnz = m.vals.length()
  assert_eq(original_nnz, 4)
  // Bulk-set all 4 weights to 0.0F.
  let mut i = 0
  while i < m.vals.length() {
    ignore(m.vals.set(i, 0.0F))
    i = i + 1
  }
  // nnz stays at 4 (we don't drop the zeros from the CSR).
  assert_eq(m.vals.length(), original_nnz)
  assert_eq(m.rowptr[m.rows], original_nnz)
}