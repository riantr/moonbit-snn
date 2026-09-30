// metaplasticity_integration_test.mbt — integration tests for v0.41.2
// SynapseNormalization (connection_normalization reuses these via the
// "real impl in metaplasticity.mbt" stub file).
//
// The 13 baseline tests in `metaplasticity_test.mbt` cover:
//   - W0 captures initial sums
//   - single-step multiplicative / additive shrinkage
//   - gated step periodicity (every τ/dt steps)
//   - τ=0 disables gating
//   - per-target SynapseTarget post_id
//
// This file covers higher-level scenarios:
//   - multi-step convergence: W1 → W0 over many iterations
//   - per-post heterogeneity: different W0 per post-neuron
//   - multi-target aggregation: W0 sums across multiple synapses
//   - gated step actually drives convergence toward W0
//   - w0=0 edge case (target has no incoming → μ=0)
//
// CSR note: metaplasticity uses POST-major CSR (rowptr indexed by
// post-neuron) — distinct from SpikingSynapse's PRE-major CSR.

// ── helpers ──────────────────────────────────────────────────────

///|
fn make_post_csr(n_post : Int, weights : Array[Float]) -> (Array[Float], Array[Int]) {
  // Build a single-target CSR: 1 target, n_post rows, weights split
  // row-by-row. Weights must have total length n_post (one per row).
  let rowptr : Array[Int] = Array::make(n_post + 1, 0)
  for i in 0..<n_post {
    rowptr[i + 1] = rowptr[i] + 1
  }
  let vals : Array[Float] = weights.copy()
  (vals, rowptr)
}

///|
fn make_multi_target(
  n_post : Int,
  // Per-target weights (rows of n_post each).
  target_weights : Array[Array[Float]],
) -> (Array[SynapseTarget], Array[Float], Array[Float]) {
  // Build the targets. Each target's rowptr is identical (n_post + 1).
  let targets : Array[SynapseTarget] = []
  let rowptr : Array[Int] = Array::make(n_post + 1, 0)
  for i in 0..<n_post {
    rowptr[i + 1] = rowptr[i] + 1
  }
  // vals_per_target[i] is the per-target weight buffer (length n_post).
  let vals_per_target : Array[Array[Float]] = []
  for tw in target_weights {
    let v = tw.copy()
    vals_per_target.push(v)
    targets.push({ post_id: 0, vals: vals_per_target[vals_per_target.length() - 1], rowptr })
  }
  (targets, vals_per_target[0], vals_per_target[1 % vals_per_target.length()])
}

// ── tests ────────────────────────────────────────────────────────

