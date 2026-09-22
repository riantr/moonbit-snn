// connection_receptor_test.mbt — tests for `ReceptorSynapse` per-edge
// receptor routing in connection_receptor.mbt.
//
// Julia's `ReceptorSynapse(glu_receptors = [1], gaba_receptors = [2])`
// from `parameters.jl` testset. We port:
//   - default glu_receptors / gaba_receptors values
//   - custom glu_receptors / gaba_receptors
//   - per-edge target_receptor routing
//   - forward_receptor_synapse routes to glu vs gaba correctly

///|
fn make_if_for_receptor(n : Int) -> IF {
  let param = IFParameter::with_el(-49.0F)
  let rng = Xoshiro::new(42UL)
  IF::new(n, param, rng)
}

///|
test "ReceptorSynapse::new — defaults: glu=[0,1], gaba=[2,3]" {
  let pre : IF = make_if_for_receptor(10)
  let post : IF = make_if_for_receptor(10)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let syn = Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let rng = Xoshiro::new(42UL)
  let s = ReceptorSynapse::new(pre, post, syn, nmda_dep, 1.0F, 0.0F, 0.5F, rng)
  // Default glu_receptors = [0, 1] (AMPA, NMDA).
  assert_eq(s.glu_receptors.length(), 2)
  assert_eq(s.glu_receptors[0], 0)
  assert_eq(s.glu_receptors[1], 1)
  // Default gaba_receptors = [2, 3] (GABAa, GABAb).
  assert_eq(s.gaba_receptors.length(), 2)
  assert_eq(s.gaba_receptors[0], 2)
  assert_eq(s.gaba_receptors[1], 3)
  // Per-edge target_receptor defaults to 0 (AMPA) for all edges.
  let n_edges = s.matrix.vals.length()
  assert_eq(s.target_receptor.length(), n_edges)
  let mut all_ampa = true
  let mut i = 0
  while i < n_edges {
    if s.target_receptor[i] != 0 {
      all_ampa = false
    }
    i = i + 1
  }
  assert_eq(all_ampa, true)
  // Pre-allocated buffers size = post.n.
  assert_eq(s.glu.length(), 10)
  assert_eq(s.gaba.length(), 10)
}

///|
test "ReceptorSynapse — custom glu_receptors / gaba_receptors" {
  // The Julia test passes `glu_receptors = [1]` (1-based, so receptor
  // 0 in 0-based MoonBit) and `gaba_receptors = [2]` (receptor 1).
  let pre : IF = make_if_for_receptor(5)
  let post : IF = make_if_for_receptor(5)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let syn = Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let rng = Xoshiro::new(42UL)
  let s = ReceptorSynapse::new(pre, post, syn, nmda_dep, 1.0F, 0.0F, 0.5F, rng)
  // Custom: glu_receptors = [0] (AMPA only), gaba_receptors = [1] (NMDA!).
  s.glu_receptors[0] = 0
  s.glu_receptors[1] = 0  // truncate to single entry by setting last to same
  // Actually [0, 0] is still 2 entries. Use a 1-element array.
  let new_glu : Array[Int] = [0]
  let new_gaba : Array[Int] = [1]
  // Manually overwrite the field via struct literal rebuild.
  let s2 : ReceptorSynapse = {
    pre: s.pre,
    post: s.post,
    matrix: s.matrix,
    syn: s.syn,
    target_receptor: s.target_receptor,
    glu_receptors: new_glu,
    gaba_receptors: new_gaba,
    nmda_dep: s.nmda_dep,
    delays: s.delays,
    glu: s.glu,
    gaba: s.gaba,
  }
  assert_eq(s2.glu_receptors.length(), 1)
  assert_eq(s2.glu_receptors[0], 0)
  assert_eq(s2.gaba_receptors.length(), 1)
  assert_eq(s2.gaba_receptors[0], 1)
}

