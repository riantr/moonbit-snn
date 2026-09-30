// ca_plasticity_integration_test.mbt — integration tests for v0.41.1
// CaPlasticity. The baseline 11 tests in `ca_plasticity_test.mbt`
// cover the per-call math (pre-fire weight update, post-fire weight
// update, trace decay, spike bumps, clamp, active-flag toggle).
// This file covers higher-level integration:
//   - active-gate actually prevents weight updates
//   - combined pre-fire + post-fire in the same step
//   - pure decay with no spikes
//   - repeated pre-fire LTP drives weight up over many steps
//   - asymmetric A_pre / A_post produces biased plasticity
//   - weight clamping at w_min when LTD dominates

///|
fn make_toy_csr() -> (Array[Float], Array[Int], Array[Int]) {
  // 2 pre × 3 post: pre0 → post0, post1; pre1 → post1, post2.
  // CSR rowptr: [0, 2, 4]; colptr: [0, 1, 1, 2].
  let w : Array[Float] = [1.0F, 1.0F, 1.0F, 1.0F]
  let colptr : Array[Int] = [0, 1, 1, 2]
  let rowptr : Array[Int] = [0, 2, 4]
  (w, colptr, rowptr)
}

///|
test "ca_plasticity_active_false_blocks_weight_update" {
  let (w, colptr, rowptr) = make_toy_csr()
  let vars = CaPlasticityVariables::new(2, 3)
  let param = CaPlasticityParameter::new()
  // Set active=false via vars.active[0].
  vars.active[0] = false
  // Pre-fire pre0 → post0/1; post-fire post1.
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, true, false]
  let w_before = w.copy()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // Weights must be unchanged.
  for s in 0..<w.length() {
    if w[s] != w_before[s] {
      fail("w[\{s}] = \{w[s]}, expected \{w_before[s]} (active=false)")
    }
  }
  // Traces also must be untouched (no decay, no spike bumps).
  if vars.tpre[0] != 0.0F || vars.tpre[1] != 0.0F {
    fail("tpre modified despite active=false: \{vars.tpre[0]}, \{vars.tpre[1]}")
  }
  if vars.tpost[0] != 0.0F || vars.tpost[1] != 0.0F || vars.tpost[2] != 0.0F {
    fail("tpost modified despite active=false: \{vars.tpost}")
  }
}

///|
test "ca_plasticity_combined_pre_and_post_fire_same_step" {
  // Pre-fire on pre0 (adds tpost[i] to W[0,1]) AND post-fire on post1
  // (adds tpre[j] to all edges targeting post1).
  let (w, colptr, rowptr) = make_toy_csr()
  let vars = CaPlasticityVariables::new(2, 3)
  let param = CaPlasticityParameter::new()
  // Pre-set traces so we can predict weight changes.
  vars.tpre[0] = 0.5F   // pre0 has accumulated some tpre
  vars.tpre[1] = 0.0F
  vars.tpost[0] = 0.7F
  vars.tpost[1] = 0.3F
  vars.tpost[2] = 0.0F
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, true, false]
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // Edges in CSR order: 0=(0,0), 1=(0,1), 2=(1,1), 3=(1,2).
  // Step 1 (pre-fire): pre0 fires → edges 0,1 get +tpost[i].
  //   W[0] = 1.0 + 0.7 = 1.7
  //   W[1] = 1.0 + 0.3 = 1.3
  // Step 2 (post-fire): post1 fires → walk all rows, find edges
  //   targeting colptr=1 (W[1] from pre0, W[2] from pre1).
  //   W[1] += tpre[0] = 0.5 → 1.8
  //   W[2] += tpre[1] = 0.0 → 1.0
  // W[3] = 1.0 (no update).
  // Step 3 (trace decay): all tpre/tpost decay by (1 - dt/tau).
  // Step 4 (spike bumps): tpre[0] += A_pre = 0.1; tpost[1] += A_post = 0.1.
  // Step 5 (clamp): all weights within [0, 30].
  let w0_expected = 1.0F + 0.7F
  let w1_expected = 1.0F + 0.3F + 0.5F
  let w2_expected = 1.0F + 0.0F
  let w3_expected = 1.0F
  if (w[0] - w0_expected).abs() > 1.0e-5F {
    fail("w[0] = \{w[0]}, expected \{w0_expected}")
  }
  if (w[1] - w1_expected).abs() > 1.0e-5F {
    fail("w[1] = \{w[1]}, expected \{w1_expected}")
  }
  if (w[2] - w2_expected).abs() > 1.0e-5F {
    fail("w[2] = \{w[2]}, expected \{w2_expected}")
  }
  if (w[3] - w3_expected).abs() > 1.0e-5F {
    fail("w[3] = \{w[3]}, expected \{w3_expected}")
  }
}

