// bottles_test.mbt — Pre-made Bottle architecture tests (v0.19.1 + 2 + 3).

///|
test "MLP::new / sizes matches layer count" {
  let m = MLP::new([4, 8, 3], 42UL)
  // 4->8 (Linear + ReLU) + 8->3 (Linear) = 3 layers.
  if m.layers.length() != 3 { fail("should have 3 layers, got \{m.layers.length()}") }
}

///|
test "MLP::new / reproducible with same seed" {
  let m1 = MLP::new([3, 4], 1234UL)
  let m2 = MLP::new([3, 4], 1234UL)
  match (m1.layers[0], m2.layers[0]) {
    (Linear(p1), Linear(p2)) => {
      if p1.weight.length() != p2.weight.length() { fail("weight length mismatch") }
      for i in 0..<p1.weight.length() {
        if p1.weight[i] != p2.weight[i] { fail("weight[\{i}] not reproducible") }
      }
    }
    _ => fail("expected Linear params")
  }
}

///|
test "MLP::new / different seeds give different weights" {
  let m1 = MLP::new([3, 4], 1UL)
  let m2 = MLP::new([3, 4], 2UL)
  match (m1.layers[0], m2.layers[0]) {
    (Linear(p1), Linear(p2)) => {
      let mut any_diff = false
      for i in 0..<p1.weight.length() {
        if p1.weight[i] != p2.weight[i] { any_diff = true; break }
      }
      if not(any_diff) { fail("different seeds should produce different weights") }
    }
    _ => fail("expected Linear params")
  }
}

///|
test "MLP::num_params / counts weights + biases for all Linear layers" {
  // 4 -> 8 -> 3: weights 4*8 + 8*3 = 56, biases 8 + 3 = 11, total 67.
  let m = MLP::new([4, 8, 3], 42UL)
  if m.num_params() != 67 {
    fail("num_params should be 67, got \{m.num_params()}")
  }
}

///|
test "MLP / forward output shape = (n, sizes[last])" {
  let m = MLP::new([3, 5, 2], 42UL)
  let n = 4
  let input : Array[Float] = Array::make(n * 3, 0.5F)
  let (out, _caches, total) = mlp_forward(m, input, n)
  if total != n * 2 {
    fail("output should have n*2 elements, got \{total}")
  }
  if out.length() != n * 2 { fail("out length mismatch") }
}

///|
test "MLP / backward d_input shape matches input" {
  let m = MLP::new([3, 5, 2], 42UL)
  let n = 4
  let input : Array[Float] = Array::make(n * 3, 0.5F)
  let (_out, caches, _total) = mlp_forward(m, input, n)
  let d_output : Array[Float] = Array::make(n * 2, 1.0F)
  let (d_input, grads) = mlp_backward(m, caches, d_output, n)
  if d_input.length() != n * 3 { fail("d_input length should be n*3") }
  // grads should include 2 Linear grads + 1 Empty (ReLU).
  let mut linear_count = 0
  let mut empty_count = 0
  for g in grads {
    match g {
      Linear(_) => linear_count = linear_count + 1
      Empty => empty_count = empty_count + 1
      _ => fail("unexpected grad variant")
    }
  }
  if linear_count != 2 { fail("should have 2 Linear grads, got \{linear_count}") }
  if empty_count != 1 { fail("should have 1 Empty grad (ReLU), got \{empty_count}") }
}

///|
test "SimpleCNN / forward output shape = (n, 10, 1, 1)" {
  let m = SimpleCNN::new(1, 6, 16, 42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 1 * 28 * 28, 0.5F)
  let (out, caches, n_out, c_out, h_out, w_out) = simple_cnn_forward(m, input, n)
  if n_out != n || c_out != 10 || h_out != 1 || w_out != 1 {
    fail("output shape should be (\{n},10,1,1), got (\{n_out},\{c_out},\{h_out},\{w_out})")
  }
  if out.length() != n * 10 { fail("out length should be n*10") }
  if caches.length() != 8 { fail("should have 8 caches (Conv, ReLU, Pool, Conv, ReLU, Pool, Flatten, Linear)") }
}

///|
test "LeNet5 / forward output shape = (n, 10, 1, 1)" {
  let m = LeNet5::new(42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 1 * 32 * 32, 0.5F)
  let (out, caches, n_out, c_out, h_out, w_out) = lenet5_forward(m, input, n)
  if n_out != n || c_out != 10 || h_out != 1 || w_out != 1 {
    fail("output shape should be (\{n},10,1,1), got (\{n_out},\{c_out},\{h_out},\{w_out})")
  }
  if out.length() != n * 10 { fail("out length should be n*10") }
  // C1, ReLU, S2, C3, ReLU, S4, C5, ReLU, Flatten, F6, ReLU, Out = 12 layers.
  if caches.length() != 12 {
    fail("LeNet5 should have 12 caches, got \{caches.length()}")
  }
}

///|
test "LeNet5 / backward d_input shape matches input" {
  let m = LeNet5::new(42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 1 * 32 * 32, 0.5F)
  let (_out, caches, _n_out, _c_out, _h_out, _w_out) = lenet5_forward(m, input, n)
  let d_output : Array[Float] = Array::make(n * 10, 1.0F)
  let (d_input, _grads) = lenet5_backward(m, caches, d_output, n)
  if d_input.length() != n * 1 * 32 * 32 {
    fail("d_input length should match input length")
  }
}

///|
test "LeNet5 / zero d_output gives zero d_input + zero grads" {
  let m = LeNet5::new(42UL)
  let n = 2
  let input : Array[Float] = Array::make(n * 1 * 32 * 32, 0.5F)
  let (_out, caches, _, _, _, _) = lenet5_forward(m, input, n)
  let d_output : Array[Float] = Array::make(n * 10, 0.0F)
  let (d_input, grads) = lenet5_backward(m, caches, d_output, n)
  let mut all_zero = true
  for i in 0..<d_input.length() {
    if d_input[i] != 0.0F { all_zero = false; break }
  }
  if not(all_zero) { fail("d_input should be all zero") }
  for g in grads {
    match g {
      Conv2d(d_w, d_b) => {
        for i in 0..<d_w.length() {
          if d_w[i] != 0.0F { all_zero = false }
        }
        for i in 0..<d_b.length() {
          if d_b[i] != 0.0F { all_zero = false }
        }
      }
      Linear(d_w, d_b) => {
        for i in 0..<d_w.length() {
          if d_w[i] != 0.0F { all_zero = false }
        }
        for i in 0..<d_b.length() {
          if d_b[i] != 0.0F { all_zero = false }
        }
      }
      _ => ()
    }
  }
  if not(all_zero) { fail("all grads should be zero") }
}