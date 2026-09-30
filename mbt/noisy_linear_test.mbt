// noisy_linear_test.mbt — tests for v0.43.0 NoisyLinear layer.

///|
test "noisy_linear_init_shapes_and_values" {
  let rng = Xoshiro::new(42UL)
  let p = NoisyLinearParam::init(4, 3, rng)
  if p.in_features != 4 {
    fail("in_features = \{p.in_features}, expected 4")
  }
  if p.out_features != 3 {
    fail("out_features = \{p.out_features}, expected 3")
  }
  if p.weight_mu.length() != 12 {
    fail("weight_mu.length() = \{p.weight_mu.length()}, expected 12")
  }
  if p.weight_sigma.length() != 12 {
    fail("weight_sigma.length() mismatch")
  }
  if p.bias_mu.length() != 3 {
    fail("bias_mu.length() = \{p.bias_mu.length()}, expected 3")
  }
  if p.bias_sigma.length() != 3 {
    fail("bias_sigma.length() mismatch")
  }
  // σ inits must all be the paper's default 0.017.
  for k in 0..<p.weight_sigma.length() {
    if p.weight_sigma[k] != 0.017F {
      fail("weight_sigma[\{k}] = \{p.weight_sigma[k]}, expected 0.017")
    }
  }
  for k in 0..<p.bias_sigma.length() {
    if p.bias_sigma[k] != 0.017F {
      fail("bias_sigma[\{k}] = \{p.bias_sigma[k]}, expected 0.017")
    }
  }
  // Bias μ must all be zero.
  for k in 0..<p.bias_mu.length() {
    if p.bias_mu[k] != 0.0F {
      fail("bias_mu[\{k}] = \{p.bias_mu[k]}, expected 0")
    }
  }
}

///|
test "noisy_linear_init_weights_bounded" {
  // μ weights must lie in [-1/sqrt(in), +1/sqrt(in)].
  let rng = Xoshiro::new(7UL)
  let p = NoisyLinearParam::init(64, 32, rng)
  let bound = 1.0F / 8.0F  // 1/sqrt(64) = 0.125
  for k in 0..<p.weight_mu.length() {
    if p.weight_mu[k] < -bound - 1.0e-6F {
      fail("weight_mu[\{k}] = \{p.weight_mu[k]} < -\{bound}")
    }
    if p.weight_mu[k] > bound + 1.0e-6F {
      fail("weight_mu[\{k}] = \{p.weight_mu[k]} > +\{bound}")
    }
  }
}

///|
test "noisy_linear_param_count" {
  let rng = Xoshiro::new(0UL)
  // 4→3 layer: 12 weight + 3 bias params, ×2 for μ+σ = 30.
  let p = NoisyLinearParam::init(4, 3, rng)
  if noisy_linear_param_count(p) != 30 {
    fail("param_count = \{noisy_linear_param_count(p)}, expected 30")
  }
}

///|
test "noisy_linear_forward_eval_matches_linear" {
  // With σ = 0 and bias σ = 0, the noisy eval forward should match
  // a regular Linear forward exactly (μ weights only).
  let in_f = 3
  let out_f = 2
  let w_mu : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let w_sig : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let b_mu : Array[Float] = [0.7F, 0.8F]
  let b_sig : Array[Float] = Array::make(out_f, 0.0F)
  let p = NoisyLinearParam::new(w_mu, w_sig, b_mu, b_sig, in_f, out_f)
  let input : Array[Float] = [1.0F, 2.0F, 3.0F]
  let out = noisy_linear_forward_eval(input, 1, p)
  // row 0: 0.1*1 + 0.2*2 + 0.3*3 + 0.7 = 0.1+0.4+0.9+0.7 = 2.1
  // row 1: 0.4*1 + 0.5*2 + 0.6*3 + 0.8 = 0.4+1.0+1.8+0.8 = 4.0
  if (out[0] - 2.1F).abs() > 1.0e-5F {
    fail("out[0] = \{out[0]}, expected ~2.1")
  }
  if (out[1] - 4.0F).abs() > 1.0e-5F {
    fail("out[1] = \{out[1]}, expected ~4.0")
  }
}

///|
test "noisy_linear_forward_output_shape" {
  let rng = Xoshiro::new(1UL)
  let p = NoisyLinearParam::init(5, 4, rng)
  let n = 3
  let input : Array[Float] = Array::make(n * 5, 1.0F)
  let out = noisy_linear_forward(input, n, p, rng)
  if out.length() != n * 4 {
    fail("out.length() = \{out.length()}, expected \{n * 4}")
  }
}