///|
test "ca_plasticity_pure_decay_no_spikes" {
  let (w, colptr, rowptr) = make_toy_csr()
  let vars = CaPlasticityVariables::new(2, 3)
  // Set some non-zero traces so we can verify decay.
  vars.tpre[0] = 0.4F
  vars.tpre[1] = 0.8F
  vars.tpost[0] = 1.0F
  vars.tpost[1] = 0.2F
  vars.tpost[2] = 0.5F
  let param = CaPlasticityParameter::new()
  let pre_fire : Array[Bool] = [false, false]
  let post_fire : Array[Bool] = [false, false, false]
  let w_before = w.copy()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // Weights untouched (no pre-fire, no post-fire).
  for s in 0..<w.length() {
    if w[s] != w_before[s] {
      fail("w[\{s}] changed despite no spikes: got \{w[s]}, expected \{w_before[s]}")
    }
  }
  // Traces decay: tpre_new = tpre * (1 - dt/tau_pre).
  let decay_pre = 1.0F - 0.125F / param.tau_pre
  let decay_post = 1.0F - 0.125F / param.tau_post
  if (vars.tpre[0] - 0.4F * decay_pre).abs() > 1.0e-5F {
    fail("tpre[0] = \{vars.tpre[0]}, expected \{0.4F * decay_pre}")
  }
  if (vars.tpre[1] - 0.8F * decay_pre).abs() > 1.0e-5F {
    fail("tpre[1] = \{vars.tpre[1]}, expected \{0.8F * decay_pre}")
  }
  if (vars.tpost[0] - 1.0F * decay_post).abs() > 1.0e-5F {
    fail("tpost[0] = \{vars.tpost[0]}, expected \{1.0F * decay_post}")
  }
}

///|
test "ca_plasticity_repeated_prefire_drives_weight_up" {
  // Drive LTP: both pre neurons AND post0 + post1 fire every step.
  // CSR layout: edges 0=(0,0), 1=(0,1), 2=(1,1), 3=(1,2).
  // Every edge except W[3] (pre1→post2) touches at least one active
  // pre + one active post. Both directions of the Hebbian rule apply:
  //   - pre-spike: W[s] += tpost[colptr[s]]   (builds tpost via post spike)
  //   - post-spike: W[s] += tpre[row-pre]     (builds tpre via pre spike)
  let (w, colptr, rowptr) = make_toy_csr()
  let vars = CaPlasticityVariables::new(2, 3)
  let param = CaPlasticityParameter::new()
  let pre_fire : Array[Bool] = [true, true]
  let post_fire : Array[Bool] = [true, true, false]
  for _ in 0..<100 {
    ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.1F)
  }
  // After 100 steps, edges W[0], W[1], W[2] must have grown above 1.0.
  // Edge W[3] (pre1→post2) targets the silent post2 and stays at 1.0.
  if w[0] <= 1.0F + 0.1F {
    fail("w[0] = \{w[0]}, expected > 1.1 (no LTP growth on pre0→post0)")
  }
  if w[1] <= 1.0F + 0.1F {
    fail("w[1] = \{w[1]}, expected > 1.1 (no LTP growth on pre0→post1)")
  }
  if w[2] <= 1.0F + 0.1F {
    fail("w[2] = \{w[2]}, expected > 1.1 (no LTP growth on pre1→post1)")
  }
  if w[3] != 1.0F {
    fail("w[3] = \{w[3]}, expected 1.0 (no update — post2 silent)")
  }
}

