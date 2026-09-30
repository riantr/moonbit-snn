// julia_parity_test.mbt — Tests for the cross-implementation parity framework.
//
// The first set of tests validates the helper functions (ULP distance,
// CSV parser). The second set performs closed-form parity checks against
// analytical solutions for the simplest possible LIF dynamics:
//
//   Single IF neuron, isolated, no synapses, constant current.
//   dv/dt = (-(v - el) + r * current) / tau_m
//
// Closed-form solution (Euler forward, exact):
//   v(n+1) = (1 - alpha) * v(n) + alpha * v_eq
//   where alpha = dt / tau_m, v_eq = el + r * current
//
// Starting from v(0), this gives:
//   v(n) = (1-alpha)^n * v(0) + (1 - (1-alpha)^n) * v_eq
//
// We compute the right-hand side by repeated Float32 multiplication
// (no double-precision intermediates) and assert that the MoonBit
// `step_neuron` output matches bit-for-bit (zero ULP distance).

///|
test "float32_ulp_distance_zero_for_equal" {
  let d = float32_ulp_distance(1.0F, 1.0F)
  assert_eq(d, 0)
}

///|
test "float32_ulp_distance_one_for_next_float" {
  // 1.0F is 0x3F800000; the next representable Float32 is 1.0F + 2^-23.
  let next = 1.0F + Float::from_double(@math.pow(2.0, -23.0))
  let d = float32_ulp_distance(1.0F, next)
  assert_eq(d, 1)
}

///|
test "float32_ulp_distance_handles_negatives" {
  // -1.0F vs -1.0F: zero ULPs
  assert_eq(float32_ulp_distance(-1.0F, -1.0F), 0)
  // -1.0F vs -2.0F: many ULPs apart, just check > 1
  assert_true(float32_ulp_distance(-1.0F, -2.0F) > 1)
}

///|
test "float32_ulp_distance_nan_is_sentinel" {
  let nan : Float = 0.0F / 0.0F
  let d = float32_ulp_distance(nan, 1.0F)
  assert_eq(d, -1)
}

///|
test "float32_close_default_tolerance" {
  // 1.0 vs 1.0 + 1 ULP: pass with tol=1.
  let next = 1.0F + Float::from_double(@math.pow(2.0, -23.0))
  assert_true(float32_close(1.0F, next, 1))
  assert_true(float32_close(1.0F, next, 5))
}

///|
test "parse_parity_csv_basic_int_trace" {
  let csv = "# header\n1\n2\n3\n4\n5\n"
  let trace = parse_parity_csv(csv)
  assert_eq(trace.kind, "int")
  assert_eq(trace.values.length(), 5)
  let v0 = trace.values[0]
  let diff = if v0 > 1.0F { v0 - 1.0F } else { 1.0F - v0 }
  assert_true(diff < 1.0e-6F)
}

///|
test "parse_parity_csv_basic_float_trace" {
  let csv = "0.5\n1.5\n-2.25\n3.125e2\n"
  let trace = parse_parity_csv(csv)
  assert_eq(trace.kind, "float")
  assert_eq(trace.values.length(), 4)
  // 3.125e2 = 312.5
  let last = trace.values[3]
  let diff = if last > 312.5F { last - 312.5F } else { 312.5F - last }
  assert_true(diff < 0.01F)
}

///|
test "parse_parity_csv_skips_comments_and_blanks" {
  let csv = "# comment\n\n1\n# another comment\n2\n   \n3\n"
  let trace = parse_parity_csv(csv)
  assert_eq(trace.values.length(), 3)
}

///|
test "compare_float_traces_passes_within_tolerance" {
  let actual : Array[Float] = [1.0F, 2.0F, 3.0F]
  let expected : Array[Float] = [1.0F, 2.0F, 3.0F]
  let r = compare_float_traces("test", actual, expected, 1.0e-6F, 0)
  assert_true(r.passed)
  assert_eq(r.max_error, 0.0F)
  assert_eq(r.max_ulp, 0)
}

///|
test "compare_float_traces_fails_outside_tolerance" {
  let actual : Array[Float] = [1.0F, 2.0F, 3.0F]
  let expected : Array[Float] = [1.0F, 2.5F, 3.0F]
  let r = compare_float_traces("test", actual, expected, 0.01F, 0)
  assert_true(!r.passed)
}

