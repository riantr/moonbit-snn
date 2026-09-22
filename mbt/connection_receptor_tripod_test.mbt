// Tests for ReceptorSynapseTripod — multi-receptor synaptic wiring
// into TripodHet (4 receptors × 3 compartments).

///|
test "ReceptorSynapseTripod::new: shapes and initial state" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(3, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(2, base)
  let post = TripodHet::new(2, param, rng)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 1.0F, 0.0F, 1.0F, rng,
  )
  if c.target_compartment != "d1" {
    fail("target_compartment = \{c.target_compartment}, expected d1")
  }
  if c.g_state.length() != 2 * 4 {
    fail("g_state.length = \{c.g_state.length()}, expected 8")
  }
  if c.h_state.length() != 2 * 4 {
    fail("h_state.length = \{c.h_state.length()}, expected 8")
  }
  if c.ge_out.length() != 2 {
    fail("ge_out.length = \{c.ge_out.length()}, expected 2")
  }
  if c.matrix.nnz() == 0 {
    fail("matrix should have connections (p=1.0, 3 pre × 2 post)")
  }
}

///|
test "ReceptorSynapseTripod: AMPA target receptor → glu_d1 buffer" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(2, base)
  let post = TripodHet::new(2, param, rng)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 2.0F, 0.0F, 1.0F, rng,
  )
  pre.fire[0] = true
  // AMPA = target_receptor 0
  forward_receptor_tripod_synapse(c, 0, 0.0F)
  if (post.glu_d1[0] - 2.0F).abs() > 1.0e-6F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, expected 2.0")
  }
  if (post.glu_d1[1] - 2.0F).abs() > 1.0e-6F {
    fail("glu_d1[1] = \{post.glu_d1[1]}, expected 2.0")
  }
  // Other compartments stay 0.
  if post.glu_s[0] != 0.0F || post.glu_d2[0] != 0.0F {
    fail("only d1 should receive AMPA weights; glu_s or glu_d2 leaked")
  }
  if post.gaba_d1[0] != 0.0F {
    fail("AMPA should not write to gaba_d1")
  }
}

///|
test "ReceptorSynapseTripod: GABAa target → gaba_d1 buffer" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(2, base)
  let post = TripodHet::new(2, param, rng)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 1.5F, 0.0F, 1.0F, rng,
  )
  pre.fire[0] = true
  // GABAa = target_receptor 2
  forward_receptor_tripod_synapse(c, 2, 0.0F)
  if (post.gaba_d1[0] - 1.5F).abs() > 1.0e-6F {
    fail("gaba_d1[0] = \{post.gaba_d1[0]}, expected 1.5")
  }
  if (post.gaba_d1[1] - 1.5F).abs() > 1.0e-6F {
    fail("gaba_d1[1] = \{post.gaba_d1[1]}, expected 1.5")
  }
  if post.glu_d1[0] != 0.0F {
    fail("GABAa should not write to glu_d1")
  }
}

///|
test "ReceptorSynapseTripod: soma compartment routing" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "soma", receptors, nmda_dep, 3.0F, 0.0F, 1.0F, rng,
  )
  pre.fire[0] = true
  // AMPA → soma
  forward_receptor_tripod_synapse(c, 0, 0.0F)
  if (post.glu_s[0] - 3.0F).abs() > 1.0e-6F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 3.0 (soma)")
  }
  if post.glu_d1[0] != 0.0F || post.glu_d2[0] != 0.0F {
    fail("only :soma should receive AMPA weights")
  }
}

///|
test "step_receptors_tripod_synapse: 2-state ODE consumes buffer, populates ge_out" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  // Set v_s near threshold so AMPA current is meaningful.
  post.v_s[0] = -30.0F
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 2.0F, 0.0F, 1.0F, rng,
  )
  pre.fire[0] = true
  forward_receptor_tripod_synapse(c, 0, 0.0F)
  // After forward, glu_d1[0] = 2.0
  if post.glu_d1[0] != 2.0F {
    fail("setup: glu_d1[0] = \{post.glu_d1[0]}, expected 2.0")
  }
  step_receptors_tripod_synapse(c, 0.125F)
  // After step, glu_d1 consumed (= 0), g_state should be non-zero, ge_out should be non-zero
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, expected 0 (consumed)")
  }
  if c.g_state[0] == 0.0F {
    fail("g_state[0] should be non-zero after pulse; got 0")
  }
  // ge_out should reflect AMPA contribution only (other receptors had
  // no input, so their g_state[i, r] is 0, contributing 0 to current).
  // I = gsyn_AMPA * g_state[0, AMPA] * (v - e_rev) = positive (v > e_rev)
  if c.ge_out[0] == 0.0F {
    fail("ge_out[0] should be non-zero; got 0")
  }
  // I_AMPA = gsyn*g*(v_s - e_rev). v_s = -30 < e_rev = 0, so I < 0.
  if c.ge_out[0] > 0.0F {
    fail("ge_out[0] = \{c.ge_out[0]}, expected negative (v_s=-30 < e_rev=0)")
  }
}

///|
test "ReceptorSynapseTripod: non-firing pre → no conductance change" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(2, pre_param, rng)
  let base = AdExParameter::new()
  let param = AdExParameterHet::homogeneous(1, base)
  let post = TripodHet::new(1, param, rng)
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let receptors = Receptors::new(ampa, nmda, gabaa, gabab)
  let nmda_dep = NMDAVoltageDependency::eyal()
  let c = ReceptorSynapseTripod::new(
    pre, post, "d1", receptors, nmda_dep, 2.0F, 0.0F, 1.0F, rng,
  )
  forward_receptor_tripod_synapse(c, 0, 0.0F)
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]}, expected 0 (no pre-fire)")
  }
  if post.gaba_d1[0] != 0.0F {
    fail("gaba_d1[0] = \{post.gaba_d1[0]}, expected 0 (no pre-fire)")
  }
}