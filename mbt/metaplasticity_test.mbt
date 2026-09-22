// Tests for metaplasticity (MultiplicativeNorm, AdditiveNorm, SynapseNormalization).
//
// Plus additional coverage from refs/SNNModels.jl/test/syn/
// metaplasticity.jl: W0 sums from full connectivity, and SynapseNormalization
// AdditiveNorm form.

///|
test "MultiplicativeNorm::new — tau field" {
  let p = MultiplicativeNorm::new(100.0F)
  assert_eq(p.tau, 100.0F)
}

///|
test "AdditiveNorm::new — tau field" {
  let p = AdditiveNorm::new(200.0F)
  assert_eq(p.tau, 200.0F)
}

///|
test "SynapseNormalization — W0 captures initial per-post weight sums" {
  let vals : Array[Float] = [1.0F, 2.0F, 3.0F]
  let rowptr : Array[Int] = [0, 1, 2, 3]
  let target : SynapseTarget = { post_id: 0, vals, rowptr }
  let norm = SynapseNormalization::new([target], Multiplicative_(MultiplicativeNorm::new(100.0F)))
  assert_eq(norm.n_post, 3)
  assert_eq(norm.w0[0], 1.0F)
  assert_eq(norm.w0[1], 2.0F)
  assert_eq(norm.w0[2], 3.0F)
}

///|
test "SynapseNormalization — W0 sums across multiple synapses (same post)" {
  let v1 : Array[Float] = [1.0F, 2.0F]
  let v2 : Array[Float] = [3.0F, 4.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }, { post_id: 0, vals: v2, rowptr: r2 }],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  assert_eq(norm.w0[0], 4.0F)
  assert_eq(norm.w0[1], 6.0F)
}

///|
test "SynapseNormalization — AdditiveNorm form" {
  let v1 : Array[Float] = [1.0F, 2.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }],
    Additive_(AdditiveNorm::new(200.0F)),
  )
  assert_eq(norm.n_post, 2)
  assert_eq(norm.w0[0], 1.0F)
  assert_eq(norm.w0[1], 2.0F)
  // Additive: μ[i] = W0[i] - W1[i]. No shrinkage → μ=0 → vals unchanged.
  metaplasticity_step(norm)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
}

///|
test "SynapseNormalization — W0 sums initial weights (full connectivity)" {
  // 4 post-neurons, single synapse with 4 connections each = 1.0,
  // rowptr=[0, 4, 8, 12, 16]. Total W0[i] = 4.0 per post.
  let n = 4
  let v : Array[Float] = Array::make(n * n, 1.0F)
  let r : Array[Int] = [0, n, 2 * n, 3 * n, 4 * n]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v, rowptr: r }],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  assert_eq(norm.n_post, n)
  let mut i = 0
  while i < n {
    assert_eq(norm.w0[i], Float::from_int(n))
    i = i + 1
  }
}

///|
test "metaplasticity_step — multiplicative: W0 captured, then shrinkage restored" {
  let v1 : Array[Float] = [0.5F, 1.0F]
  let v2 : Array[Float] = [1.5F, 2.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }, { post_id: 0, vals: v2, rowptr: r2 }],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  // Initial vals give W0[0]=0.5+1.5=2.0, W0[1]=1.0+2.0=3.0.
  // After step: W1[0]=0.5+1.5=2.0, μ=0, vals unchanged.
  metaplasticity_step(norm)
  assert_eq(v1[0], 0.5F)
  assert_eq(v1[1], 1.0F)
  assert_eq(v2[0], 1.5F)
  assert_eq(v2[1], 2.0F)
}

///|
test "metaplasticity_step — multiplicative shrinkage + restore" {
  // W0 captured: W0[0]=1+3=4, W0[1]=2+4=6 (initial vals).
  // Then shrink to half. Multiplicative step should restore.
  let v1 : Array[Float] = [1.0F, 2.0F]
  let v2 : Array[Float] = [3.0F, 4.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }, { post_id: 0, vals: v2, rowptr: r2 }],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  // Now shrink to half: v1=[0.5, 1.0], v2=[1.5, 2.0].
  v1[0] = 0.5F
  v1[1] = 1.0F
  v2[0] = 1.5F
  v2[1] = 2.0F
  // After step: W1[0]=0.5+1.5=2.0, μ=(4-2)/2=1.0, w*=2 → [1.0, 3.0].
  metaplasticity_step(norm)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
  assert_eq(v2[0], 3.0F)
  assert_eq(v2[1], 4.0F)
}

///|
test "metaplasticity_step — additive shrinkage correction" {
  let v1 : Array[Float] = [1.0F, 2.0F]
  let v2 : Array[Float] = [3.0F, 4.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }, { post_id: 0, vals: v2, rowptr: r2 }],
    Additive_(AdditiveNorm::new(200.0F)),
  )
  // W0[0]=1+3=4, W0[1]=2+4=6 (captured at construction).
  // Shrink to half: W1[0]=0.5+1.5=2.0, W1[1]=1.0+2.0=3.0.
  // Additive: μ[i] = W0[i] - W1[i]. Applied to all synapses
  // connecting to post i:
  //   v1[0] = 0.5 + 2.0 = 2.5 (μ[0] added)
  //   v2[0] = 1.5 + 2.0 = 3.5 (μ[0] added)
  //   v1[1] = 1.0 + 3.0 = 4.0 (μ[1] added)
  //   v2[1] = 2.0 + 3.0 = 5.0 (μ[1] added)
  // Note: additive is NOT a restore (vs multiplicative); it over-
  // corrects when multiple synapses connect to the same post.
  v1[0] = 0.5F
  v1[1] = 1.0F
  v2[0] = 1.5F
  v2[1] = 2.0F
  metaplasticity_step(norm)
  assert_eq(v1[0], 2.5F)
  assert_eq(v1[1], 4.0F)
  assert_eq(v2[0], 3.5F)
  assert_eq(v2[1], 5.0F)
}

