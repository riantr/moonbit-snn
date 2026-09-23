// Tests for LayerScale.

///|
fn ls_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "LayerScale::new: default init value = 1e-4 + shape" {
  let ls = LayerScale::new(8)
  assert_eq(ls.dim, 8)
  assert_eq(ls.gamma.length(), 8)
  for i in 0..<8 {
    if !ls_eps(ls.gamma[i], 0.0001F, 0.00001F) {
      fail("gamma[\{i}] = \{ls.gamma[i]}, expected 1e-4")
    }
  }
}

///|
test "LayerScale::new: custom init value" {
  let ls = LayerScale::new(4, init_value=0.001F)
  for i in 0..<4 {
    if !ls_eps(ls.gamma[i], 0.001F, 0.0001F) {
      fail("gamma[\{i}] = \{ls.gamma[i]}, expected 1e-3")
    }
  }
}

///|
test "layer_scale_forward: out[i] = gamma[i] * sublayer[i] (per-channel)" {
  let ls = LayerScale::new(2, init_value=0.5F)
  let sublayer : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]  // batch=2, dim=2
  let out = layer_scale_forward(sublayer, ls)
  assert_eq(out.length(), 4)
  // gamma[0] = 0.5, gamma[1] = 0.5 → out = [0.5, 1.0, 1.5, 2.0]
  let expected : Array[Float] = [0.5F, 1.0F, 1.5F, 2.0F]
  for i in 0..<4 {
    if !ls_eps(out[i], expected[i], 0.0001F) {
      fail("out[\{i}] = \{out[i]}, expected \{expected[i]}")
    }
  }
}

///|
test "layer_scale_backward: d_sublayer = gamma * d_output" {
  let ls = LayerScale::new(2, init_value=0.5F)
  let sublayer : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (d_sublayer, _) = layer_scale_backward(sublayer, d_output, ls)
  // d_sublayer[i] = gamma[i % 2] * d_output[i]
  for i in 0..<4 {
    let g = if i % 2 == 0 { 0.5F } else { 0.5F }
    let expected = g * d_output[i]
    if !ls_eps(d_sublayer[i], expected, 0.0001F) {
      fail("d_sublayer[\{i}] = \{d_sublayer[i]}, expected \{expected}")
    }
  }
}

///|
test "layer_scale_backward: d_gamma[i] = sum d_output[*, i] * sublayer[*, i]" {
  let ls = LayerScale::new(2, init_value=0.1F)
  let sublayer : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let d_output : Array[Float] = [0.5F, 0.6F, 0.7F, 0.8F]
  let (_, d_gamma) = layer_scale_backward(sublayer, d_output, ls)
  // gamma[0] = sum over b in 0,1: d_output[b*2+0] * sublayer[b*2+0]
  //         = 0.5*1.0 + 0.7*3.0 = 0.5 + 2.1 = 2.6
  // gamma[1] = 0.6*2.0 + 0.8*4.0 = 1.2 + 3.2 = 4.4
  if !ls_eps(d_gamma[0], 2.6F, 0.0001F) {
    fail("d_gamma[0] = \{d_gamma[0]}, expected 2.6")
  }
  if !ls_eps(d_gamma[1], 4.4F, 0.0001F) {
    fail("d_gamma[1] = \{d_gamma[1]}, expected 4.4")
  }
}

///|
test "layer_scale_backward: zero d_output → zero d_sublayer + zero d_gamma" {
  let ls = LayerScale::new(2)
  let sublayer : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let zero : Array[Float] = Array::make(4, 0.0F)
  let (d_sublayer, d_gamma) = layer_scale_backward(sublayer, zero, ls)
  for i in 0..<4 {
    if d_sublayer[i] != 0.0F {
      fail("d_sublayer[\{i}] = \{d_sublayer[i]} for zero d_output")
    }
  }
  for i in 0..<2 {
    if d_gamma[i] != 0.0F {
      fail("d_gamma[\{i}] = \{d_gamma[i]} for zero d_output")
    }
  }
}