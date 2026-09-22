// Tests for attention_mask utilities.

///|
fn am_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "causal_mask: shape + diagonal=0 + above-diagonal=-1e9" {
  let seq_len = 4
  let m = causal_mask(seq_len)
  assert_eq(m.length(), seq_len * seq_len)
  // Below diagonal (j <= i): unconstrained
  for i in 0..<seq_len {
    for j in 0..<(i + 1) {
      if !am_eps(m[i * seq_len + j], 0.0F, 0.0001F) {
        fail("m[\{i},\{j}] = \{m[i * seq_len + j]}, expected 0")
      }
    }
  }
  // Above diagonal (j > i): blocked
  for i in 0..<seq_len {
    for j in (i + 1)..<seq_len {
      let v = m[i * seq_len + j]
      if v > -100000000.0F {
        fail("m[\{i},\{j}] = \{v}, expected -1e9 (blocked)")
      }
    }
  }
}

///|
test "causal_mask: row 0 can only attend to position 0" {
  // Strong test: row 0 has only m[0,0] = 0; all others = -1e9.
  let m = causal_mask(5)
  assert_eq(m[0], 0.0F)
  for j in 1..<5 {
    let v = m[0 * 5 + j]
    if v > -100000000.0F {
      fail("row 0 col \{j} = \{v}, expected -1e9")
    }
  }
}

///|
test "mask_broadcast: shape + values replicated across heads" {
  let seq_len = 3
  let num_heads = 2
  let mask_2d = causal_mask(seq_len)
  let full = mask_broadcast(mask_2d, seq_len, num_heads)
  assert_eq(full.length(), num_heads * seq_len * seq_len)
  // Each head has the same mask.
  for h in 0..<num_heads {
    for i in 0..<seq_len {
      for j in 0..<seq_len {
        let head_off = h * seq_len * seq_len
        let v = full[head_off + i * seq_len + j]
        let expected = mask_2d[i * seq_len + j]
        if v != expected {
          fail("head \{h} (\{i},\{j}) = \{v} != original \{expected}")
        }
      }
    }
  }
}

///|
test "mask_from_allowed: arbitrary boolean matrix" {
  let seq_len = 3
  // Allowed matrix:
  //   row 0: T F F  (only self)
  //   row 1: T T F  (self + previous)
  //   row 2: F T T  (self + previous two, but not "future")
  let allowed : Array[Array[Bool]] = [
    [true, false, false],
    [true, true, false],
    [false, true, true],
  ]
  let m = mask_from_allowed(seq_len, allowed)
  assert_eq(m.length(), 9)
  // Expected values (0 = allowed, -1e9 = blocked):
  //   [0, -1e9, -1e9]
  //   [0,    0, -1e9]
  //  [-1e9,  0,    0]
  for i in 0..<seq_len {
    for j in 0..<seq_len {
      let v = m[i * seq_len + j]
      let allowed_ij = allowed[i][j]
      if allowed_ij {
        if !am_eps(v, 0.0F, 0.0001F) {
          fail("(\{i},\{j}) allowed but m = \{v}")
        }
      } else {
        if v > -100000000.0F {
          fail("(\{i},\{j}) blocked but m = \{v}")
        }
      }
    }
  }
}

///|
test "causal_mask_heads: end-to-end via MHA, position i only attends to ≤i" {
  let seq_len = 4
  let d_model = 4
  let num_heads = 1
  let mha = MultiHeadAttention::new(d_model, num_heads, 1UL)
  let x : Array[Float] = Array::make(seq_len * d_model, 0.5F)
  let mask = causal_mask_heads(seq_len, num_heads)
  let (_out, cache) = multi_head_attention_forward(x, mha, mask)
  // Verify weights: weight[i, j] = 0 for j > i (causal block).
  let w = cache.weights
  for i in 0..<seq_len {
    for j in (i + 1)..<seq_len {
      let v = w[i * seq_len + j]
      if v > 0.0001F {
        fail("weights[\{i},\{j}] = \{v}, expected ≈ 0 (causal block)")
      }
    }
    // And sum-to-1 across the i+1 allowed keys.
    let mut sum_w = 0.0F
    for j in 0..<(i + 1) {
      sum_w = sum_w + w[i * seq_len + j]
    }
    if !am_eps(sum_w, 1.0F, 0.001F) {
      fail("weights row \{i} sum = \{sum_w}, expected 1.0")
    }
  }
}