///|
test "metaplasticity_step — zero W1 handled gracefully (multiplicative)" {
  let v1 : Array[Float] = [0.0F, 0.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  metaplasticity_step(norm)
  assert_eq(v1[0], 0.0F)
  assert_eq(v1[1], 0.0F)
}

///|
test "SynapseTarget — post_id field captured" {
  // Verify the new `post_id` field on SynapseTarget is captured at
  // construction and preserved in `norm.targets[i].post_id`. Mirrors
  // Julia's `targets[:post] = unique(posts)[1]` semantics (we use
  // caller-supplied post_id rather than a NamedTuple-keyed lookup).
  let v1 : Array[Float] = [1.0F, 2.0F]
  let v2 : Array[Float] = [3.0F, 4.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [
      { post_id: 7, vals: v1, rowptr: r1 },
      { post_id: 7, vals: v2, rowptr: r2 },
    ],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  assert_eq(norm.targets[0].post_id, 7)
  assert_eq(norm.targets[1].post_id, 7)
}

///|
test "SynapseNormalization — accepts all targets sharing same post_id" {
  // Regression: all three targets share post_id=5 — must succeed and
  // sum per-post weights across all three target buffers.
  let v1 : Array[Float] = [1.0F, 2.0F]
  let v2 : Array[Float] = [3.0F, 4.0F]
  let v3 : Array[Float] = [5.0F, 6.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let r2 : Array[Int] = [0, 1, 2]
  let r3 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [
      { post_id: 5, vals: v1, rowptr: r1 },
      { post_id: 5, vals: v2, rowptr: r2 },
      { post_id: 5, vals: v3, rowptr: r3 },
    ],
    Multiplicative_(MultiplicativeNorm::new(100.0F)),
  )
  // W0[0] = 1 + 3 + 5 = 9.0
  // W0[1] = 2 + 4 + 6 = 12.0
  assert_eq(norm.w0[0], 9.0F)
  assert_eq(norm.w0[1], 12.0F)
}

///|
test "metaplasticity_step_gated — fires only every τ/dt steps" {
  // Mirrors Julia's outer `plasticity!` periodic gate. With τ = 0.5ms
  // and dt = 0.125ms, period = round(0.5/0.125) = 4. The step should
  // fire at step_count = 0, 4, 8, ... and be a no-op at 1, 2, 3, 5, 6, 7.
  let v1 : Array[Float] = [1.0F, 2.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }],
    Multiplicative_(MultiplicativeNorm::new(0.5F)),
  )
  // Step 0: should fire (W1=W0, no change).
  metaplasticity_step_gated(norm, 0, 0.125F)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
  // Step 1, 2, 3: no-op (gate skips).
  metaplasticity_step_gated(norm, 1, 0.125F)
  metaplasticity_step_gated(norm, 2, 0.125F)
  metaplasticity_step_gated(norm, 3, 0.125F)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
  // Shrink to half manually.
  v1[0] = 0.5F
  v1[1] = 1.0F
  // Step 4: should fire and restore (μ=1.0 → w*=2 → [1.0, 2.0]).
  metaplasticity_step_gated(norm, 4, 0.125F)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
  // Step 5, 6, 7: no-op.
  v1[0] = 0.25F
  v1[1] = 0.5F
  metaplasticity_step_gated(norm, 5, 0.125F)
  metaplasticity_step_gated(norm, 6, 0.125F)
  metaplasticity_step_gated(norm, 7, 0.125F)
  assert_eq(v1[0], 0.25F)
  assert_eq(v1[1], 0.5F)
  // Step 8: should fire (μ=3.0 → w*=4 → [1.0, 2.0]).
  metaplasticity_step_gated(norm, 8, 0.125F)
  assert_eq(v1[0], 1.0F)
  assert_eq(v1[1], 2.0F)
}

///|
test "metaplasticity_step_gated — τ=0 disables periodic normalization" {
  // τ=0 means no periodic firing (matches Julia default).
  let v1 : Array[Float] = [1.0F, 2.0F]
  let r1 : Array[Int] = [0, 1, 2]
  let norm = SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }],
    Multiplicative_(MultiplicativeNorm::new(0.0F)),
  )
  // Manually perturb weights; gated step should NOT restore them.
  v1[0] = 0.5F
  v1[1] = 1.0F
  metaplasticity_step_gated(norm, 0, 0.125F)
  metaplasticity_step_gated(norm, 4, 0.125F)
  metaplasticity_step_gated(norm, 100, 0.125F)
  // Weights stay perturbed (no firing).
  assert_eq(v1[0], 0.5F)
  assert_eq(v1[1], 1.0F)
}