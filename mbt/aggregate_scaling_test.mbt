// aggregate_scaling_test.mbt — port of `test/network/aggregate_scaling.jl`.
//
// Tests cover:
//   - AggregateScalingParameter::new and AggregateScalingParameter::uniform
//     constructors (defaults + custom values).
//   - AggregateScaling::new initial state (WT[i] = sum of incoming
//     weights, wt/y/mu zero-init).
//   - aggregate_scaling_forward (y decays, y bumps on fire, WT
//     drives toward (1 - y/Y)).
//   - aggregate_scaling_plasticity (periodicity, μ computation, weight
//     rescaling).
//   - Integration with SynapseTarget (mutate vals[] in place).

///|
test "AggregateScalingParameter::new defaults" {
  let y : Array[Float] = Array::make(10, 0.05F)
  let param = AggregateScalingParameter::new(tau_a=100.0F, tau_e=100.0F, y)
  assert_eq(param.tau, 10.0F)
  assert_eq(param.w_min, 0.5F)
  assert_eq(param.w_max, 250.0F)
  assert_eq(param.y.length(), 10)
}

///|
test "AggregateScalingParameter::uniform (N=5, 10Hz)" {
  let param = AggregateScalingParameter::uniform(5, 10.0F)
  assert_eq(param.y.length(), 5)
  // Internal rate = 10 Hz * 0.1 = 1.0F per ms.
  let mut i = 0
  while i < 5 {
    assert_eq(param.y[i], 1.0F)
    i = i + 1
  }
}

///|
test "AggregateScalingParameter::uniform custom tau_a/tau_e" {
  let param = AggregateScalingParameter::uniform(
    3, 5.0F, tau_a=300.0F, tau_e=100.0F, w_min=0.5F, w_max=400.0F,
  )
  assert_eq(param.tau, 10.0F)
  assert_eq(param.tau_a, 300.0F)
  assert_eq(param.tau_e, 100.0F)
  assert_eq(param.w_min, 0.5F)
  assert_eq(param.w_max, 400.0F)
}

///|
test "AggregateScaling::new initial state" {
  let n = 4
  // Build a simple 4-post, 2-pre per post with vals = [0.5, 1.5]
  // (WT[0] = 2.0 for post 0).
  let vals : Array[Float] = [0.5F, 1.5F, 0.5F, 1.5F, 0.5F, 1.5F, 0.5F, 1.5F]
  let rowptr : Array[Int] = [0, 2, 4, 6, 8]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let targets : Array[SynapseTarget] = [target]
  let param = AggregateScalingParameter::uniform(n, 10.0F)
  let c = AggregateScaling::new(n, targets, param)
  // WT[i] = 2.0 for every post (1 pre per post × 2 weights per edge)
  let mut i = 0
  while i < n {
    assert_eq(c.wt_total[i], 2.0F)
    assert_eq(c.y[i], 0.0F)
    assert_eq(c.wt[i], 0.0F)
    assert_eq(c.mu[i], 0.0F)
    i = i + 1
  }
}

///|
test "aggregate_scaling_forward: y decays without spike" {
  let n = 2
  let vals : Array[Float] = [1.0F, 1.0F]
  let rowptr : Array[Int] = [0, 1, 2]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let param = AggregateScalingParameter::uniform(n, 10.0F, tau_a=100.0F)
  let c = AggregateScaling::new(n, [target], param)
  let fire : Array[Float] = [0.0F, 0.0F]
  // 100 steps with no spikes: y should decay to ~0.
  let dt = 0.125F
  let mut s = 0
  while s < 100 {
    aggregate_scaling_forward(c, fire, dt)
    s = s + 1
  }
  // y[i] = 0 * (1 - 0.125/100)^100 ≈ 0
  if c.y[0] >= 0.01F {
    fail("y did not decay without spikes")
  }
  // WT[0] = (1 - WT[0]/Wmax) * (1 - 0/Y) / tau_e integrated for 100 steps.
  // Should grow from 1.0 toward Wmax=250 as y stays at 0.
  if c.wt_total[0] <= 1.0F {
    fail("WT did not grow with y=0")
  }
}

