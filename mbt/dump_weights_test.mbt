// dump_weights_test.mbt — tests for v0.40.7 dump_weights.mbt.
//
// Covers:
//   - empty matrix edge cases (count=0, sum=0, empty output)
//   - flat dump_weights parity with matrix.vals
//   - count / sum / min-max helpers
//   - (pre, post, weight) triple extraction with correct pre indices
//   - dense CSR → n_pre × n_post conversion
//   - CSV output format

///|
fn make_if_pair(n_pre : Int, n_post : Int) -> (IF, IF) {
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(n_pre, IFParameter::new(), rng)
  let post = IF::new(n_post, IFParameter::new(), rng)
  (pre, post)
}

///|
test "dump_weights_empty_matrix_returns_empty_array" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let out = dump_weights(syn)
  if out.length() != 0 {
    fail("empty matrix should yield empty array, got \{out.length()}")
  }
}

///|
test "dump_weights_count_zero_for_no_connections" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  if dump_weights_count(syn) != 0 {
    fail("empty matrix should have nnz=0")
  }
}

///|
test "dump_weights_sum_zero_for_no_connections" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  if dump_weights_sum(syn) != 0.0F {
    fail("empty matrix sum should be 0.0F")
  }
}

///|
test "dump_weights_min_max_zero_zero_for_empty" {
  let (pre, post) = make_if_pair(2, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let (lo, hi) = dump_weights_min_max(syn)
  if lo != 0.0F || hi != 0.0F {
    fail("empty matrix min_max should be (0, 0), got (\{lo}, \{hi})")
  }
}

///|
test "dump_weights_flat_matches_matrix_vals_in_order" {
  let (pre, post) = make_if_pair(3, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 0.5F)
  spiking_connect(syn, 1, 2, 1.5F)
  spiking_connect(syn, 2, 1, 2.5F)
  spiking_connect(syn, 3, 2, 3.5F)
  let out = dump_weights(syn)
  if out.length() != 4 {
    fail("expected 4 entries, got \{out.length()}")
  }
  // Iteration order: pre=0 first (post=0, post=1), then pre=1 (post=0),
  // then pre=2 (post=1). Matches CSR row-major.
  if (out[0] - 0.5F).abs() > 1.0e-6F {
    fail("out[0] = \{out[0]}, expected 0.5")
  }
  if (out[1] - 1.5F).abs() > 1.0e-6F {
    fail("out[1] = \{out[1]}, expected 1.5")
  }
  if (out[2] - 2.5F).abs() > 1.0e-6F {
    fail("out[2] = \{out[2]}, expected 2.5")
  }
  if (out[3] - 3.5F).abs() > 1.0e-6F {
    fail("out[3] = \{out[3]}, expected 3.5")
  }
}

///|
test "dump_weights_count_matches_nnz_after_connections" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 3, 2.0F)
  spiking_connect(syn, 2, 2, 3.0F)
  if dump_weights_count(syn) != 3 {
    fail("expected nnz=3, got \{dump_weights_count(syn)}")
  }
}

