// Tests for CompartmentSynapseBall / CompartmentSynapseTripod
// (compartment-targeted SpikingSynapse variants for multi-compartment
// neurons).
//
// Covers:
//   - Construction (matrix shape, target field)
//   - Forward routing: pre-fire → correct compartment buffer
//   - Sym dispatch (:ge → glu, :gi → gaba)
//   - Target dispatch (:soma vs :d for Ball, :soma vs :d1 vs :d2 for Tripod)
//   - Pending queue + delayed delivery

///|
test "CompartmentSynapseBall::random: matrix shape and target" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(5, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(3, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "soma", 1.0F, 0.0F, 0.5F, rng)
  if c.matrix.nnz() == 0 {
    fail("expected non-zero connections (p=0.5, 5 pre × 3 post)")
  }
  if c.target != "soma" {
    fail("target = \{c.target}, expected soma")
  }
  if c.sym != "ge" {
    fail("sym = \{c.sym}, expected ge")
  }
}

///|
test "CompartmentSynapseBall: pre-fire onto :soma → glu_s accumulates" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(2, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "soma", 2.0F, 0.0F, 1.0F, rng)
  // Force pre[0] to fire.
  pre.fire[0] = true
  forward_compartment_ball(c, 0.0F)
  // All connections: weight = 2.0; both post[0] and post[1] get +2.0
  if (post.glu_s[0] - 2.0F).abs() > 1.0e-6F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 2.0")
  }
  if (post.glu_s[1] - 2.0F).abs() > 1.0e-6F {
    fail("glu_s[1] = \{post.glu_s[1]}, expected 2.0")
  }
  // :d should be untouched.
  if post.glu_d[0] != 0.0F {
    fail("glu_d[0] = \{post.glu_d[0]}, expected 0 (target was :soma)")
  }
  if post.glu_d[1] != 0.0F {
    fail("glu_d[1] = \{post.glu_d[1]}, expected 0 (target was :soma)")
  }
  // GABA channels should also stay 0.
  if post.gaba_s[0] != 0.0F || post.gaba_s[1] != 0.0F {
    fail("gaba_s should stay 0 for :ge stim")
  }
}

///|
test "CompartmentSynapseBall: target=:d routes to glu_d not glu_s" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(2, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "d", 1.5F, 0.0F, 1.0F, rng)
  pre.fire[0] = true
  forward_compartment_ball(c, 0.0F)
  if (post.glu_d[0] - 1.5F).abs() > 1.0e-6F {
    fail("glu_d[0] = \{post.glu_d[0]}, expected 1.5")
  }
  if (post.glu_d[1] - 1.5F).abs() > 1.0e-6F {
    fail("glu_d[1] = \{post.glu_d[1]}, expected 1.5")
  }
  if post.glu_s[0] != 0.0F || post.glu_s[1] != 0.0F {
    fail("glu_s should stay 0 when target is :d")
  }
}

///|
test "CompartmentSynapseBall: :gi sym routes to gaba buffers" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(2, soma_param, rng)
  let c_s = CompartmentSynapseBall::random(pre, post, "gi", "soma", 1.0F, 0.0F, 1.0F, rng)
  let c_d = CompartmentSynapseBall::random(pre, post, "gi", "d", 2.0F, 0.0F, 1.0F, rng)
  pre.fire[0] = true
  forward_compartment_ball(c_s, 0.0F)
  forward_compartment_ball(c_d, 0.0F)
  if (post.gaba_s[0] - 1.0F).abs() > 1.0e-6F {
    fail("gaba_s[0] = \{post.gaba_s[0]}, expected 1.0")
  }
  if (post.gaba_d[0] - 2.0F).abs() > 1.0e-6F {
    fail("gaba_d[0] = \{post.gaba_d[0]}, expected 2.0")
  }
  // glu_* stay 0.
  if post.glu_s[0] != 0.0F || post.glu_d[0] != 0.0F {
    fail("glu_* should stay 0 for :gi stim")
  }
}

///|
test "CompartmentSynapseBall: non-firing pre → no conductance change" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(2, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(3, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "soma", 1.0F, 0.0F, 1.0F, rng)
  // pre does not fire
  forward_compartment_ball(c, 0.0F)
  for j in 0..<3 {
    if post.glu_s[j] != 0.0F {
      fail("glu_s[\{j}] = \{post.glu_s[j]}, expected 0 (no pre-fire)")
    }
    if post.glu_d[j] != 0.0F {
      fail("glu_d[\{j}] = \{post.glu_d[j]}, expected 0 (no pre-fire)")
    }
  }
}

