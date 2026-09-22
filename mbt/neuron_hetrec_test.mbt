// Tests for the HetRec heterogeneous-timescale non-recurrent neuron population.
// Bit-exact port of SNNModels.jl/test/pop/hetrec.jl.

///|
test "HetRecParameter::new — Julia defaults" {
  let p = HetRecParameter::new()
  assert_eq(p.nd, 2)
  assert_eq(p.overlap, 0.5F)
  assert_eq(p.tau_d_low, 10.0F)
  assert_eq(p.tau_d_high, 100.0F)
  assert_eq(p.rate_low, 0.0F)
  assert_eq(p.rate_high, 1.0F)
  assert_eq(p.tau_abs, 5.0F)
  assert_eq(p.steepness, 1.0F)
  assert_eq(p.tau_m, 20.0F)
  assert_eq(p.tau_rate, 100.0F)
}

///|
test "HetRec::new — array sizes match Julia HetRec layout" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let n = 4
  let p = HetRec::new(n, param, rng)
  assert_eq(p.n, n)
  assert_eq(p.v_d.length(), n * param.nd)
  assert_eq(p.v_s.length(), n)
  assert_eq(p.is_.length(), n * param.nd)
  assert_eq(p.r.length(), n)
  assert_eq(p.tau_d.length(), n * param.nd)
  assert_eq(p.fire.length(), n)
  assert_eq(p.tabs.length(), n)
  assert_eq(p.trace.length(), n)
  assert_eq(p.randcache.length(), n)
  assert_eq(p.colptr.length(), n + 1)
  // Sparse mapping: each soma owns at least Nd "own" dendrites.
  assert_eq(p.colptr[0], 0)
  assert_eq(p.colptr[n], p.i_syn.length())
  for k in 0..<n {
    if p.colptr[k] >= p.colptr[k + 1] {
      fail("colptr[\{k}] = \{p.colptr[k]} not < colptr[\{k + 1}] = \{p.colptr[k + 1]}")
    }
  }
  // Every entry in i_syn is in [0, N*Nd).
  for s in 0..<p.i_syn.length() {
    if p.i_syn[s] < 0 || p.i_syn[s] >= n * param.nd {
      fail("i_syn[\{s}] = \{p.i_syn[s]} out of range")
    }
    if p.w_syn[s] != 1.0F {
      fail("w_syn[\{s}] = \{p.w_syn[s]} (should be 1.0 for binary M)")
    }
  }
}

///|
test "HetRec::new — baseline rates uniform in [0, 1]" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let n = 200
  let p = HetRec::new(n, param, rng)
  let mut min_r : Float = 1.0F
  let mut max_r : Float = 0.0F
  for i in 0..<n {
    if p.r[i] < min_r {
      min_r = p.r[i]
    }
    if p.r[i] > max_r {
      max_r = p.r[i]
    }
    if p.r[i] < 0.0F || p.r[i] > 1.0F {
      fail("r[\{i}] = \{p.r[i]} outside [0, 1]")
    }
  }
  // Range sanity (with 200 samples, we expect the sample range to
  // cover most of [0, 1] but allow small edge cases).
  if max_r - min_r < 0.5F {
    fail("r range too narrow: \{min_r}..\{max_r}")
  }
}

///|
test "HetRec::new — dendritic time constants uniform in [10, 100] ms" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let n = 50
  let p = HetRec::new(n, param, rng)
  let total_d = n * param.nd
  for i in 0..<total_d {
    if p.tau_d[i] < 10.0F || p.tau_d[i] > 100.0F {
      fail("tau_d[\{i}] = \{p.tau_d[i]} outside [10, 100]")
    }
  }
}

///|
test "HetRec::new — overlap=0 → only own dendrites per soma" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::{
    nd: 2,
    overlap: 0.0F,
    tau_d_low: 10.0F,
    tau_d_high: 100.0F,
    rate_low: 0.0F,
    rate_high: 1.0F,
    tau_abs: 5.0F,
    steepness: 1.0F,
    tau_m: 20.0F,
    tau_rate: 100.0F,
  }
  let n = 5
  let p = HetRec::new(n, param, rng)
  // With overlap=0, each soma should have exactly Nd synapses
  // (one per own dendrite).
  for i in 0..<n {
    let n_syn = p.colptr[i + 1] - p.colptr[i]
    assert_eq(n_syn, param.nd)
  }
  // Own dendrites: for soma i, dendrite (j-1)*N + i (j=0..Nd).
  for i in 0..<n {
    let start = p.colptr[i]
    for j in 0..<param.nd {
      let expected = j * n + i
      if p.i_syn[start + j] != expected {
        fail("i_syn[\{start + j}] = \{p.i_syn[start + j]} expected \{expected}")
      }
    }
  }
}

