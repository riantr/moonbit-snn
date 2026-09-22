/// dump_params_test.mbt — tests for the dump.jl parameter port
/// (TripletRule, NLTAH, ClopathSTDP, ISTDP). These are struct-only
/// ports; no step functions exist in the original dump.jl.

///|
test "TripletRule::pfister_alltoall_full defaults match Julia" {
  let t = TripletRule::pfister_alltoall_full()
  assert_eq(t.a2_ltp, 5.0e-10F)
  assert_eq(t.a3_ltp, 6.2e-3F)
  assert_eq(t.a2_ltd, 7.0e-3F)
  assert_eq(t.a3_ltd, 2.3e-4F)
  assert_eq(t.tau_x, 101.0F)
  assert_eq(t.tau_y, 125.0F)
  assert_eq(t.tau_p, 16.8F)
  assert_eq(t.tau_m, 33.7F)
}

///|
test "TripletRule custom struct literal preserves every field" {
  let t : TripletRule = { a2_ltp: 0.0F, a3_ltp: 0.0F, a2_ltd: 0.0F,
    a3_ltd: 0.0F, tau_x: 1.0F, tau_y: 1.0F, tau_p: 1.0F, tau_m: 1.0F }
  assert_eq(t.tau_x, 1.0F)
  assert_eq(t.tau_m, 1.0F)
}

///|
test "NLTAH struct literal preserves every field" {
  let n : NLTAH = { tau: 50.0F, lambda_: 0.5F, mu: 1.0F }
  assert_eq(n.tau, 50.0F)
  assert_eq(n.lambda_, 0.5F)
  assert_eq(n.mu, 1.0F)
}

///|
test "ClopathSTDP::lkd defaults match Julia lkd_stdp literal" {
  let c = ClopathSTDP::lkd()
  assert_eq(c.a_ltd, 8.0e-5F)
  assert_eq(c.a_ltp, 14.0e-5F)
  assert_eq(c.theta_ltd, -70.0F)
  assert_eq(c.theta_ltp, -49.0F)
  assert_eq(c.tau_u, 10.0F)
  assert_eq(c.tau_v, 7.0F)
  assert_eq(c.tau_x, 15.0F)
  assert_eq(c.tau_1, 5.0F)
  // Invariant: inv_tau fields are reciprocals of tau fields.
  let inv_tau_u = 1.0F / c.tau_u
  let abs_diff = if c.inv_tau_u > inv_tau_u { c.inv_tau_u - inv_tau_u } else { inv_tau_u - c.inv_tau_u }
  if abs_diff > 1.0e-6F { fail("inv_tau_u does not match 1/tau_u") }
}

///|
test "ClopathSTDP::lkd — w_min + w_max bounds" {
  let c = ClopathSTDP::lkd()
  assert_eq(c.w_min, 1.78F)
  assert_eq(c.w_max, 21.0F)
}

///|
test "ClopathSTDP custom struct literal preserves every field" {
  let c : ClopathSTDP = { a_ltd: 0.0F, a_ltp: 0.0F, theta_ltd: -80.0F,
    theta_ltp: 0.0F, tau_s: 100.0F, tau_u: 5.0F, tau_v: 5.0F, tau_x: 5.0F,
    tau_1: 5.0F, eps: 0.0F, w_min: 0.0F, w_max: 100.0F, inv_tau_u: 0.2F,
    inv_tau_v: 0.2F, inv_tau_x: 0.2F, inv_tau_1: 0.2F }
  assert_eq(c.theta_ltd, -80.0F)
  assert_eq(c.tau_s, 100.0F)
}

///|
test "ISTDP::new defaults match Julia ISTDP struct" {
  let i = ISTDP::new()
  assert_eq(i.eta, 0.2F)
  assert_eq(i.r0, 0.01F)
  assert_eq(i.vd, -70.0F)
  // Julia's τd is Float64 (matches Double in MoonBit).
  assert_eq(i.tau_d, 5.0)
  assert_eq(i.tau_y, 20.0F)
  assert_eq(i.w_min, 2.78F)
  assert_eq(i.w_max, 243.0F)
  // α = 2 * r0 * tau_y = 2 * 0.01 * 20 = 0.4 (within Float32 tolerance).
  let alpha_diff = if i.alpha > 0.4F { i.alpha - 0.4F } else { 0.4F - i.alpha }
  if alpha_diff > 1.0e-5F { fail("ISTDP alpha should be ~0.4F") }
}

///|
test "ISTDP::vogels factory matches Julia vogels_istdp()" {
  let i = ISTDP::vogels()
  assert_eq(i.eta, 1.0F)
  assert_eq(i.r0, 0.005F)
  assert_eq(i.tau_y, 20.0F)
  assert_eq(i.w_min, 48.7F)
  assert_eq(i.w_max, 243.0F)
  // α = 2 * 0.005 * 20 = 0.2 (within Float32 tolerance).
  let alpha_diff = if i.alpha > 0.2F { i.alpha - 0.2F } else { 0.2F - i.alpha }
  if alpha_diff > 1.0e-5F { fail("ISTDP::vogels alpha should be ~0.2F") }
}

///|
test "ISTDP custom struct literal preserves every field" {
  let i : ISTDP = { eta: 0.5F, r0: 0.05F, vd: -65.0F, tau_d: 10.0,
    tau_y: 30.0F, alpha: 3.0F, w_min: 1.0F, w_max: 100.0F }
  assert_eq(i.eta, 0.5F)
  assert_eq(i.r0, 0.05F)
  assert_eq(i.vd, -65.0F)
  assert_eq(i.w_max, 100.0F)
}