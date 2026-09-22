// tripod_het.mbt — minimal example of TripodHet.
//
// Julia reference (paraphrased):
//   pre = IF(N=10)
//   post = TripodHet(N=4, param=AdExParameterHet{Vector{Float32}})
//   monitor!(post, [:fire])
//   sim!(model, 1s)
//
// MoonBit (v0.10.24):
//   - 4 TripodHet neurons (soma + 2 dendrites each).
//   - Per-neuron AdEx params: vt ∈ {-50, -50, -30, -30} mV.
//     First two neurons have low threshold (spike easily);
//     last two have high threshold (don't spike with same drive).
//   - 200 pA tonic drive on soma for 500 ms.
//   - Reports spike count per neuron; verifies the heterogeneity
//     matters (low-vt neurons fire, high-vt don't).
//
// Bit-exact note: per-neuron thresholds affect spike timing.
// Qualitative match to Julia's `TripodHet{N=4, AdExParameter{Vector}}`.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  let n = 4
  let base = @snn_mbt.AdExParameter::new()
  // Per-neuron vt: neuron 0/1 are excitable (-50), neuron 2/3 are not (-30).
  let vt_arr : Array[Float] = [-50.0F, -50.0F, -30.0F, -30.0F]
  let vr_arr : Array[Float] = [base.vr, base.vr, base.vr, base.vr]
  let el_arr : Array[Float] = [base.el, base.el, base.el, base.el]
  let tm_arr : Array[Float] = [base.tm, base.tm, base.tm, base.tm]
  let r_arr : Array[Float] = [base.r, base.r, base.r, base.r]
  let dt_slope_arr : Array[Float] = [base.dt_slope, base.dt_slope, base.dt_slope, base.dt_slope]
  let tw_arr : Array[Float] = [base.tw, base.tw, base.tw, base.tw]
  let a_arr : Array[Float] = [base.a, base.a, base.a, base.a]
  let b_arr : Array[Float] = [base.b, base.b, base.b, base.b]
  let param : @snn_mbt.AdExParameterHet = {
    vt: vt_arr,
    vr: vr_arr,
    el: el_arr,
    tm: tm_arr,
    r: r_arr,
    dt_slope: dt_slope_arr,
    tw: tw_arr,
    a: a_arr,
    b: b_arr,
  }
  let pop : @snn_mbt.TripodHet = @snn_mbt.TripodHet::new(n, param, rng)

  // 1500 pA tonic drive on soma of every neuron (strong enough to fire
  // excitable low-vt neurons; not enough to overcome vt=-30 high-vt).
  for k in 0..<n {
    pop.i_s[k] = 1500.0F
  }

  println("tripod_het.mbt: " + n.to_string() + " TripodHet neurons, " + "1500 pA tonic soma drive")
  println("  per-neuron vt = " + param.vt[0].to_string() + ", " + param.vt[1].to_string() +
    ", " + param.vt[2].to_string() + ", " + param.vt[3].to_string() + " mV")

  // Drive 500 ms.
  let duration_ms = 500.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut n0 = 0
  let mut n1 = 0
  let mut n2 = 0
  let mut n3 = 0
  for _step in 0..<steps {
    let _ = _step
    @snn_mbt.step_tripod_het(pop, dt)
    if pop.fire[0] { n0 = n0 + 1 }
    if pop.fire[1] { n1 = n1 + 1 }
    if pop.fire[2] { n2 = n2 + 1 }
    if pop.fire[3] { n3 = n3 + 1 }
  }
  println("")
  println("After " + duration_ms.to_string() + " ms sim:")
  println("  neuron 0 (vt=-50) spikes = " + n0.to_string())
  println("  neuron 1 (vt=-50) spikes = " + n1.to_string())
  println("  neuron 2 (vt=-30) spikes = " + n2.to_string())
  println("  neuron 3 (vt=-30) spikes = " + n3.to_string())
  println("")
  if n0 > 0 && n1 > 0 && n2 == 0 && n3 == 0 {
    println("✓ heterogeneity check passed: low-vt neurons fire, high-vt don't")
  } else if n0 == 0 || n1 == 0 {
    println("note: low-vt neurons didn't fire; try higher drive")
  } else if n2 > 0 || n3 > 0 {
    println("note: high-vt neurons fired; consider vt closer to vr")
  }
  // Print final state of one neuron for sanity.
  println("")
  println("Final state of neuron 0:")
  println("  v_s = " + pop.v_s[0].to_string() + " mV")
  println("  v_d1 = " + pop.v_d1[0].to_string() + " mV")
  println("  v_d2 = " + pop.v_d2[0].to_string() + " mV")
  println("  w_s = " + pop.w_s[0].to_string() + " nS")
}