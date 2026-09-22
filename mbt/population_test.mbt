// Tests for the heterogeneous population dispatcher (AnyPop).

///|
test "AnyPop sim — IF only" {
  let param = IFParameter::new()
  let rng = Xoshiro::default()
  let p = IF::new(3, param, rng)
  let pops : Array[AnyPop] = [IF_(p)]
  sim_any_pops_for(pops, 10.0F, 0.125F)
  // Just verify it ran (80 steps of v decay)
  if p.v[0].is_nan() {
    fail("v[0] is NaN after sim")
  }
  assert_eq(n_total(pops), 3)
}

///|
test "AnyPop sim — heterogeneous 5 populations" {
  let if_param = IFParameter::new()
  let adex_param = AdExParameter::new()
  let iz_param = IZParameter::rs()
  let ml_param = MorrisLecarParameter::new()
  let hh_param = HHParameter::new()
  let rng = Xoshiro::default()
  let if_pop = IF::new(2, if_param, rng)
  let adex_pop = AdEx::new(1, adex_param, rng)
  let iz_pop = IZ::new(1, iz_param, rng)
  let ml_pop = MorrisLecar::new(1, ml_param, rng)
  let hh_pop = HH::new(1, hh_param, rng)
  let pops : Array[AnyPop] = [IF_(if_pop), AdEx_(adex_pop), IZ_(iz_pop),
                               ML_(ml_pop), HH_(hh_pop)]
  sim_any_pops_for(pops, 10.0F, 0.125F)
  for p in pops {
    match p {
      IF_(x) => if x.v[0].is_nan() { fail("IF v NaN") }
      AdEx_(x) => if x.v[0].is_nan() { fail("AdEx v NaN") }
      AdExSinExp_(x) => if x.v[0].is_nan() { fail("AdExSinExp v NaN") }
      IZ_(x) => if x.v[0].is_nan() { fail("IZ v NaN") }
      ML_(x) => if x.v[0].is_nan() { fail("ML v NaN") }
      HH_(x) => if x.v[0].is_nan() { fail("HH v NaN") }
      Poisson_(_) => ()
      WC_(_) => ()
      HetRec_(_) => ()
    }
  }
  // Total neurons = 2 + 1 + 1 + 1 + 1 = 6
  assert_eq(n_total(pops), 6)
}

///|
test "AnyPop n_neurons" {
  let rng = Xoshiro::default()
  let if_pop = IF::new(5, IFParameter::new(), rng)
  let ml_pop = MorrisLecar::new(3, MorrisLecarParameter::new(), rng)
  let pops : Array[AnyPop] = [IF_(if_pop), ML_(ml_pop)]
  assert_eq(n_total(pops), 8)
}
