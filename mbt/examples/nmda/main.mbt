// nmda.mbt — port of SNNModels.jl/src/populations/synapse/receptors.jl
// (NMDA voltage-dependent gating + 2-state synaptic ODE).
//
// Exercises:
//   - NMDA gating B(v) = 1/(1 + (mg/b)*exp(k*v)) at multiple voltages.
//   - 2-state ODE: g decay + h rise after a single pulse.
//   - NMDA modulation: same synaptic input → smaller current at -80 mV
//     (hyperpolarized, Mg block) vs -20 mV (released block).
//
// Reference Julia (somaNMDA defaults): mg=1.0 mM, b=3.57, k=-0.062.

///|
fn main {
  let nmda = @snn_mbt.NMDAVoltageDependency::soma()  // mg=1, b=3.57, k=-0.062

  println("nmda.mbt: NMDA voltage-dependent gating + 2-state ODE demo")
  println("")
  println("=== NMDA gating B(v) sweep ===")
  println("  v (mV)    B(v)")
  for v in [-100.0F, -80.0F, -65.0F, -50.0F, -30.0F, -10.0F, 20.0F, 50.0F] {
    let b = @snn_mbt.nmda_gating(v, nmda)
    println("  " + v.to_string() + "      " + b.to_string())
  }

  println("")
  println("=== 2-state ODE decay ===")
  // One AMPA-like pulse (τr=1, τd=6, g0=1).
  let ampa = @snn_mbt.Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let g : Array[Float] = [0.0F]
  let h : Array[Float] = [0.0F]
  let target : Array[Float] = [10.0F]
  // Single pulse at t=0.
  @snn_mbt.step_receptor(g, h, target, ampa, 0.125F)
  let g_peak = g[0]
  println("  after 1 step (dt=0.125 ms, target=10): g[0] = " + g_peak.to_string())
  // Run 200 more steps with no input — g should decay.
  for k in 0..<200 {
    let empty : Array[Float] = [0.0F]
    @snn_mbt.step_receptor(g, h, empty, ampa, 0.125F)
    if k == 24 || k == 49 || k == 99 || k == 199 {
      let t_ms = Float::from_int(k) + 1.0F * 0.125F
      println("  after " + t_ms.to_string() + " ms (step " + (k + 1).to_string() + "): g[0] = " + g[0].to_string())
    }
  }

  println("")
  println("=== NMDA modulation: I_syn at v=-80 vs v=-20 mV ===")
  let nmda_rec = @snn_mbt.Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let g2 : Array[Float] = [1.0F]
  let v_hyp : Array[Float] = [-80.0F]
  let v_dep : Array[Float] = [-20.0F]
  let out_hyp : Array[Float] = [0.0F]
  let out_dep : Array[Float] = [0.0F]
  @snn_mbt.receptor_current(g2, v_hyp, nmda_rec, nmda, out_hyp)
  @snn_mbt.receptor_current(g2, v_dep, nmda_rec, nmda, out_dep)
  println("  v=-80 mV: I = " + out_hyp[0].to_string())
  println("  v=-20 mV: I = " + out_dep[0].to_string())
  let ratio = if out_hyp[0].abs() > 1.0e-6F {
    out_dep[0].abs() / out_hyp[0].abs()
  } else {
    0.0F
  }
  println("  |I_dep| / |I_hyp| = " + ratio.to_string())
  println("")
  if ratio > 2.0F {
    println("✓ NMDA modulation confirmed: depolarized v yields much larger")
    println("  synaptic current (Mg block released); hyperpolarized v")
    println("  shows strong attenuation.")
  } else {
    println("note: NMDA modulation weaker than expected; check mg/b/k values")
  }
}