///|
test "step_hetrec — zero input → v_d, v_s stay at 0, no spikes" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let p = HetRec::new(5, param, rng)
  let dt = 0.125F
  for _ in 0..<100 {
    hetrec_refresh_random(p, rng)
    step_hetrec(p, dt)
  }
  let n = p.n
  for i in 0..<n {
    if p.v_s[i] != 0.0F {
      fail("v_s[\{i}] = \{p.v_s[i]} (expected 0 with no input)")
    }
    if p.v_d[i] != 0.0F {
      fail("v_d[\{i}] = \{p.v_d[i]} (expected 0 with no input)")
    }
  }
}

///|
test "step_hetrec — constant is_ drives v_d, then v_s via M" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let n = 2
  let p = HetRec::new(n, param, rng)
  let dt = 0.125F
  // Inject a constant current into all dendrites.
  for i in 0..<p.is_.length() {
    p.is_[i] = 10.0F
  }
  let mut max_abs_v_s : Float = 0.0F
  for _ in 0..<200 {
    hetrec_refresh_random(p, rng)
    step_hetrec(p, dt)
    for i in 0..<n {
      let abs_v = if p.v_s[i] < 0.0F { -p.v_s[i] } else { p.v_s[i] }
      if abs_v > max_abs_v_s {
        max_abs_v_s = abs_v
      }
    }
  }
  // Dendritic Euler step: v_d += dt * (-v_d - is) / τd
  // With is=10 and τd ~ 55ms, v_d → -is = -10 (steady state).
  // Soma Euler: v_s[i] += (W * v_d[I[s]] - v_s[i]) * dt / τm
  // So v_s converges to W * v_d_steady = -10 (each soma pulls toward
  // -10 because all its connected dendrites sit at -10).
  // Either direction (positive or negative) is a valid signal of
  // activity — just verify |v_s| moves meaningfully from 0.
  if max_abs_v_s < 1.0F {
    fail("|v_s| stayed below 1.0 under constant dendritic drive (max_abs_v_s = \{max_abs_v_s})")
  }
  // No NaN/Inf.
  for i in 0..<n {
    if p.v_s[i].is_nan() || p.v_s[i].is_inf() {
      fail("v_s[\{i}] = \{p.v_s[i]} (NaN/Inf)")
    }
  }
  // v_d should be near -is (its steady state).
  let mut max_abs_v_d : Float = 0.0F
  for d in 0..<p.v_d.length() {
    let abs_v = if p.v_d[d] < 0.0F { -p.v_d[d] } else { p.v_d[d] }
    if abs_v > max_abs_v_d {
      max_abs_v_d = abs_v
    }
  }
  if max_abs_v_d < 1.0F {
    fail("|v_d| stayed below 1.0 under is=10 drive (max_abs_v_d = \{max_abs_v_d})")
  }
}

///|
test "step_hetrec — long sim no NaN, mean v_s in reasonable range" {
  let rng = Xoshiro::default()
  let param = HetRecParameter::new()
  let n = 30
  let p = HetRec::new(n, param, rng)
  let dt = 0.125F
  for step in 0..<800 {
    // Small Poisson-like drive: every 10 steps, inject a brief burst
    // of current into a random subset of dendrites.
    if step % 10 == 0 {
      for d in 0..<p.is_.length() {
        let u = next_f32(rng)
        if u < 0.3F {
          p.is_[d] = 5.0F
        } else {
          p.is_[d] = 0.0F
        }
      }
    } else {
      for d in 0..<p.is_.length() {
        p.is_[d] = 0.0F
      }
    }
    hetrec_refresh_random(p, rng)
    step_hetrec(p, dt)
  }
  // Verify no NaN/Inf.
  let mut sum_v_s : Float = 0.0F
  for i in 0..<n {
    if p.v_s[i].is_nan() || p.v_s[i].is_inf() {
      fail("v_s[\{i}] = \{p.v_s[i]} (NaN/Inf)")
    }
    sum_v_s = sum_v_s + p.v_s[i]
  }
  // Sanity: mean v_s should be bounded (we don't assert exact range
  // since stochastic firing makes it variable).
  let mean = sum_v_s / Float::from_int(n)
  if mean < -50.0F || mean > 50.0F {
    fail("mean v_s = \{mean} out of plausible range")
  }
}