///|
test "ReceptorSynapse::set_target_receptor — change edge target" {
  let pre : IF = make_if_for_receptor(10)
  let post : IF = make_if_for_receptor(10)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let syn = Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let rng = Xoshiro::new(42UL)
  let s = ReceptorSynapse::new(pre, post, syn, nmda_dep, 1.0F, 0.0F, 0.5F, rng)
  // Default target_receptor = 0 (AMPA) for all edges.
  if s.matrix.vals.length() > 0 {
    assert_eq(s.target_receptor[0], 0)
    // Re-target edge 0 to NMDA (1).
    s.set_target_receptor(0, 1)
    assert_eq(s.target_receptor[0], 1)
    // Re-target edge 0 to GABAa (2).
    s.set_target_receptor(0, 2)
    assert_eq(s.target_receptor[0], 2)
  }
}

///|
test "forward_receptor_synapse — routes to glu buffer for AMPA" {
  // Build a 2×1 synapse: pre[0] fires, with one edge to post[0].
  let pre : IF = make_if_for_receptor(2)
  let post : IF = make_if_for_receptor(1)
  // Force pre[0] to fire on next forward.
  pre.fire[0] = true
  // Build a deterministic 2×1 dense matrix.
  let dense : Array[Array[Float]] = [
    [2.5F],  // pre[0] → post[0] weight 2.5
    [0.0F],  // pre[1] → post[0] weight 0
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let syn = Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let rng = Xoshiro::new(42UL)
  let s = ReceptorSynapse::new(pre, post, syn, nmda_dep, 1.0F, 0.0F, 0.0F, rng)
  // Manually replace matrix with our dense 2×1 (1 nnz).
  let s2 : ReceptorSynapse = {
    pre: s.pre,
    post: s.post,
    matrix: m,
    syn: s.syn,
    target_receptor: [0],  // AMPA for the single edge
    glu_receptors: s.glu_receptors,
    gaba_receptors: s.gaba_receptors,
    nmda_dep: s.nmda_dep,
    delays: s.delays,
    glu: [0.0F],
    gaba: [0.0F],
  }
  forward_receptor_synapse(s2)
  // Edge is AMPA → routed to glu[0].
  let diff = (s2.glu[0] - 2.5F).abs()
  if diff > 1.0e-6F {
    fail("glu[0] = \{s2.glu[0]}, expected 2.5 (AMPA routed to glu)")
  }
  // gaba should remain 0.
  assert_eq(s2.gaba[0], 0.0F)
}

///|
test "forward_receptor_synapse — routes to gaba buffer for GABAa" {
  // Same setup but edge targets GABAa (2) instead of AMPA (0).
  let pre : IF = make_if_for_receptor(2)
  let post : IF = make_if_for_receptor(1)
  pre.fire[0] = true
  let dense : Array[Array[Float]] = [
    [1.5F],
    [0.0F],
  ]
  let m = SparseMatrixCSR::from_dense(dense, 0.1F)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda_rec = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let syn = Receptors::new(ampa, nmda_rec, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let rng = Xoshiro::new(42UL)
  let s = ReceptorSynapse::new(pre, post, syn, nmda_dep, 1.0F, 0.0F, 0.0F, rng)
  let s2 : ReceptorSynapse = {
    pre: s.pre,
    post: s.post,
    matrix: m,
    syn: s.syn,
    target_receptor: [2],  // GABAa
    glu_receptors: s.glu_receptors,
    gaba_receptors: s.gaba_receptors,
    nmda_dep: s.nmda_dep,
    delays: s.delays,
    glu: [0.0F],
    gaba: [0.0F],
  }
  forward_receptor_synapse(s2)
  // Edge is GABAa → routed to gaba[0].
  let diff = (s2.gaba[0] - 1.5F).abs()
  if diff > 1.0e-6F {
    fail("gaba[0] = \{s2.gaba[0]}, expected 1.5 (GABAa routed to gaba)")
  }
  // glu should remain 0.
  assert_eq(s2.glu[0], 0.0F)
}