///|
test "dump_weights_sum_for_known_weights" {
  let (pre, post) = make_if_pair(2, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  spiking_connect(syn, 2, 1, 3.0F)
  spiking_connect(syn, 2, 2, 4.0F)
  let s = dump_weights_sum(syn)
  if (s - 10.0F).abs() > 1.0e-5F {
    fail("sum = \{s}, expected 10.0")
  }
}

///|
test "dump_weights_min_max_correct_for_known_weights" {
  let (pre, post) = make_if_pair(3, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, -2.0F)
  spiking_connect(syn, 1, 2, 5.0F)
  spiking_connect(syn, 2, 1, 0.5F)
  spiking_connect(syn, 3, 2, -7.0F)
  let (lo, hi) = dump_weights_min_max(syn)
  if (lo - -7.0F).abs() > 1.0e-6F {
    fail("min = \{lo}, expected -7.0")
  }
  if (hi - 5.0F).abs() > 1.0e-6F {
    fail("max = \{hi}, expected 5.0")
  }
}

///|
test "dump_weights_with_indices_extracts_correct_triples" {
  let (pre, post) = make_if_pair(3, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  // Build a known topology:
  //   pre=0 (1st) → post=0 (1st)  w=1.0
  //   pre=0 (1st) → post=1 (2nd)  w=2.0
  //   pre=1 (2nd) → post=0        w=3.0
  //   pre=2 (3rd) → post=1        w=4.0
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  spiking_connect(syn, 2, 1, 3.0F)
  spiking_connect(syn, 3, 2, 4.0F)
  let triples = dump_weights_with_indices(syn)
  if triples.length() != 4 {
    fail("expected 4 triples, got \{triples.length()}")
  }
  // Order: CSR row-major = pre asc, then post asc within row.
  let (p0, q0, w0) = triples[0]
  if p0 != 0 || q0 != 0 || (w0 - 1.0F).abs() > 1.0e-6F {
    fail("triple[0] = (\{p0}, \{q0}, \{w0}), expected (0, 0, 1.0)")
  }
  let (p1, q1, w1) = triples[1]
  if p1 != 0 || q1 != 1 || (w1 - 2.0F).abs() > 1.0e-6F {
    fail("triple[1] = (\{p1}, \{q1}, \{w1}), expected (0, 1, 2.0)")
  }
  let (p2, q2, w2) = triples[2]
  if p2 != 1 || q2 != 0 || (w2 - 3.0F).abs() > 1.0e-6F {
    fail("triple[2] = (\{p2}, \{q2}, \{w2}), expected (1, 0, 3.0)")
  }
  let (p3, q3, w3) = triples[3]
  if p3 != 2 || q3 != 1 || (w3 - 4.0F).abs() > 1.0e-6F {
    fail("triple[3] = (\{p3}, \{q3}, \{w3}), expected (2, 1, 4.0)")
  }
}

///|
test "dump_weights_with_indices_empty_for_no_connections" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let triples = dump_weights_with_indices(syn)
  if triples.length() != 0 {
    fail("empty matrix should yield no triples, got \{triples.length()}")
  }
}

///|
test "dump_weights_dense_correct_shape" {
  let (pre, post) = make_if_pair(3, 4)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let dense = dump_weights_dense(syn)
  if dense.length() != 3 {
    fail("expected 3 rows, got \{dense.length()}")
  }
  for i in 0..<3 {
    if dense[i].length() != 4 {
      fail("row \{i} length = \{dense[i].length()}, expected 4")
    }
  }
}

///|
test "dump_weights_dense_zeros_for_missing_edges" {
  // 2×3 matrix with only (0,1) and (1,2) filled in. All other
  // 4 entries must be 0.0F.
  let (pre, post) = make_if_pair(2, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 2, 0.7F)  // (0,1) = 0.7
  spiking_connect(syn, 2, 3, 1.3F)  // (1,2) = 1.3
  let dense = dump_weights_dense(syn)
  // dense[0][0] = 0
  if dense[0][0] != 0.0F {
    fail("dense[0][0] = \{dense[0][0]}, expected 0")
  }
  // dense[0][1] = 0.7
  if (dense[0][1] - 0.7F).abs() > 1.0e-6F {
    fail("dense[0][1] = \{dense[0][1]}, expected 0.7")
  }
  // dense[0][2] = 0
  if dense[0][2] != 0.0F {
    fail("dense[0][2] = \{dense[0][2]}, expected 0")
  }
  // dense[1][0] = 0
  if dense[1][0] != 0.0F {
    fail("dense[1][0] = \{dense[1][0]}, expected 0")
  }
  // dense[1][1] = 0
  if dense[1][1] != 0.0F {
    fail("dense[1][1] = \{dense[1][1]}, expected 0")
  }
  // dense[1][2] = 1.3
  if (dense[1][2] - 1.3F).abs() > 1.0e-6F {
    fail("dense[1][2] = \{dense[1][2]}, expected 1.3")
  }
}

///|
test "dump_weights_dense_all_zero_for_empty_matrix" {
  let (pre, post) = make_if_pair(2, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let dense = dump_weights_dense(syn)
  for i in 0..<2 {
    for j in 0..<3 {
      if dense[i][j] != 0.0F {
        fail("empty dense[\{i}][\{j}] = \{dense[i][j]}, expected 0")
      }
    }
  }
}

///|
test "dump_weights_to_csv_correct_format" {
  let (pre, post) = make_if_pair(2, 2)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.5F)
  spiking_connect(syn, 2, 2, 2.5F)
  let csv = dump_weights_to_csv(syn)
  // Expected: 2 lines, "0,0,1.5\n1,1,2.5". We verify by counting
  // newlines (must be exactly 1) and substring checks. We avoid
  // String::split because it returns Iter[StringView], not
  // Array[String], so indexing is not supported.
  let mut newline_count = 0
  let mut has_first = false
  let mut has_second = false
  for i in 0..<csv.length() {
    if csv[i] == '\n' {
      newline_count = newline_count + 1
    }
  }
  if newline_count != 1 {
    fail("expected exactly 1 newline, got \{newline_count}, csv='\{csv}'")
  }
  // "0,0," prefix (line 1: pre=0, post=0, weight=1.5)
  has_first = csv.has_prefix("0,0,")
  // "1,1," prefix (line 2: pre=1, post=1, weight=2.5)
  has_second = csv.contains("1,1,")
  if !has_first {
    fail("csv = '\{csv}', expected to start with '0,0,'")
  }
  if !has_second {
    fail("csv = '\{csv}', expected to contain '1,1,'")
  }
  // Weights must be present (Float to_string may differ in trailing zeros
  // — substring "1.5" matches "1.5", "1.50", "1.5000" etc).
  if !csv.contains("1.5") {
    fail("csv = '\{csv}', expected to contain weight 1.5")
  }
  if !csv.contains("2.5") {
    fail("csv = '\{csv}', expected to contain weight 2.5")
  }
}

///|
test "dump_weights_to_csv_empty_matrix_yields_empty_string" {
  let (pre, post) = make_if_pair(3, 3)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let csv = dump_weights_to_csv(syn)
  if csv.length() != 0 {
    fail("empty matrix CSV should be '', got '\{csv}'")
  }
}