///|
test "noisy_linear_forward_zero_input_gives_bias_only" {
  // x = 0 ⇒ y = b. With sigma=0 this is exactly b_mu + b_sigma·ε_j.
  let in_f = 4
  let out_f = 3
  let w_mu : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let w_sig : Array[Float] = Array::make(out_f * in_f, 0.0F)
  let b_mu : Array[Float] = [1.0F, 2.0F, 3.0F]
  let b_sig : Array[Float] = Array::make(out_f, 0.0F)
  let p = NoisyLinearParam::new(w_mu, w_sig, b_mu, b_sig, in_f, out_f)
  let rng = Xoshiro::new(99UL)
  let input : Array[Float] = Array::make(in_f, 0.0F)
  let out = noisy_linear_forward(input, 1, p, rng)
  if (out[0] - 1.0F).abs() > 1.0e-5F {
    fail("out[0] = \{out[0]}, expected 1.0")
  }
  if (out[1] - 2.0F).abs() > 1.0e-5F {
    fail("out[1] = \{out[1]}, expected 2.0")
  }
  if (out[2] - 3.0F).abs() > 1.0e-5F {
    fail("out[2] = \{out[2]}, expected 3.0")
  }
}

///|
test "noisy_linear_forward_noise_affects_output" {
  // With non-zero σ, two different RNGs should produce different outputs.
  let rng1 = Xoshiro::new(1UL)
  let p1 = NoisyLinearParam::init(8, 4, rng1)
  let input : Array[Float] = Array::make(8, 0.5F)
  let out1 = noisy_linear_forward(input, 1, p1, rng1)
  let rng2 = Xoshiro::new(2UL)
  let p2 = NoisyLinearParam::init(8, 4, rng2)
  let out2 = noisy_linear_forward(input, 1, p2, rng2)
  // p1/p2 are independent random init, but RNG also differs.
  // At least one output should differ.
  let mut any_diff = false
  for k in 0..<out1.length() {
    if (out1[k] - out2[k]).abs() > 1.0e-6F {
      any_diff = true
    }
  }
  if !any_diff {
    fail("two different RNGs produced identical outputs (no noise effect)")
  }
}

///|
test "noisy_linear_forward_same_seed_deterministic" {
  // Same seed → same forward output (bit-exact reproducibility).
  let in_f = 4
  let out_f = 3
  let rng_init_a = Xoshiro::new(5UL)
  let p_a = NoisyLinearParam::init(in_f, out_f, rng_init_a)
  let rng_init_b = Xoshiro::new(5UL)
  let p_b = NoisyLinearParam::init(in_f, out_f, rng_init_b)
  // Both rngs reseeded before forward pass.
  let rng_forward_a = Xoshiro::new(1234UL)
  let rng_forward_b = Xoshiro::new(1234UL)
  let input : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F]
  let out_a = noisy_linear_forward(input, 1, p_a, rng_forward_a)
  let out_b = noisy_linear_forward(input, 1, p_b, rng_forward_b)
  for k in 0..<out_a.length() {
    if out_a[k] != out_b[k] {
      fail("out differs at \{k}: \{out_a[k]} vs \{out_b[k]}")
    }
  }
}

///|
test "f_sign_sqrt_basic" {
  // f(0) = 0, f(+1) = +1, f(-1) = -1, f(+4) = +2, f(-9) = -3.
  if f_sign_sqrt(0.0F) != 0.0F {
    fail("f(0) = \{f_sign_sqrt(0.0F)}, expected 0")
  }
  if (f_sign_sqrt(1.0F) - 1.0F).abs() > 1.0e-5F {
    fail("f(1) = \{f_sign_sqrt(1.0F)}, expected 1")
  }
  if (f_sign_sqrt(-1.0F) - -1.0F).abs() > 1.0e-5F {
    fail("f(-1) = \{f_sign_sqrt(-1.0F)}, expected -1")
  }
  if (f_sign_sqrt(4.0F) - 2.0F).abs() > 1.0e-5F {
    fail("f(4) = \{f_sign_sqrt(4.0F)}, expected 2")
  }
  if (f_sign_sqrt(-9.0F) - -3.0F).abs() > 1.0e-5F {
    fail("f(-9) = \{f_sign_sqrt(-9.0F)}, expected -3")
  }
}

///|
test "sample_gaussians_count_and_bounded" {
  // n samples must come out; 99% of |z| ≤ 3 (3σ) for a healthy RNG.
  let rng = Xoshiro::new(7UL)
  let n = 1000
  let z = sample_gaussians(n, rng)
  if z.length() != n {
    fail("z.length() = \{z.length()}, expected \{n}")
  }
  let mut out_of_range = 0
  for k in 0..<n {
    if z[k] < -3.5F || z[k] > 3.5F {
      out_of_range = out_of_range + 1
    }
  }
  // Allow up to 1% outliers (3.5σ tail).
  if out_of_range > 10 {
    fail("\{out_of_range}/\{n} samples outside ±3.5σ")
  }
}