///|
test "metaplasticity_multiplicative_converges_w1_to_w0" {
  // 1 target, 4 post-neurons. Initial weights are all 1.0 → W0=[4.0].
  // (Actually each post has 1 incoming with weight 1.0, so W0=[1,1,1,1].)
  // Mutate weights to [0.5, 2.0, 0.5, 3.0] → W1=[0.5, 2.0, 0.5, 3.0].
  // After 1 multiplicative step:
  //   μ[0] = (1 - 0.5) / 0.5 = 1.0 → weights *= 2 → [1.0, ...]
  // Run 50 steps; W1 should converge to W0=[1, 1, 1, 1].
  let (vals, rowptr) = make_post_csr(4, [0.5F, 2.0F, 0.5F, 3.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let targets : Array[SynapseTarget] = [target]
  let norm = SynapseNormalization::new(targets, NormParam::Multiplicative_(MultiplicativeNorm::new(1.0F)))
  // W0 captured = [1, 1, 1, 1].
  for _ in 0..<200 {
    metaplasticity_step(norm)
  }
  // After many steps, weights must be very close to W0.
  for i in 0..<4 {
    if (vals[i] - norm.w0[i]).abs() > 0.1F {
      fail("vals[\{i}] = \{vals[i]}, expected ~\{norm.w0[i]} after convergence")
    }
  }
}

///|
test "metaplasticity_additive_converges_w1_to_w0" {
  // Same setup but additive rule. With 1 incoming per post, W0=initial
  // weight. After one step with weights mutated: additive shifts by
  // (W0 - W1). After many steps, weights approach W0.
  let (vals, rowptr) = make_post_csr(4, [0.5F, 2.0F, 0.5F, 3.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let targets : Array[SynapseTarget] = [target]
  let norm = SynapseNormalization::new(targets, NormParam::Additive_(AdditiveNorm::new(1.0F)))
  // W0 = [1, 1, 1, 1] (initial weight before mutation).
  for _ in 0..<200 {
    metaplasticity_step(norm)
  }
  for i in 0..<4 {
    if (vals[i] - norm.w0[i]).abs() > 0.1F {
      fail("vals[\{i}] = \{vals[i]}, expected ~\{norm.w0[i]} after convergence")
    }
  }
}

///|
test "metaplasticity_per_post_heterogeneity" {
  // W0 should be captured per-post. Two posts with very different
  // initial sums; after one step each post is normalised independently.
  // Build 1 target with rowptr=[1,1] (2 posts, 1 incoming per post).
  let (vals, _) = make_post_csr(2, [2.0F, 4.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr: Array::make(3, 0) }
  // Fix rowptr to [0, 1, 2] (1 element per row).
  target.rowptr[0] = 0
  target.rowptr[1] = 1
  target.rowptr[2] = 2
  let norm = SynapseNormalization::new([target], NormParam::Multiplicative_(MultiplicativeNorm::new(1.0F)))
  // W0 captured = [2.0, 4.0] (initial weights before mutation).
  // Now mutate weights: post-0 → 1.0, post-1 → 8.0.
  vals[0] = 1.0F
  vals[1] = 8.0F
  metaplasticity_step(norm)
  // post-0: μ = (2-1)/1 = 1.0 → vals[0] *= 2 = 2.0
  // post-1: μ = (4-8)/8 = -0.5 → vals[1] *= 0.5 = 4.0
  if (vals[0] - 2.0F).abs() > 1.0e-5F {
    fail("vals[0] = \{vals[0]}, expected 2.0 (W0 restoration)")
  }
  if (vals[1] - 4.0F).abs() > 1.0e-5F {
    fail("vals[1] = \{vals[1]}, expected 4.0 (W0 restoration)")
  }
}

///|
test "metaplasticity_multi_target_aggregation" {
  // Two targets share post-neurons 0..1. Each target has 1 incoming
  // per post. Initial weights: target A=[1, 1], target B=[2, 2].
  // W0[i] = sum across targets = [3, 3].
  let rowptr : Array[Int] = [0, 1, 2]
  let a_vals : Array[Float] = [1.0F, 1.0F]
  let b_vals : Array[Float] = [2.0F, 2.0F]
  let ta : SynapseTarget = { post_id: 0, vals: a_vals, rowptr }
  let tb : SynapseTarget = { post_id: 0, vals: b_vals, rowptr }
  let norm = SynapseNormalization::new([ta, tb], NormParam::Multiplicative_(MultiplicativeNorm::new(1.0F)))
  if norm.w0[0] != 3.0F || norm.w0[1] != 3.0F {
    fail("W0 = [\{norm.w0[0]}, \{norm.w0[1]}], expected [3, 3] (sum across targets)")
  }
}

///|
test "metaplasticity_gated_step_drives_convergence" {
  // Start with perturbed weights. Run gated steps; after many periods
  // weights must converge to W0.
  let (vals, rowptr) = make_post_csr(4, [0.5F, 2.0F, 0.5F, 3.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let norm = SynapseNormalization::new([target], NormParam::Additive_(AdditiveNorm::new(0.5F)))
  // τ=0.5, dt=0.1 → period_steps = 5. Run 200 steps (= 40 periods).
  for s in 1..<201 {
    metaplasticity_step_gated(norm, s, 0.1F)
  }
  for i in 0..<4 {
    if (vals[i] - norm.w0[i]).abs() > 0.5F {
      fail("vals[\{i}] = \{vals[i]}, expected ~\{norm.w0[i]} after 200 gated steps")
    }
  }
}

///|
test "metaplasticity_w0_zero_no_op" {
  // W0[i] = 0 (no incoming) → μ = 0 - 0 = 0 → no weight change.
  // 2 posts, both with initial weight 0.
  let (vals, rowptr) = make_post_csr(2, [0.0F, 0.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let norm = SynapseNormalization::new([target], NormParam::Multiplicative_(MultiplicativeNorm::new(1.0F)))
  vals[0] = 5.0F  // perturbed
  vals[1] = 5.0F
  metaplasticity_step(norm)
  // W0=0, W1=5 → multiplicative μ = (0-5)/5 = -1.0 → vals *= 0 = 0.
  if vals[0] != 0.0F {
    fail("vals[0] = \{vals[0]}, expected 0 (W0=0 collapses to zero)")
  }
  if vals[1] != 0.0F {
    fail("vals[1] = \{vals[1]}, expected 0")
  }
}

///|
test "metaplasticity_gated_period_steps_match_tau_over_dt" {
  // τ=1.0, dt=0.1 → period=10. Step at s=10, 20, 30 should fire; s=5
  // should not. Use an additive rule with non-zero delta so we can
  // detect whether the step ran.
  let (vals, rowptr) = make_post_csr(1, [0.0F])
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let norm = SynapseNormalization::new([target], NormParam::Additive_(AdditiveNorm::new(1.0F)))
  // W0 = 0. Perturb: vals[0] = 1.0.
  vals[0] = 1.0F
  // s=5: should not fire (5 % 10 != 0). Weight stays at 1.0.
  metaplasticity_step_gated(norm, 5, 0.1F)
  if vals[0] != 1.0F {
    fail("s=5 fired unexpectedly: vals[0] = \{vals[0]}")
  }
  // s=10: should fire (10 % 10 == 0). Weight becomes 1.0 + (0-1) = 0.0.
  metaplasticity_step_gated(norm, 10, 0.1F)
  if vals[0] != 0.0F {
    fail("s=10 should fire: vals[0] = \{vals[0]}, expected 0.0")
  }
}