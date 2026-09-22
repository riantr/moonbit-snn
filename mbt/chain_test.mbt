// chain_test.mbt — Generic Chain / Sequential tests (v0.19.0).

///|
test "chain / empty chain returns input unchanged with no caches" {
  let input : Array[Float] = [1.0F, 2.0F, 3.0F]
  let (out, caches, _, _, _, _) = chain_forward([], input, 1, 1, 1, 3)
  if out.length() != input.length() {
    fail("empty chain output length mismatch")
  }
  if caches.length() != 0 {
    fail("empty chain should produce no caches")
  }
}

///|
test "chain / MLP: Linear -> ReLU -> Linear forward" {
  // 4 -> 3 (ReLU) -> 2. Batch N = 2.
  let w1 : Array[Float] = [
    1.0F, 0.0F, -1.0F, 0.5F, // out[0] = x[0] - x[2] + 0.5*x[3]
    0.0F, 1.0F, 0.0F, -1.0F, // out[1] = x[1] - x[3]
    0.5F, 0.5F, 0.5F, 0.5F, // out[2] = 0.5 * sum
  ]
  let b1 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let p1 = LinearParam::new(w1, b1, 4, 3)
  let w2 : Array[Float] = [1.0F, -1.0F, 0.0F, 1.0F, 0.0F, -1.0F]
  let b2 : Array[Float] = [0.0F, 0.0F]
  let p2 = LinearParam::new(w2, b2, 3, 2)
  let layers : Array[Layer] = [
    Layer::linear(p1), Layer::relu(), Layer::linear(p2),
  ]
  let input : Array[Float] = [
    1.0F, 2.0F, 3.0F, 4.0F,  // sample 0
    0.5F, -1.0F, 0.5F, 0.0F, // sample 1
  ]
  let (out, caches, n_out, c_out, h_out, w_out) = chain_forward(
    layers, input, 2, 4, 1, 1,
  )
  if n_out != 2 || c_out != 2 || h_out != 1 || w_out != 1 {
    fail("output shape should be (2,2,1,1), got (\{n_out},\{c_out},\{h_out},\{w_out})")
  }
  if out.length() != 4 { fail("output should have 4 elements") }
  if caches.length() != 3 { fail("should have 3 caches") }
  // spot-check sample 0:
  // p1.forward([1,2,3,4]) = [1 - 3 + 2, 2 - 4, 0.5*(1+2+3+4)] = [0, -2, 5]
  // ReLU -> [0, 0, 5]
  // p2.forward([0,0,5]) = [0 - 0, 0 - 5] = [0, -5]
  if (out[0] - 0.0F).abs() > 0.0001F { fail("out[0] expected 0, got \{out[0]}") }
  if (out[1] - -5.0F).abs() > 0.0001F { fail("out[1] expected -5, got \{out[1]}") }
}

///|
test "chain / MLP backward: shape and d_input sanity" {
  // Same 4 -> 3 (ReLU) -> 2 topology. Verify backward returns d_input
  // matching the input shape and the right number of grads.
  let w1 : Array[Float] = Array::make(3 * 4, 0.1F)
  let b1 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let p1 = LinearParam::new(w1, b1, 4, 3)
  let w2 : Array[Float] = Array::make(2 * 3, 0.1F)
  let b2 : Array[Float] = [0.0F, 0.0F]
  let p2 = LinearParam::new(w2, b2, 3, 2)
  let layers : Array[Layer] = [
    Layer::linear(p1), Layer::relu(), Layer::linear(p2),
  ]
  let input : Array[Float] = Array::make(2 * 4, 0.5F)
  let (_out, caches, _n, _c, _h, _w) = chain_forward(
    layers, input, 2, 4, 1, 1,
  )
  let d_output : Array[Float] = Array::make(2 * 2, 1.0F)
  let (d_input, grads) = chain_backward(layers, caches, d_output, 2, 2, 1, 1)
  if d_input.length() != input.length() {
    fail("d_input length should be \{input.length()}, got \{d_input.length()}")
  }
  if grads.length() != 3 { fail("should have 3 grads") }
  // Check grad types.
  match grads[0] {
    LayerGrad::Linear(_, _) => ()
    _ => fail("grads[0] should be Linear")
  }
  match grads[1] {
    LayerGrad::Empty => ()
    _ => fail("grads[1] (ReLU) should be Empty")
  }
  match grads[2] {
    LayerGrad::Linear(_, _) => ()
    _ => fail("grads[2] should be Linear")
  }
}

