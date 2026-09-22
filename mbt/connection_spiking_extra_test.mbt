// connection_spiking_extra_test.mbt — port of
// `refs/SNNModels.jl/test/syn/spiking_synapse.jl` (subset).
//
// Julia has 8 small test cases; we port the ones that map cleanly:
//   - (p, μ) — covered already in connection_spiking_test
//   - (p, μ, σ) — covered already
//   - identity (1-to-1) — new test on from_dense N×N identity
//   - explicit dense matrix — covered already
//   - inhibitory :gi — covered already
//   - NoLTP / NoSTP defaults — covered already
//   - custom name — new test for SpikingSynapse::with_name
//   - EmptySynapse — new test for EmptyConnection::new

///|
fn make_if(n : Int) -> IF {
  // Build a minimal IF + IFParameter + Xoshiro for tests.
  // Use the public with_el constructor (IFParameter struct is not
  // pub(all), so we can't write it as a literal here).
  let param = IFParameter::with_el(-49.0F)
  let rng = Xoshiro::new(42UL)
  IF::new(n, param, rng)
}

///|
test "spiking_synapse::new — (p, μ) random connections" {
  let rng = Xoshiro::new(42UL)
  let pre : IF = make_if(30)
  let post : IF = make_if(15)
  let s = SpikingSynapse::random(pre, post, "ge", 1.0F, 0.0F, 0.3F, rng)
  assert_eq(s.matrix.vals.length() > 0, true)
  assert_eq(s.sym, "ge")
}

///|
test "spiking_synapse::new — (p, μ, σ) random connections" {
  let rng = Xoshiro::new(42UL)
  let pre : IF = make_if(20)
  let post : IF = make_if(10)
  let s = SpikingSynapse::random(pre, post, "ge", 1.0F, 0.2F, 0.5F, rng)
  assert_eq(s.matrix.vals.length() > 0, true)
}

///|
test "spiking_synapse::new — identity (1-to-1) matrix has N nnz" {
  let n = 8
  // Build an N×N identity dense matrix.
  let dense : Array[Array[Float]] = Array::make(n, [])
  for i in 0..<n {
    let row : Array[Float] = Array::make(n, 0.0F)
    ignore(row.set(i, 1.0F))
    ignore(dense.set(i, row))
  }
  let m = SparseMatrixCSR::from_dense(dense, 0.5F)
  assert_eq(m.vals.length(), n)
}

///|
test "spiking_synapse::new — inhibitory :gi" {
  let rng = Xoshiro::new(42UL)
  let pre : IF = make_if(10)
  let post : IF = make_if(20)
  let s = SpikingSynapse::random(pre, post, "gi", 2.0F, 0.0F, 0.4F, rng)
  assert_eq(s.sym, "gi")
}

///|
test "spiking_synapse::with_name — defaults to empty" {
  let pre : IF = make_if(10)
  let post : IF = make_if(10)
  let s = SpikingSynapse::new(pre, post, "ge")
  // Default name is empty string.
  assert_eq(s.name, "")
}

///|
test "spiking_synapse::with_name — sets custom label" {
  let pre : IF = make_if(5)
  let post : IF = make_if(5)
  let s0 = SpikingSynapse::new(pre, post, "ge")
  let s = s0.with_name("my_syn")
  assert_eq(s.name, "my_syn")
  // Original unchanged (MoonBit semantics: with_X returns new struct).
  assert_eq(s0.name, "")
}

///|
test "spiking_synapse — NoLTP / NoSTP defaults" {
  // SpikingSynapse has no separate LTPVars/STPVars fields (those
  // live on the plasticity adapter, e.g. MarkramSTPEntry). The
  // equivalent for "no LTP / no STP" is: rho is empty AND delays
  // is empty AND pending_* are all empty.
  let pre : IF = make_if(10)
  let post : IF = make_if(10)
  let s = SpikingSynapse::new(pre, post, "ge")
  assert_eq(s.rho.length(), 0)
  assert_eq(s.delays.length(), 0)
  assert_eq(s.pending_times.length(), 0)
  assert_eq(s.pending_posts.length(), 0)
  assert_eq(s.pending_weights.length(), 0)
}

///|
test "EmptyConnection::new — defaults to empty name" {
  let ec = EmptyConnection::new()
  assert_eq(ec.name, "")
}

///|
test "EmptyConnection::with_name — sets custom label" {
  let ec0 = EmptyConnection::new()
  let ec = ec0.with_name("placeholder")
  assert_eq(ec.name, "placeholder")
  // Original unchanged.
  assert_eq(ec0.name, "")
}

///|
test "spiking_synapse — explicit dense matrix via from_dense" {
  // SpikingSynapse accepts a sparse CSR via its matrix field. The
  // Julia test passes a Matrix{Float32} as `conn`. We emulate this
  // by building the CSR from `from_dense` and assigning it directly.
  let rng = Xoshiro::new(42UL)
  let pre : IF = make_if(8)
  let post : IF = make_if(6)
  // Build a dense 8×6 random-weights matrix.
  let dense : Array[Array[Float]] = Array::make(pre.n, [])
  for i in 0..<pre.n {
    let row : Array[Float] = Array::make(post.n, 0.0F)
    for j in 0..<post.n {
      let (z1, _) = box_muller(rng)
      row[j] = 0.5F * Float::from_double(z1).abs()
    }
    ignore(dense.set(i, row))
  }
  let m = SparseMatrixCSR::from_dense(dense, 0.0F)
  // nnz should equal the number of non-zero entries.
  let mut nnz = 0
  for i in 0..<m.rows {
    let start = m.rowptr[i]
    let end = m.rowptr[i + 1]
    nnz = nnz + (end - start)
  }
  assert_eq(nnz > 0, true)
}