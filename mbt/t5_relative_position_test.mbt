// Tests for T5-style relative position bias.

///|
fn t5_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "T5RelativePosition::new: shape + Xavier init non-zero" {
  let rp = T5RelativePosition::new(4, 8, 42UL)
  assert_eq(rp.num_heads, 4)
  assert_eq(rp.max_distance, 8)
  assert_eq(rp.weight.length(), 4 * 17)  // 2*8+1 = 17 buckets
  let mut non_zero = 0
  for i in 0..<rp.weight.length() {
    if rp.weight[i] != 0.0F {
      non_zero = non_zero + 1
    }
  }
  if non_zero < 60 {
    fail("only \{non_zero}/68 non-zero weights (Xavier init failed?)")
  }
}

///|
test "t5_compute_bias: shape + depends only on relative offset" {
  let rp = T5RelativePosition::new(2, 4, 1UL)
  let seq_len = 5
  let bias = t5_compute_bias(rp, seq_len)
  assert_eq(bias.length(), 2 * seq_len * seq_len)
  // bias[h, i, j] depends only on j - i, clamped to [-4, 4].
  // So bias[h, 0, 1] == bias[h, 2, 3] (both offset = +1).
  for h in 0..<2 {
    let v01 = bias[h * seq_len * seq_len + 0 * seq_len + 1]
    let v23 = bias[h * seq_len * seq_len + 2 * seq_len + 3]
    if v01 != v23 {
      fail("bias[h=\{h}, 0, 1] = \{v01} != bias[h=\{h}, 2, 3] = \{v23}")
    }
    // And bias[h, 1, 0] == bias[h, 3, 2] (both offset = -1).
    let v10 = bias[h * seq_len * seq_len + 1 * seq_len + 0]
    let v32 = bias[h * seq_len * seq_len + 3 * seq_len + 2]
    if v10 != v32 {
      fail("bias[h=\{h}, 1, 0] = \{v10} != bias[h=\{h}, 3, 2] = \{v32}")
    }
  }
}

///|
test "t5_compute_bias: clamps relative offset beyond max_distance" {
  let rp = T5RelativePosition::new(1, 2, 1UL)
  let seq_len = 6
  let bias = t5_compute_bias(rp, seq_len)
  // max_distance = 2 → offsets beyond ±2 clamp to ±2.
  // bias[h, 0, 5] should equal bias[h, 0, 4] (both clamp to +2).
  // bias[h, 0, 2] should equal bias[h, 0, 3] (both clamp to +2).
  let v05 = bias[0 * seq_len * seq_len + 0 * seq_len + 5]
  let v04 = bias[0 * seq_len * seq_len + 0 * seq_len + 4]
  if v05 != v04 {
    fail("bias[0, 0, 5] = \{v05} != bias[0, 0, 4] = \{v04} (clamp failed)")
  }
  let v02 = bias[0 * seq_len * seq_len + 0 * seq_len + 2]
  let v03 = bias[0 * seq_len * seq_len + 0 * seq_len + 3]
  if v02 != v03 {
    fail("bias[0, 0, 2] = \{v02} != bias[0, 0, 3] = \{v03} (clamp failed)")
  }
}

///|
test "t5_compute_bias: deterministic + different seeds give different outputs" {
  let seq_len = 4
  let rp_a = T5RelativePosition::new(2, 3, 7UL)
  let rp_b = T5RelativePosition::new(2, 3, 7UL)
  let rp_c = T5RelativePosition::new(2, 3, 8UL)
  let bias_a = t5_compute_bias(rp_a, seq_len)
  let bias_b = t5_compute_bias(rp_b, seq_len)
  let bias_c = t5_compute_bias(rp_c, seq_len)
  for i in 0..<bias_a.length() {
    if bias_a[i] != bias_b[i] {
      fail("same-seed not deterministic at \{i}")
    }
    if bias_a[i] == bias_c[i] {
      fail("different seeds produced identical bias at \{i}")
    }
  }
}

///|
test "t5_backward: d_weight accumulates per bucket correctly" {
  // rp with 1 head, max_distance=2, n_buckets=5.
  let rp = T5RelativePosition::new(1, 2, 1UL)
  let seq_len = 3
  // d_bias: set d_bias[0, 0, 1] = 1 (offset=+1, bucket=+1+2=3),
  //                       [0, 1, 0] = 2 (offset=-1, bucket=-1+2=1),
  //                       [0, 2, 0] = 3 (offset=-2, bucket=0).
  let d_bias : Array[Float] = Array::make(seq_len * seq_len, 0.0F)
  d_bias[0 * seq_len + 1] = 1.0F
  d_bias[1 * seq_len + 0] = 2.0F
  d_bias[2 * seq_len + 0] = 3.0F
  let d_weight = t5_backward(rp, d_bias, seq_len)
  // bucket 0 (offset=-2): 3.0
  // bucket 1 (offset=-1): 2.0
  // bucket 2 (offset= 0): 0.0
  // bucket 3 (offset=+1): 1.0
  // bucket 4 (offset=+2): 0.0
  if !t5_eps(d_weight[0], 3.0F, 0.0001F) {
    fail("d_weight[0] = \{d_weight[0]}, expected 3.0")
  }
  if !t5_eps(d_weight[1], 2.0F, 0.0001F) {
    fail("d_weight[1] = \{d_weight[1]}, expected 2.0")
  }
  if !t5_eps(d_weight[2], 0.0F, 0.0001F) {
    fail("d_weight[2] = \{d_weight[2]}, expected 0.0")
  }
  if !t5_eps(d_weight[3], 1.0F, 0.0001F) {
    fail("d_weight[3] = \{d_weight[3]}, expected 1.0")
  }
  if !t5_eps(d_weight[4], 0.0F, 0.0001F) {
    fail("d_weight[4] = \{d_weight[4]}, expected 0.0")
  }
}

///|
test "t5 backward + forward: zero d_bias → zero d_weight" {
  let rp = T5RelativePosition::new(2, 4, 1UL)
  let seq_len = 5
  let zero : Array[Float] = Array::make(rp.num_heads * seq_len * seq_len, 0.0F)
  let d_weight = t5_backward(rp, zero, seq_len)
  for i in 0..<d_weight.length() {
    if d_weight[i] != 0.0F {
      fail("d_weight[\{i}] = \{d_weight[i]} for zero d_bias")
    }
  }
}