///|
test "chain / CNN: Conv2d -> ReLU -> MaxPool2d -> Flatten -> Linear" {
  // 1x1x4x4 input -> Conv(2x1x3x3, pad=1, stride=1) -> 1x2x4x4
  // -> ReLU (shape unchanged) -> MaxPool2d(2x2, stride=2) -> 1x2x2x2
  // -> Flatten -> 1x8x1x1 -> Linear(2) -> 1x2x1x1.
  let conv = Conv2dParam::new(
    [0.1F, 0.0F, 0.1F, 0.0F, 0.1F, 0.0F, 0.1F, 0.0F, 0.1F,
     0.0F, 0.1F, 0.0F, 0.1F, 0.0F, 0.1F, 0.0F, 0.1F, 0.0F],
    [0.0F, 0.0F],
    2, 1, 3, 3, stride=1, pad=1,
  )
  let pool = MaxPool2dParam::new(2, 2, stride=2, pad=0)
  let linear = LinearParam::new(Array::make(2 * 8, 0.1F), [0.0F, 0.0F], 8, 2)
  let layers : Array[Layer] = [
    Layer::conv2d(conv), Layer::relu(), Layer::max_pool2d(pool),
    Layer::flatten(), Layer::linear(linear),
  ]
  let input : Array[Float] = Array::make(1 * 1 * 4 * 4, 0.5F)
  let (out, caches, n_out, c_out, h_out, w_out) = chain_forward(
    layers, input, 1, 1, 4, 4,
  )
  if n_out != 1 || c_out != 2 || h_out != 1 || w_out != 1 {
    fail("output shape should be (1,2,1,1), got (\{n_out},\{c_out},\{h_out},\{w_out})")
  }
  if out.length() != 2 { fail("output length should be 2") }
  if caches.length() != 5 { fail("should have 5 caches") }
}

///|
test "chain / gradient sanity: zero d_output yields zero d_input + zero grads" {
  let w1 : Array[Float] = Array::make(3 * 2, 0.1F)
  let b1 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let p1 = LinearParam::new(w1, b1, 2, 3)
  let layers : Array[Layer] = [Layer::linear(p1), Layer::relu()]
  let input : Array[Float] = [1.0F, -1.0F, 2.0F, -2.0F]
  let (_out, caches, _n, c_out, _h, _w) = chain_forward(
    layers, input, 2, 2, 1, 1,
  )
  let d_output : Array[Float] = Array::make(2 * c_out, 0.0F)
  let (d_input, grads) = chain_backward(layers, caches, d_output, 2, c_out, 1, 1)
  for i in 0..<d_input.length() {
    if d_input[i] != 0.0F { fail("d_input[\{i}] should be 0") }
  }
  match grads[0] {
    LayerGrad::Linear(d_w, d_b) => {
      for i in 0..<d_w.length() {
        if d_w[i] != 0.0F { fail("d_w[\{i}] should be 0") }
      }
      for i in 0..<d_b.length() {
        if d_b[i] != 0.0F { fail("d_b[\{i}] should be 0") }
      }
    }
    _ => fail("grads[0] should be Linear")
  }
  match grads[1] {
    LayerGrad::Empty => ()
    _ => fail("grads[1] (ReLU) should be Empty")
  }
}

