// Tests for SRNN demo (CSNN-style IF + surrogate + BPTT).

///|
fn sr_eps(a : Float, b : Float, eps : Float) -> Bool {
  let d = if a > b { a - b } else { b - a }
  d <= eps
}

///|
test "srnn_poisson_encode: shape + spike probability" {
  let input : Array[Float] = [0.5F, 0.2F]
  let n_steps = 100
  let dt = 1.0F
  let max_rate = 0.5F  // Hz (effective dt = 1ms), so input=0.5 gives p=0.25
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let spikes = srnn_poisson_encode(input, n_steps, dt, max_rate, rng)
  assert_eq(spikes.length(), n_steps * 2)
  // Each cell has p = min(1, input[i] * max_rate * dt).
  // For input[0]=0.5, max_rate=0.5: p = 0.25 → ~25 spikes in 100 timesteps.
  let mut c0 = 0
  for t in 0..<n_steps {
    if spikes[t * 2 + 0] > 0.5F {
      c0 = c0 + 1
    }
  }
  if c0 < 15 || c0 > 40 {
    fail("spike count for input[0]=0.5: got \{c0}/100, expected ~25")
  }
}

///|
test "srnn_dataset_build: shape + label distribution" {
  let (images, labels) = srnn_dataset_build(1UL)
  assert_eq(images.length(), 16 * 2)
  assert_eq(labels.length(), 16)
  let mut n_c0 = 0
  let mut n_c1 = 0
  for i in 0..<labels.length() {
    if labels[i] == 0 { n_c0 = n_c0 + 1 }
    if labels[i] == 1 { n_c1 = n_c1 + 1 }
  }
  assert_eq(n_c0, 8)
  assert_eq(n_c1, 8)
}

///|
test "srnn_forward: output shape + finite loss" {
  let s = SRNN::new(2, 4, 2, 42UL)
  let n_steps = 5
  let beta = 1.0F
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let input : Array[Float] = [0.5F, 0.3F]
  let x = srnn_poisson_encode(input, n_steps, 1.0F, 50.0F, rng)
  let (loss, cache) = srnn_forward(x, n_steps, 0, s, beta)
  if !(loss > 0.0F && loss < 10.0F) {
    fail("loss = \{loss}, expected in (0, 10)")
  }
  assert_eq(cache.v.length(), n_steps * 4)
  assert_eq(cache.spikes.length(), n_steps * 4)
  assert_eq(cache.y.length(), n_steps * 2)
  assert_eq(cache.probs.length(), 2)
  let mut sum_p = 0.0F
  for i in 0..<2 {
    sum_p = sum_p + cache.probs[i]
  }
  if !(sum_p > 0.99F && sum_p < 1.01F) {
    fail("softmax doesn't sum to 1: \{sum_p}")
  }
}

///|
test "srnn_backward: grad shapes + zero loss �?zero grads" {
  let s = SRNN::new(2, 4, 2, 42UL)
  let n_steps = 5
  let beta = 1.0F
  let rng = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let input : Array[Float] = [0.5F, 0.3F]
  let x = srnn_poisson_encode(input, n_steps, 1.0F, 50.0F, rng)
  let (_loss, cache) = srnn_forward(x, n_steps, 0, s, beta)
  let (d_w_in, d_w_rec, d_w_out, d_b_in, d_b_out) = srnn_backward(cache, s)
  assert_eq(d_w_in.length(), 2 * 4)
  assert_eq(d_w_rec.length(), 4 * 4)
  assert_eq(d_w_out.length(), 4 * 2)
  assert_eq(d_b_in.length(), 4)
  assert_eq(d_b_out.length(), 2)
  // At least one grad is non-zero (otherwise we'd have a degenerate net).
  let mut non_zero = 0
  for i in 0..<d_w_in.length() {
    if d_w_in[i] != 0.0F { non_zero = non_zero + 1 }
  }
  if non_zero == 0 {
    fail("all d_w_in zero, gradient not flowing")
  }
}

///|
test "srnn_train_step: loss decreases on a single sample" {
  let (images, labels) = srnn_dataset_build(1UL)
  let srnn = SRNN::new(2, 8, 2, 42UL)
  let n_steps = 8
  let beta = 1.5F
  let rng = Xoshiro::from_state(7UL, 8UL, 9UL, 10UL)
  let img : Array[Float] = [images[0], images[1]]
  let (initial_loss, _) = srnn_forward(
    srnn_poisson_encode(img, n_steps, 1.0F, 50.0F, rng), n_steps, labels[0], srnn, beta,
  )
  let lr = 0.01F
  // Train K=8 steps on the same sample (rng state advances naturally).
  let mut rng_step = Xoshiro::from_state(7UL, 8UL, 9UL, 10UL)
  for _step in 0..<8 {
    let one_img : Array[Float] = [images[0], images[1]]
    ignore(srnn_train_step(one_img, labels[0], n_steps, lr, beta, srnn, rng_step))
    // Advance rng state via next_f32 (each call to srnn_train_step
    // consumes some random numbers internally, so re-derive a fresh
    // rng for deterministic encoding).
    rng_step = Xoshiro::from_state(7UL, 8UL, 9UL, 10UL)
    ignore(next_f32(rng_step))
  }
  // Evaluate loss on the same input deterministically.
  let eval_rng = Xoshiro::from_state(7UL, 8UL, 9UL, 10UL)
  let x_eval = srnn_poisson_encode(img, n_steps, 1.0F, 50.0F, eval_rng)
  let (final_loss, _) = srnn_forward(x_eval, n_steps, labels[0], srnn, beta)
  if !(final_loss < initial_loss) {
    fail("loss did not decrease: initial = \{initial_loss}, final = \{final_loss}")
  }
}

///|
test "srnn_eval_loss: deterministic + finite" {
  let (images, labels) = srnn_dataset_build(1UL)
  let s = SRNN::new(2, 4, 2, 42UL)
  let n_steps = 5
  let beta = 1.0F
  let loss = srnn_eval_loss(images, labels, n_steps, beta, s, 99UL)
  if !(loss > 0.0F && loss < 10.0F) {
    fail("eval_loss = \{loss}, expected in (0, 10)")
  }
}