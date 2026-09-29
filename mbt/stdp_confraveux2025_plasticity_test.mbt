// stdp_confraveux2025_plasticity_test.mbt — tests for v0.40.1 STDP Confavreux 2025.

///|
test "stdp_confraveux_default_param" {
  let p = StdpConfavreux2025Param::new()
  assert_true(p.tau_pre > 0.0F && p.tau_post > 0.0F)
  assert_true(p.eta > 0.0F)
  assert_true(p.alpha == 0.0F && p.gamma == 0.0F)
}

///|
test "stdp_confraveux_state_zero_init" {
  let s = StdpConfavreux2025State::new(4, 3)
  assert_eq(s.n_pre, 4)
  assert_eq(s.n_post, 3)
  for j in 0..<4 {
    assert_true(s.tpre[j] == 0.0F)
  }
}

///|
test "stdp_confraveux_post_spike_ltp" {
  let p = StdpConfavreux2025Param::new()
  let state = StdpConfavreux2025State::new(1, 1)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [false]
  let w : Array[Float] = [0.5F]
  stdp_confraveux2025_step(state, p, fire_pre, fire_post, w, 0.0F)
  let fire_pre2 : Array[Bool] = [false]
  let fire_post2 : Array[Bool] = [true]
  stdp_confraveux2025_step(state, p, fire_pre2, fire_post2, w, 5.0F)
  assert_true(w[0] > 0.5F)
  assert_true((w[0] - 0.5078F).abs() < 0.005F)
}

///|
test "stdp_confraveux_baseline_alpha_post_only" {
  let p : StdpConfavreux2025Param = {
    eta: 0.1F, alpha: 0.5F, gamma: 0.0F, mu: 1.0F, nu: 1.0F,
    tau_pre: 20.0F, tau_post: 20.0F, w_min: 0.0F, w_max: 1.0F,
  }
  let state = StdpConfavreux2025State::new(1, 1)
  let fire_pre : Array[Bool] = [false]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.0F]
  stdp_confraveux2025_step(state, p, fire_pre, fire_post, w, 0.0F)
  assert_true(w[0] == 0.0F)
}

///|
test "stdp_confraveux_baseline_gamma_post_only" {
  // Per the Julia ref: gamma adds on the post-spike side:
  //   Δ_post = η · (ν · tpre + γ). With tpre=0 and γ=0.5, Δ = 0.1·0.5 = 0.05.
  let p : StdpConfavreux2025Param = {
    eta: 0.1F, alpha: 0.0F, gamma: 0.5F, mu: 1.0F, nu: 1.0F,
    tau_pre: 20.0F, tau_post: 20.0F, w_min: 0.0F, w_max: 1.0F,
  }
  let state = StdpConfavreux2025State::new(1, 1)
  let fire_pre : Array[Bool] = [false]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.0F]
  stdp_confraveux2025_step(state, p, fire_pre, fire_post, w, 0.0F)
  // After post-fire alone: w ≈ 0.05.
  assert_true((w[0] - 0.05F).abs() < 0.01F)
}

///|
test "stdp_confraveux_baseline_alpha_pre_only" {
  // Per the Julia ref: alpha adds on the pre-spike side:
  //   Δ_pre = η · (μ · tpost + α). After a post-fire at t=0 then pre-fire at t=5
  //   with α=0.5, μ=1, η=0.1, tpost ≈ 0.78, Δ ≈ 0.1·(0.78 + 0.5) ≈ 0.128.
  let p : StdpConfavreux2025Param = {
    eta: 0.1F, alpha: 0.5F, gamma: 0.0F, mu: 1.0F, nu: 1.0F,
    tau_pre: 20.0F, tau_post: 20.0F, w_min: 0.0F, w_max: 1.0F,
  }
  let state = StdpConfavreux2025State::new(1, 1)
  let fire_pre : Array[Bool] = [false]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.0F]
  stdp_confraveux2025_step(state, p, fire_pre, fire_post, w, 0.0F)
  let fire_pre2 : Array[Bool] = [true]
  let fire_post2 : Array[Bool] = [false]
  stdp_confraveux2025_step(state, p, fire_pre2, fire_post2, w, 5.0F)
  // Approximately 0.1 * (0.78 + 0.5) = 0.128. Use loose bounds.
  assert_true(w[0] > 0.10F && w[0] < 0.15F)
}

///|
test "stdp_confraveux_clamp_wmax" {
  let p : StdpConfavreux2025Param = {
    eta: 100.0F, alpha: 0.0F, gamma: 0.0F, mu: 1.0F, nu: 1.0F,
    tau_pre: 100.0F, tau_post: 100.0F, w_min: 0.0F, w_max: 0.6F,
  }
  let state = StdpConfavreux2025State::new(1, 1)
  let fire_pre : Array[Bool] = [true]
  let fire_post : Array[Bool] = [true]
  let w : Array[Float] = [0.5F]
  for t in 0..<20 {
    stdp_confraveux2025_step(state, p, fire_pre, fire_post, w, Float::from_int(t))
  }
  assert_true(w[0] <= 0.6F)
}