///|
test "aggregate_scaling_forward: y bumps on spike" {
  let n = 1
  let vals : Array[Float] = [1.0F]
  let rowptr : Array[Int] = [0, 1]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let param = AggregateScalingParameter::uniform(n, 10.0F, tau_a=100.0F)
  let c = AggregateScaling::new(n, [target], param)
  let fire : Array[Float] = [1.0F]
  let dt = 0.125F
  let _ = dt
  // First step with fire: y decays by y/tau_a then bumps by 1 on
  // fire (Euler order: decay → bump → WT-update).
  // Initial y = 0, so decay = 0, then bump = 1. Final y = 1.0F.
  aggregate_scaling_forward(c, fire, 0.125F)
  if c.y[0] < 0.9F || c.y[0] > 1.1F {
    fail("y did not bump on fire")
  }
  // After another step (no fire): y decays by 1/tau_a from 1.0.
  // y_new = 1.0 - 1.0*0.125/100 = 0.99875
  aggregate_scaling_forward(c, [0.0F], 0.125F)
  if c.y[0] < 0.9F || c.y[0] > 1.05F {
    fail("y did not decay after bump")
  }
}

///|
test "aggregate_scaling_plasticity: periodic weight rescaling" {
  let n = 2
  let vals : Array[Float] = [10.0F, 10.0F, 10.0F, 10.0F]
  let rowptr : Array[Int] = [0, 2, 4]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let param = AggregateScalingParameter::uniform(n, 10.0F, w_min=0.5F)
  let c = AggregateScaling::with_plasticity_interval(
    AggregateScaling::new(n, [target], param),
    4,
  )
  // Run 4 forward steps with no fire (WT grows toward 250, no
  // plasticity triggered at step 0, 1, 2, 3).
  let fire : Array[Float] = [0.0F, 0.0F]
  let mut s = 1
  while s <= 4 {
    aggregate_scaling_forward(c, fire, 0.125F)
    s = s + 1
  }
  // Call plasticity at step 4 (multiple of 4). wt[i] recomputed = 20.
  // mu[i] = (WT[i] - 0.5) / 20. W[s] = W[s] * mu + 0.5.
  aggregate_scaling_plasticity(c, 4)
  // After plasticity: vals[s] = 10 * mu + 0.5
  // Check that weights changed.
  if vals[0] == 10.0F && vals[1] == 10.0F {
    fail("weights did not rescale")
  }
}

///|
test "aggregate_scaling_plasticity: skip on non-periodic step" {
  let n = 1
  let vals : Array[Float] = [5.0F]
  let rowptr : Array[Int] = [0, 1]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let param = AggregateScalingParameter::uniform(n, 10.0F)
  let c = AggregateScaling::new(n, [target], param)
  let _ = AggregateScaling::with_plasticity_interval(c, 10)
  // Call plasticity at step 5 (NOT multiple of 10): should be no-op.
  aggregate_scaling_plasticity(c, 5)
  // Weights unchanged.
  assert_eq(vals[0], 5.0F)
}

///|
test "aggregate_scaling_plasticity: zero wt guarded" {
  let n = 1
  let vals : Array[Float] = [0.0F]
  let rowptr : Array[Int] = [0, 1]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let param = AggregateScalingParameter::uniform(n, 10.0F)
  let c0 = AggregateScaling::new(n, [target], param)
  let c = AggregateScaling::with_plasticity_interval(c0, 1)
  // Run forward once, plasticity at step 1. wt[i]=0, mu guarded to 1.
  aggregate_scaling_plasticity(c, 1)
  // vals[0] = 0 * 1 + 0.05 = 0.05 (w_min default in uniform).
  assert_eq(vals[0], 0.05F)
}