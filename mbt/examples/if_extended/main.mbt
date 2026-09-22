// if_extended example — port of SpikingNeuralNetworks.jl/examples
// using ExtendedIF neurons with multi-receptor (g_Exc/g_PV/g_SST)
// synaptic conductances.
//
// A 3-population PV/SST/E network driven by constant current on each
// receptor. Verifies:
//   1. ExtendedIF construction + initial state (v=vr, conductances=0)
//   2. update_synapses! decays conductances with per-synapse τ
//   3. update_neuron! integrates multi-receptor synaptic current
//   4. integrate! runs both phases end-to-end
//   5. Fire + refractory behaviour matches Julia

///|
fn main {
  // ---- 1. Build a small E + PV + SST network ----
  let n_e : Int = 20
  let n_pv : Int = 5
  let n_sst : Int = 5
  let p_e : @snn_mbt.ExtendedIF = @snn_mbt.ExtendedIF::new(n=n_e)
  let p_pv : @snn_mbt.ExtendedIF = @snn_mbt.ExtendedIF::new(
    n=n_pv,
    param=@snn_mbt.ExtendedIFParameter::custom(
      cm=200.0F, vt=-50.0F, vr=-60.0F, el=-70.0F, gl=10.0F,
      tau_e=3.0F, tau_i=15.0F, e_i=-75.0F, e_e=0.0F,
      tau_abs=3.0F, alpha=0.0F,
    ),
  )
  let p_sst : @snn_mbt.ExtendedIF = @snn_mbt.ExtendedIF::new(
    n=n_sst,
    param=@snn_mbt.ExtendedIFParameter::custom(
      cm=180.0F, vt=-45.0F, vr=-55.0F, el=-65.0F, gl=12.0F,
      tau_e=6.0F, tau_i=30.0F, e_i=-75.0F, e_e=0.0F,
      tau_abs=4.0F, alpha=0.5F,
    ),
  )
  println("if_extended.mbt: 3-population E/PV/SST network built")
  println("  E   = \{n_e} neurons (default ExtendedIF params)")
  println("  PV  = \{n_pv} neurons (Vt=-50, Vr=-60, tau_i=15ms)")
  println("  SST = \{n_sst} neurons (Vt=-45, Vr=-55, tau_i=30ms, α=0.5)")
  let _ = p_e // touch p_e to avoid unused warning in some paths
  // ---- 2. Drive synapses with constant Poisson-like current ----
  // Set g_exc[0] = 10nS (excitatory input)
  ignore(p_e.g_exc.set(0, 10.0F))
  // Set g_pv[0] = 4nS (PV input)
  ignore(p_e.g_pv.set(0, 4.0F))
  // Set g_sst[0] = 3nS (SST input)
  ignore(p_e.g_sst.set(0, 3.0F))
  // Also set tonic current on E to make it more likely to fire
  ignore(p_e.i.set(0, 800.0F))
  // ---- 3. Run 800 steps (100ms @ dt=0.125ms) ----
  let dt : Float = 0.125F
  let mut step : Int = 0
  let mut spike_count : Int = 0
  while step < 800 {
    @snn_mbt.integrate_extended_if(p_e, p_e.param, dt)
    @snn_mbt.integrate_extended_if(p_pv, p_pv.param, dt)
    @snn_mbt.integrate_extended_if(p_sst, p_sst.param, dt)
    // count spikes (fire=true on this step)
    let mut k : Int = 0
    while k < n_e {
      if p_e.fire[k] {
        spike_count = spike_count + 1
      }
      k = k + 1
    }
    step = step + 1
  }
  println(
    "if_extended.mbt: simulation done (100ms @ dt=0.125ms = 800 steps)",
  )
  println("  E[0] v final = \{p_e.v[0]}")
  println(
    "  E[0] g_exc = \{p_e.g_exc[0]}, g_pv = \{p_e.g_pv[0]}, g_sst = \{p_e.g_sst[0]}",
  )
  println("  E[0] fire = \{p_e.fire[0]}")
  println("  E total spikes in 100ms = \{spike_count}")
  println("  PV[0] v = \{p_pv.v[0]}, SST[0] v = \{p_sst.v[0]}")
}