///|
test "ca_plasticity_asymmetric_a_pre_vs_a_post" {
  // High A_pre → strong LTP on pre-fire; high A_post → strong LTD on post-fire.
  let (w, colptr, rowptr) = make_toy_csr()
  let vars = CaPlasticityVariables::new(2, 3)
  let param : CaPlasticityParameter = {
    a_pre: 5.0F, a_post: 0.001F, tau_pre: 20.0F, tau_post: 20.0F,
    w_max: 30.0F, w_min: 0.0F,
  }
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, false, false]
  // One pre-fire step → strong tpre[0] bump, modest weight update.
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.1F)
  // tpre[0] should be ≈ A_pre = 5.0 (no decay yet within same step happens
  // BEFORE the spike bump, so no decay applied to the bump).
  // Actually wait — order is: pre-fire weight update → post-fire weight
  // update → trace decay → spike bump. So tpre[0] = 0 → spike bump → 5.0.
  if (vars.tpre[0] - 5.0F).abs() > 1.0e-5F {
    fail("tpre[0] = \{vars.tpre[0]}, expected 5.0 (high A_pre)")
  }
}

///|
test "ca_plasticity_weight_clamps_at_w_max" {
  // Saturate at w_max=5.0 (override default 30.0).
  let w : Array[Float] = [4.8F]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = CaPlasticityVariables::new(1, 1)
  let param : CaPlasticityParameter = {
    a_pre: 0.1F, a_post: 0.1F, tau_pre: 20.0F, tau_post: 20.0F,
    w_max: 5.0F, w_min: 0.0F,
  }
  // Pre-fire pre0 → edge 0 += tpost[0]. Set tpost[0] = 1.0 first.
  vars.tpost[0] = 1.0F
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.1F)
  // Pre-fire added 1.0 → W=5.8 → clamp to 5.0.
  if w[0] != 5.0F {
    fail("w[0] = \{w[0]}, expected clamp to 5.0")
  }
}

///|
test "ca_plasticity_weight_clamps_at_w_min" {
  // With w_min > 0, weights cannot go below.
  let w : Array[Float] = [0.5F]
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let vars = CaPlasticityVariables::new(1, 1)
  let param : CaPlasticityParameter = {
    a_pre: 0.1F, a_post: 0.1F, tau_pre: 20.0F, tau_post: 20.0F,
    w_max: 30.0F, w_min: 1.0F,
  }
  vars.tpost[0] = -1.0F  // negative tpost → weight would drop to -0.5
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.1F)
  if w[0] != 1.0F {
    fail("w[0] = \{w[0]}, expected clamp to 1.0")
  }
}

///|
test "ca_plasticity_set_ltp_active_round_trip" {
  // Toggle the active flag and verify the step actually skips when false.
  let (w, colptr, rowptr) = make_toy_csr()
  let param = CaPlasticityParameter::new()
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false, true, false]
  // Build an entry with non-zero traces so the second pass actually
  // produces a weight change. Otherwise pre-fire would add tpost=0 and
  // post-fire would add tpre=0 (both default-zero), giving zero delta
  // even when active=true.
  let entry = CaPlasticityEntry::new(0, 2, 3)
  entry.vars.tpre[0] = 0.5F
  entry.vars.tpost[1] = 0.5F
  // Set inactive via the entry's helper.
  CaPlasticityEntry::set_ltp_active(entry, false)
  let w_before = w.copy()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, entry.vars, param, 0.0F, 0.125F)
  for s in 0..<w.length() {
    if w[s] != w_before[s] {
      fail("w[\{s}] changed despite active=false: got \{w[s]}")
    }
  }
  // Re-enable and verify the step now updates.
  CaPlasticityEntry::set_ltp_active(entry, true)
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, entry.vars, param, 0.0F, 0.125F)
  // At least one weight must have changed.
  let mut changed = false
  for s in 0..<w.length() {
    if w[s] != w_before[s] {
      changed = true
    }
  }
  if !changed {
    fail("after re-enabling active, no weight changed")
  }
}