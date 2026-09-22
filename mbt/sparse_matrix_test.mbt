// Tests for the sparse matrix (CSR).

///|
test "SparseMatrixCSR::empty" {
  let m = SparseMatrixCSR::empty(5, 5)
  assert_eq(m.rows, 5)
  assert_eq(m.cols, 5)
  assert_eq(m.nnz(), 0)
}

///|
test "SparseMatrixCSR::from_dense preserves non-zeros" {
  let dense : Array[Array[Float]] = [
    [1.0F, 0.0F, 2.0F],
    [0.0F, 3.0F, 0.0F],
    [4.0F, 0.0F, 5.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.001F)
  assert_eq(m.nnz(), 5)
  assert_eq(m.get(0, 0), 1.0F)
  assert_eq(m.get(0, 1), 0.0F)
  assert_eq(m.get(0, 2), 2.0F)
  assert_eq(m.get(1, 1), 3.0F)
  assert_eq(m.get(2, 0), 4.0F)
  assert_eq(m.get(2, 2), 5.0F)
}

///|
test "SparseMatrixCSR::set inserts new value" {
  let m = SparseMatrixCSR::empty(3, 3)
  m.set(1, 1, 5.0F)
  assert_eq(m.nnz(), 1)
  assert_eq(m.get(1, 1), 5.0F)
  assert_eq(m.row_nnz(1), 1)
}

///|
test "SparseMatrixCSR::set replaces existing value" {
  let m = SparseMatrixCSR::empty(3, 3)
  m.set(1, 1, 5.0F)
  m.set(1, 1, 7.0F)
  assert_eq(m.nnz(), 1)
  assert_eq(m.get(1, 1), 7.0F)
}

///|
test "SparseMatrixCSR::set across multiple rows" {
  let m = SparseMatrixCSR::empty(4, 4)
  m.set(0, 0, 1.0F)
  m.set(1, 1, 2.0F)
  m.set(2, 2, 3.0F)
  m.set(3, 3, 4.0F)
  assert_eq(m.get(0, 0), 1.0F)
  assert_eq(m.get(1, 1), 2.0F)
  assert_eq(m.get(2, 2), 3.0F)
  assert_eq(m.get(3, 3), 4.0F)
  assert_eq(m.row_nnz(0), 1)
  assert_eq(m.row_nnz(1), 1)
  assert_eq(m.row_nnz(2), 1)
  assert_eq(m.row_nnz(3), 1)
}

///|
test "SparseMatrixCSR::forward only fires on pre spikes" {
  let dense : Array[Array[Float]] = [
    [1.0F, 2.0F],
    [3.0F, 4.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.001F)
  let pre_fire : Array[Bool] = [true, false]
  let post_g : Array[Float] = [0.0F, 0.0F]
  m.forward(pre_fire, post_g)
  // Only row 0 fired; weights 1.0 and 2.0 added to post 0 and post 1
  assert_eq(post_g[0], 1.0F)
  assert_eq(post_g[1], 2.0F)
}

///|
test "SparseMatrixCSR::random produces expected nnz" {
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let m = SparseMatrixCSR::random(100, 100, 0.5F, 0.1F, 0.8F, rng)
  // Expected nnz ≈ 100 * 100 * 0.8 = 8000; tol ±15%
  let n = m.nnz()
  if n < 6800 || n > 9200 {
    fail("SparseMatrixCSR::random nnz = " + n.to_string() + " (expected ~8000)")
  }
}

///|
test "SparseMatrixCSR::random — fixed seed is deterministic" {
  let rng1 = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 1UL, 2UL)
  let m1 = SparseMatrixCSR::random(20, 20, 0.5F, 0.1F, 0.5F, rng1)
  let rng2 = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 1UL, 2UL)
  let m2 = SparseMatrixCSR::random(20, 20, 0.5F, 0.1F, 0.5F, rng2)
  // Same nnz, same first entry.
  assert_eq(m1.nnz(), m2.nnz())
  if m1.nnz() > 0 {
    assert_eq(m1.vals[0], m2.vals[0])
  }
}

///|
test "ConnectRule::FixedIn — every post has same in-degree" {
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let rows = 10
  let cols = 8
  let p = 0.3F
  let m = SparseMatrixCSR::random_with_rule(rows, cols, 1.0F, 0.0F, p, FixedIn, rng)
  // n_keep = round(10 * 0.3) = 3
  let expected_in = 3
  for j in 0..<cols {
    let mut in_deg = 0
    for i in 0..<rows {
      if m.get(i, j) > 0.0F {
        in_deg = in_deg + 1
      }
    }
    assert_eq(in_deg, expected_in)
  }
}

///|
test "ConnectRule::FixedOut — every pre has same out-degree" {
  let rng = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let rows = 6
  let cols = 10
  let p = 0.4F
  let m = SparseMatrixCSR::random_with_rule(rows, cols, 1.0F, 0.0F, p, FixedOut, rng)
  // n_keep = round(10 * 0.4) = 4
  let expected_out = 4
  for i in 0..<rows {
    assert_eq(m.row_nnz(i), expected_out)
  }
}

///|
test "ConnectRule::Bernoulli — matches default random()" {
  let rng1 = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let m1 = SparseMatrixCSR::random(20, 20, 1.0F, 0.0F, 0.5F, rng1)
  let rng2 = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let m2 = SparseMatrixCSR::random_with_rule(20, 20, 1.0F, 0.0F, 0.5F, Bernoulli, rng2)
  // Default Bernoulli rule should produce the same nnz as random().
  assert_eq(m1.nnz(), m2.nnz())
}