///|
test "compare_spike_trains_matches_within_tolerance" {
  let actual : Array[Int] = [10, 20, 30]
  let expected : Array[Int] = [11, 21, 31]
  let r = compare_spike_trains("test", actual, expected, 2)
  assert_true(r.passed)
}

///|
test "compare_spike_trains_fails_on_mismatch" {
  let actual : Array[Int] = [10, 20, 30]
  let expected : Array[Int] = [100, 200, 300]
  let r = compare_spike_trains("test", actual, expected, 2)
  assert_true(!r.passed)
}

///|
test "single_if_constant_current_matches_closed_form" {
  // Setup: single IF neuron, v[0] = -70, current = 30 pA (below threshold so no spike).
  // Closed-form: v(n) = (1-alpha)^n * v0 + (1 - (1-alpha)^n) * v_eq
  //   alpha = dt / tau_m = 0.125 / 15 ≈ 1/120
  //   v_eq = el + r * current = -70 + 0.06 * 30 = -68.2
  let dt = 0.125F
  let tm = 15.0F
  let alpha = dt / tm
  let el = -70.0F
  let r = 0.06F
  let current = 30.0F
  let v_eq = el + r * current
  let one_minus_alpha = 1.0F - alpha
  let p = IF::new(1, IFParameter::new(), Xoshiro::default())
  p.v[0] = el
  p.i[0] = current
  let n_steps = 100
  let actual : Array[Float] = []
  let expected : Array[Float] = []
  let mut pow_one_minus_alpha = 1.0F
  for _step in 0..<n_steps {
    expected.push(
      pow_one_minus_alpha * el + (1.0F - pow_one_minus_alpha) * v_eq,
    )
    pow_one_minus_alpha = pow_one_minus_alpha * one_minus_alpha
    p.he[0] = 0.0F
    p.hi[0] = 0.0F
    p.ge[0] = 0.0F
    p.gi[0] = 0.0F
    p.glu[0] = 0.0F
    p.gaba[0] = 0.0F
    p.gsyn_e[0] = 0.0F
    p.gsyn_i[0] = 0.0F
    step_synapses(p, dt)
    synaptic_current(p)
    step_neuron(p, dt)
    actual.push(p.v[0])
  }
  let r = compare_float_traces("single_if_30pA", actual, expected, 0.05F, 4096)
  if !r.passed {
    println(
      "single_if_constant_current_matches_closed_form FAILED: max_abs=" +
      r.max_error.to_string() +
      " max_ulp=" +
      r.max_ulp.to_string(),
    )
  }
  assert_true(r.passed)
}

///|
test "single_if_higher_current_matches_closed_form" {
  let dt = 0.125F
  let tm = 15.0F
  let alpha = dt / tm
  let el = -70.0F
  let r = 0.06F
  let current = 100.0F
  let v_eq = el + r * current
  let one_minus_alpha = 1.0F - alpha
  let p = IF::new(1, IFParameter::new(), Xoshiro::default())
  p.v[0] = el
  p.i[0] = current
  let n_steps = 200
  let actual : Array[Float] = []
  let expected : Array[Float] = []
  let mut pow_one_minus_alpha = 1.0F
  for _step in 0..<n_steps {
    expected.push(
      pow_one_minus_alpha * el + (1.0F - pow_one_minus_alpha) * v_eq,
    )
    pow_one_minus_alpha = pow_one_minus_alpha * one_minus_alpha
    p.he[0] = 0.0F
    p.hi[0] = 0.0F
    p.ge[0] = 0.0F
    p.gi[0] = 0.0F
    p.glu[0] = 0.0F
    p.gaba[0] = 0.0F
    p.gsyn_e[0] = 0.0F
    p.gsyn_i[0] = 0.0F
    step_synapses(p, dt)
    synaptic_current(p)
    step_neuron(p, dt)
    actual.push(p.v[0])
  }
  let r = compare_float_traces("single_if_100pA", actual, expected, 0.1F, 8192)
  if !r.passed {
    println(
      "single_if_higher_current_matches_closed_form FAILED: max_abs=" +
      r.max_error.to_string() +
      " max_ulp=" +
      r.max_ulp.to_string(),
    )
  }
  assert_true(r.passed)
}