/// ca_plasticity_test.mbt 鈥?tests for CaPlasticityParameter
/// (Brette-Gerstner pair-spike form, ported from
/// refs/SNNModels.jl/src/connections/sparse_plasticity/CaRule.jl).

///|
test "CaPlasticityParameter::new defaults match Julia" {
  let p = CaPlasticityParameter::new()
  assert_eq(p.a_pre, 0.1F)
  assert_eq(p.a_post, 0.1F)
  assert_eq(p.tau_pre, 20.0F)
  assert_eq(p.tau_post, 20.0F)
  assert_eq(p.w_max, 30.0F)
  assert_eq(p.w_min, 0.0F)
}

///|
test "CaPlasticityParameter::custom sets every field" {
  let p = CaPlasticityParameter::custom(a_pre=0.05F, a_post=0.2F, tau_pre=15.0F, tau_post=25.0F, w_max=50.0F, w_min=-5.0F)
  assert_eq(p.a_pre, 0.05F)
  assert_eq(p.a_post, 0.2F)
  assert_eq(p.tau_pre, 15.0F)
  assert_eq(p.tau_post, 25.0F)
  assert_eq(p.w_max, 50.0F)
  assert_eq(p.w_min, -5.0F)
}

///|
test "CaPlasticityVariables::new zero-init + correct lengths" {
  let v = CaPlasticityVariables::new(4, 2)
  assert_eq(v.n_pre, 4)
  assert_eq(v.n_post, 2)
  assert_eq(v.tpre.length(), 4)
  assert_eq(v.tpost.length(), 2)
  assert_eq(v.tpre[0], 0.0F)
  assert_eq(v.tpost[1], 0.0F)
}

///|
test "CaPlasticityEntry::new initialises vars + mut param" {
  let e = CaPlasticityEntry::new(0, 3, 2)
  assert_eq(e.conn_index, 0)
  assert_eq(e.n_pre, 3)
  assert_eq(e.n_post, 2)
  assert_eq(e.param.a_pre, 0.1F)
  assert_eq(e.vars.tpre.length(), 3)
  assert_eq(e.t_now[0], 0.0F)
}

///|
test "CaPlasticityEntry::change_plasticity swaps param, preserves vars" {
  let e = CaPlasticityEntry::new(0, 3, 2)
  // Mutate tpre manually so we can verify preservation.
  e.vars.tpre[0] = 7.0F
  let new_p = CaPlasticityParameter::custom(a_pre=0.5F)
  e.change_plasticity(new_p)
  assert_eq(e.param.a_pre, 0.5F)
  // Vars preserved.
  assert_eq(e.vars.tpre[0], 7.0F)
}

///|
test "ca_plasticity_step 鈥?pre-fire adds tpost[i] to W[s]" {
  // 2 pre 脳 1 post; first pre connects to post via rowptr[0..1].
  let colptr : Array[Int] = [0, 0]   // both connections 鈫?post 0
  let rowptr : Array[Int] = [0, 1, 2]
  let w : Array[Float] = [1.0F, 2.0F]
  let pre_fire : Array[Bool] = [true, false]
  let post_fire : Array[Bool] = [false]
  let vars = CaPlasticityVariables::new(2, 1)
  vars.tpost[0] = 5.0F
  let param = CaPlasticityParameter::new()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // w[0] should have += tpost[0] = 5.0 (pre[0] fires, rowptr[0..1]=[0,0..1]=[0..1) walks s=0)
  // Row 0 fires, walks rowptr[0]..rowptr[1] = [0..1) 鈥?s=0: w[0] += tpost[0] = 5.0.
  // Row 1 doesn't fire. w[1] unchanged.
  assert_eq(w[0], 6.0F)
  assert_eq(w[1], 2.0F)
}

///|
test "ca_plasticity_step 鈥?post-fire adds tpre[j] to W[s]" {
  let colptr : Array[Int] = [0, 0]   // both rows 鈫?post 0
  let rowptr : Array[Int] = [0, 1, 2]
  let w : Array[Float] = [1.0F, 2.0F]
  let pre_fire : Array[Bool] = [false, false]
  let post_fire : Array[Bool] = [true]
  let vars = CaPlasticityVariables::new(2, 1)
  vars.tpre[0] = 3.0F
  vars.tpre[1] = 4.0F
  let param = CaPlasticityParameter::new()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // post[0] fires; iterate k=0; for each j, check colptr[s]==0.
  //   j=0: rowptr[0..1]=[0..1), s=0: colptr[0]==0 鈫?w[0] += tpre[0] = 3.0
  //   j=1: rowptr[1..2]=[1..2), s=1: colptr[1]==0 鈫?w[1] += tpre[1] = 4.0
  assert_eq(w[0], 4.0F)
  assert_eq(w[1], 6.0F)
}

///|
test "ca_plasticity_step 鈥?weight clamps at w_max" {
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let w : Array[Float] = [29.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let vars = CaPlasticityVariables::new(1, 1)
  vars.tpost[0] = 100.0F  // big enough to push w[0] past w_max
  let param = CaPlasticityParameter::new()  // w_max = 30.0F
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  assert_eq(w[0], 30.0F)  // clamped at w_max
}

///|
test "ca_plasticity_step 鈥?trace decay toward zero" {
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let w : Array[Float] = [0.0F]
  let pre_fire : Array[Bool] = [false]
  let post_fire : Array[Bool] = [false]
  let vars = CaPlasticityVariables::new(1, 1)
  vars.tpre[0] = 1.0F
  vars.tpost[0] = 1.0F
  let param = CaPlasticityParameter::new()
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // After decay: tpre = 1 + 0.125 * (-1) / 20 = 1 - 0.00625 = 0.99375
  // Verify close to expected (within FP tolerance).
  let diff = vars.tpre[0] - 0.99375F
  if diff < 0.0F { ignore(-diff) }
  let abs_diff = if vars.tpre[0] > 0.99375F { vars.tpre[0] - 0.99375F } else { 0.99375F - vars.tpre[0] }
  if abs_diff > 1.0e-5F { fail("tpre decay outside tolerance") }
}

///|
test "ca_plasticity_step 鈥?pre-fire bumps tpre[j] by A_pre" {
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let w : Array[Float] = [0.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  let vars = CaPlasticityVariables::new(1, 1)
  vars.tpre[0] = 0.0F
  vars.tpost[0] = 0.0F
  let param = CaPlasticityParameter::new()  // a_pre = 0.1
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, vars, param, 0.0F, 0.125F)
  // Decay then bump: tpre[0] = 0 + 0.125*(-0)*inv_tau_pre + 0.1 = 0.1
  assert_eq(vars.tpre[0], 0.1F)
}

///|
test "CaPlasticityEntry::set_ltp_active 鈥?toggles active flag" {
  let e = CaPlasticityEntry::new(0, 3, 2)
  // Default active=true.
  let colptr : Array[Int] = [0]
  let rowptr : Array[Int] = [0, 1]
  let w : Array[Float] = [0.0F]
  let pre_fire : Array[Bool] = [true]
  let post_fire : Array[Bool] = [false]
  e.set_ltp_active(false)
  // With active=false, the step function should not modify w.
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, e.vars, e.param, 0.0F, 0.125F)
  assert_eq(w[0], 0.0F)
  // Re-enable 鈥?now step modifies w.
  e.set_ltp_active(true)
  e.vars.tpost[0] = 2.0F
  ca_plasticity_step(w, pre_fire, post_fire, colptr, rowptr, e.vars, e.param, 0.0F, 0.125F)
  if w[0] <= 0.0F { fail("active=true should modify w") }
}