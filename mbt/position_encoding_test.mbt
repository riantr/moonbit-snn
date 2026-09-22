// Tests for position_encoding.

///|
fn pe_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "sinusoidal_position_encoding: shape + boundary values" {
  let max_len = 10
  let d_model = 8
  let pe = sinusoidal_position_encoding(max_len, d_model)
  assert_eq(pe.length(), max_len * d_model)
  // PE[0, *] : sin(0) = 0 for even j, cos(0) = 1 for odd j.
  for j in 0..<d_model {
    let v = pe[j]
    if j % 2 == 0 {
      if !pe_eps(v, 0.0F, 0.0001F) {
        fail("PE[0, \{j}] = \{v}, expected sin(0) ≈ 0")
      }
    } else {
      if !pe_eps(v, 1.0F, 0.0001F) {
        fail("PE[0, \{j}] = \{v}, expected cos(0) ≈ 1")
      }
    }
  }
}

///|
test "sinusoidal_position_encoding: parity (even=sin, odd=cos) + same-pos same-value" {
  let pe = sinusoidal_position_encoding(5, 6)
  // For pos=1, j=0 → sin(1/10000^(0/6)) = sin(1) ≈ 0.8415
  // For pos=1, j=1 → cos(1) ≈ 0.5403
  let v00 = pe[1 * 6 + 0]
  if !pe_eps(v00, 0.8415F, 0.001F) {
    fail("PE[1, 0] = \{v00}, expected sin(1) ≈ 0.8415")
  }
  let v01 = pe[1 * 6 + 1]
  if !pe_eps(v01, 0.5403F, 0.001F) {
    fail("PE[1, 1] = \{v01}, expected cos(1) ≈ 0.5403")
  }
  // PE is deterministic (no RNG state)
  let pe2 = sinusoidal_position_encoding(5, 6)
  for i in 0..<pe.length() {
    if pe[i] != pe2[i] {
      fail("non-deterministic at \{i}: \{pe[i]} vs \{pe2[i]}")
    }
  }
}

///|
test "sinusoidal_position_encoding: distinct rows for distinct positions" {
  // Two different positions should produce different rows.
  let pe = sinusoidal_position_encoding(4, 4)
  let mut differ = false
  for j in 0..<4 {
    if pe[0 * 4 + j] != pe[1 * 4 + j] {
      differ = true
      break
    }
  }
  if !differ {
    fail("PE[0, *] == PE[1, *] — positions not distinguished")
  }
}

///|
test "PositionalEmbedding::new: shape + non-zero init + deterministic seed" {
  let pe_a = PositionalEmbedding::new(8, 4, 42UL)
  let pe_b = PositionalEmbedding::new(8, 4, 42UL)
  assert_eq(pe_a.weight.length(), 32)
  assert_eq(pe_a.max_len, 8)
  assert_eq(pe_a.d_model, 4)
  // Deterministic
  for i in 0..<pe_a.weight.length() {
    if pe_a.weight[i] != pe_b.weight[i] {
      fail("non-deterministic at \{i}")
    }
  }
  // Non-zero init
  let mut non_zero = 0
  for i in 0..<pe_a.weight.length() {
    if pe_a.weight[i] != 0.0F {
      non_zero = non_zero + 1
    }
  }
  if non_zero < 30 {
    fail("only \{non_zero}/32 non-zero weights")
  }
}

///|
test "positional_embedding_forward: slices correctly + backward" {
  let pe = PositionalEmbedding::new(10, 4, 1UL)
  let seq_len = 3
  let out = positional_embedding_forward(pe, seq_len)
  assert_eq(out.length(), seq_len * 4)
  // Forward slice matches the first seq_len rows of pe.weight.
  for i in 0..<(seq_len * 4) {
    if out[i] != pe.weight[i] {
      fail("forward[\{i}] = \{out[i]}, weight = \{pe.weight[i]}")
    }
  }
  // Backward: only active positions get gradient
  let d_output : Array[Float] = Array::make(seq_len * 4, 0.7F)
  let d_weight = positional_embedding_backward(pe, d_output, seq_len)
  assert_eq(d_weight.length(), 10 * 4)
  for s in 0..<3 {
    for j in 0..<4 {
      let off = s * 4 + j
      if d_weight[off] != 0.7F {
        fail("d_weight[\{off}] = \{d_weight[off]}, expected 0.7")
      }
    }
  }
  // Inactive positions stay zero
  for s in 3..<10 {
    for j in 0..<4 {
      let off = s * 4 + j
      if d_weight[off] != 0.0F {
        fail("inactive d_weight[\{off}] = \{d_weight[off]}, expected 0")
      }
    }
  }
}