///|
test "chain / backward gradient check vs numerical (Linear + ReLU)" {
  // Numerical gradient of sum(output) w.r.t. input via central diff.
  let w1 : Array[Float] = [0.3F, -0.2F, 0.1F, 0.5F, -0.4F, 0.2F]
  let b1 : Array[Float] = [0.0F, 0.0F]
  let p1 = LinearParam::new(w1, b1, 3, 2)
  let layers : Array[Layer] = [Layer::linear(p1), Layer::relu()]
  let n = 2
  // Inputs chosen so all post-Linear pre-ReLU activations are well away
  // from 0 (ReLU is non-smooth at 0, gradient checks fail there).
  let input : Array[Float] = [1.0F, 0.5F, -0.5F, -1.0F, 1.5F, -2.0F]
  let (out, caches, _, c_out, _, _) = chain_forward(
    layers, input, n, 3, 1, 1,
  )
  let d_output : Array[Float] = Array::make(n * c_out, 1.0F)
  let (d_input, _grads) = chain_backward(layers, caches, d_output, n, c_out, 1, 1)
  let eps = 0.001F
  for i in 0..<input.length() {
    let input_plus : Array[Float] = Array::make(input.length(), 0.0F)
    let input_minus : Array[Float] = Array::make(input.length(), 0.0F)
    for k in 0..<input.length() {
      input_plus[k] = input[k]
      input_minus[k] = input[k]
    }
    input_plus[i] = input[i] + eps
    input_minus[i] = input[i] - eps
    let (out_plus, _, _, _, _, _) = chain_forward(
      layers, input_plus, n, 3, 1, 1,
    )
    let (out_minus, _, _, _, _, _) = chain_forward(
      layers, input_minus, n, 3, 1, 1,
    )
    let mut sum_plus = 0.0F
    let mut sum_minus = 0.0F
    for k in 0..<out_plus.length() {
      sum_plus = sum_plus + out_plus[k]
      sum_minus = sum_minus + out_minus[k]
    }
    let num_grad = (sum_plus - sum_minus) / (2.0F * eps)
    if (d_input[i] - num_grad).abs() > 0.01F {
      fail("d_input[\{i}] analytical \{d_input[i]} vs numerical \{num_grad}")
    }
  }
}

///|
test "chain / BN included in MLP forward + backward" {
  // Linear(3) -> BatchNorm2d -> ReLU -> Linear(2). Verify chain
  // accepts normalisation layers and the backward returns d_gamma /
  // d_beta for the BN slot.
  let w1 : Array[Float] = Array::make(3 * 2, 0.2F)
  let b1 : Array[Float] = [0.0F, 0.0F, 0.0F]
  let p1 = LinearParam::new(w1, b1, 2, 3)
  let bn = BatchNorm2d::new(3)
  let w2 : Array[Float] = Array::make(2 * 3, 0.1F)
  let b2 : Array[Float] = [0.0F, 0.0F]
  let p2 = LinearParam::new(w2, b2, 3, 2)
  let layers : Array[Layer] = [
    Layer::linear(p1), Layer::batch_norm2d(bn), Layer::relu(),
    Layer::linear(p2),
  ]
  let input : Array[Float] = Array::make(2 * 2, 0.5F)
  let (_out, caches, _, c_out, _, _) = chain_forward(
    layers, input, 2, 2, 1, 1,
  )
  let d_output : Array[Float] = Array::make(2 * c_out, 1.0F)
  let (d_input, grads) = chain_backward(layers, caches, d_output, 2, c_out, 1, 1)
  if d_input.length() != input.length() {
    fail("d_input length should match input length")
  }
  if grads.length() != 4 { fail("should have 4 grads") }
  // Layer 0: Linear
  match grads[0] {
    LayerGrad::Linear(_, _) => ()
    _ => fail("grads[0] should be Linear")
  }
  // Layer 1: BatchNorm2d (expect d_gamma + d_beta)
  match grads[1] {
    LayerGrad::BatchNorm2d(d_g, d_b) => {
      if d_g.length() != 3 { fail("d_gamma should have length 3") }
      if d_b.length() != 3 { fail("d_beta should have length 3") }
    }
    _ => fail("grads[1] should be BatchNorm2d")
  }
  // Layer 2: ReLU (Empty)
  match grads[2] {
    LayerGrad::Empty => ()
    _ => fail("grads[2] (ReLU) should be Empty")
  }
  // Layer 3: Linear
  match grads[3] {
    LayerGrad::Linear(_, _) => ()
    _ => fail("grads[3] should be Linear")
  }
}