///|
test "CompartmentSynapseBall: pending queue delays delivery" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "soma", 5.0F, 0.0F, 1.0F, rng)
  // Inject a 5ms delay on the single connection.
  c.set_delays([5.0F])
  pre.fire[0] = true
  // Forward at t=0 → spike scheduled for delivery at t=5
  forward_compartment_ball(c, 0.0F)
  if post.glu_s[0] != 0.0F {
    fail("glu_s[0] = \{post.glu_s[0]} before delivery, expected 0")
  }
  // Drain at t=4 → still too early
  deliver_pending_compartment_ball(c, 4.0F)
  if post.glu_s[0] != 0.0F {
    fail("glu_s[0] = \{post.glu_s[0]} at t=4, expected 0 (delay=5)")
  }
  // Drain at t=5 → delivery
  deliver_pending_compartment_ball(c, 5.0F)
  if (post.glu_s[0] - 5.0F).abs() > 1.0e-5F {
    fail("glu_s[0] = \{post.glu_s[0]} at t=5, expected 5.0")
  }
}

///|
test "CompartmentSynapseTripod: target=:d2 routes to glu_d2" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = Tripod::new(2, soma_param, rng)
  let c = CompartmentSynapseTripod::random(pre, post, "ge", "d2", 2.5F, 0.0F, 1.0F, rng)
  pre.fire[0] = true
  forward_compartment_tripod(c, 0.0F)
  if (post.glu_d2[0] - 2.5F).abs() > 1.0e-6F {
    fail("glu_d2[0] = \{post.glu_d2[0]}, expected 2.5")
  }
  if (post.glu_d2[1] - 2.5F).abs() > 1.0e-6F {
    fail("glu_d2[1] = \{post.glu_d2[1]}, expected 2.5")
  }
  // Other dendrites stay 0
  if post.glu_s[0] != 0.0F || post.glu_d1[0] != 0.0F {
    fail("only d2 should receive weight; glu_s or glu_d1 leaked")
  }
}

///|
test "CompartmentSynapseTripod: target=:soma routes to glu_s" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = Tripod::new(1, soma_param, rng)
  let c = CompartmentSynapseTripod::random(pre, post, "ge", "soma", 1.0F, 0.0F, 1.0F, rng)
  pre.fire[0] = true
  forward_compartment_tripod(c, 0.0F)
  if (post.glu_s[0] - 1.0F).abs() > 1.0e-6F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 1.0")
  }
  if post.glu_d1[0] != 0.0F || post.glu_d2[0] != 0.0F {
    fail("dendrites should stay 0 when target is :soma")
  }
}

///|
test "CompartmentSynapseTripod: :gi sym writes to gaba_d1" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = Tripod::new(1, soma_param, rng)
  let c = CompartmentSynapseTripod::random(pre, post, "gi", "d1", 3.0F, 0.0F, 1.0F, rng)
  pre.fire[0] = true
  forward_compartment_tripod(c, 0.0F)
  if (post.gaba_d1[0] - 3.0F).abs() > 1.0e-6F {
    fail("gaba_d1[0] = \{post.gaba_d1[0]}, expected 3.0")
  }
  if post.glu_d1[0] != 0.0F || post.glu_s[0] != 0.0F || post.glu_d2[0] != 0.0F {
    fail("glu_* should stay 0 for :gi stim")
  }
}

///|
test "CompartmentSynapseTripod: pending queue delays delivery" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = Tripod::new(1, soma_param, rng)
  let c = CompartmentSynapseTripod::random(pre, post, "ge", "d1", 4.0F, 0.0F, 1.0F, rng)
  c.set_delays([3.0F])
  pre.fire[0] = true
  forward_compartment_tripod(c, 0.0F)
  deliver_pending_compartment_tripod(c, 1.0F)
  if post.glu_d1[0] != 0.0F {
    fail("glu_d1[0] = \{post.glu_d1[0]} at t=1, expected 0 (delay=3)")
  }
  deliver_pending_compartment_tripod(c, 3.0F)
  if (post.glu_d1[0] - 4.0F).abs() > 1.0e-5F {
    fail("glu_d1[0] = \{post.glu_d1[0]} at t=3, expected 4.0")
  }
}

///|
test "CompartmentSynapseBall: rho scaling applied to weights" {
  let rng = Xoshiro::default()
  let pre_param = IFParameter::new()
  let pre = IF::new(1, pre_param, rng)
  let soma_param = AdExParameter::new()
  let post = BallAndStick::new(1, soma_param, rng)
  let c = CompartmentSynapseBall::random(pre, post, "ge", "soma", 2.0F, 0.0F, 1.0F, rng)
  // Set rho to 0.5 → weight scaled by 0.5
  c.rho.push(0.5F)
  pre.fire[0] = true
  forward_compartment_ball(c, 0.0F)
  if (post.glu_s[0] - 1.0F).abs() > 1.0e-5F {
    fail("glu_s[0] = \{post.glu_s[0]}, expected 1.0 (weight 2.0 × rho 0.5)")
  }
}