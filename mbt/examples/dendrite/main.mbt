// dendrite.mbt — passive dendritic compartment (single dendrite) example.
//
// Julia reference (SpikingNeuralNetworks.jl/test/pop/dendrite.jl):
// drive a passive Dendrite with constant external current and observe
// voltage decay toward El with axial coupling to a held parent voltage.
//
// MoonBit (v0.10.17):
//   - 1 passive Dendrite (no active channels, just leak + axial).
//   - Held parent voltage at -50 mV (mimics a depolarized soma).
//   - External current pulse: I_ext = 50 pA for the first 200 ms, then
//     I_ext = 0 pA (passive relaxation).
//   - 1 s sim at dt = 0.125 ms. Reports final v_d + peak v_d.
//
// Why these values? With El=-70.6 mV, C=10 pF, gm=1 nS, gax=10 nS:
//   - At t=0, parent pulls dendrite up: ic = (-50 - v_d) * 10 nS.
//   - I_ext = 50 pA adds another depolarization term.
//   - With 200 ms of drive, dendrite charges up; then relaxation phase
//     shows leak + axial back toward steady state (~-60 mV given the
//     held parent at -50).

///|
fn main {
  let dt = 0.125F

  // 1 passive Dendrite (default params).
  let dend = @snn_mbt.Dendrite::new(1)
  // Voltage state.
  let v : Array[Float] = Array::make(1, dend.el[0])  // start at rest
  let v_parent : Array[Float] = Array::make(1, -50.0F)  // depolarized soma
  let i_ext : Array[Float] = Array::make(1, 0.0F)

  let total_steps : Int = (1000.0F / dt).to_int()
  let mut v_peak = v[0]
  let mut v_final = v[0]
  for step in 0..<total_steps {
    let t_ms = Float::from_int(step) * dt
    // First 200 ms: 50 pA external current. After: 0.
    if t_ms < 200.0F {
      i_ext[0] = 50.0F
    } else {
      i_ext[0] = 0.0F
    }
    let dv = @snn_mbt.dendrite_step(dend, v, v_parent, i_ext)
    v[0] = v[0] + dt * dv[0]
    if v[0] > v_peak {
      v_peak = v[0]
    }
    v_final = v[0]
  }

  println("dendrite.mbt: 1 passive Dendrite, 1s sim")
  println("  dendrite: default (El=-70.6mV, C=10pF, gax=10nS, gm=1nS)")
  println("  parent: held at -50 mV (depolarized soma stand-in)")
  println("  drive: I_ext=50pA for first 200ms, then 0pA (passive relaxation)")
  println("dendrite.mbt: simulation done (1s, dt=0.125ms)")
  println("  v_d[0] initial = " + dend.el[0].to_string() + " (mV, El)")
  println("  v_d[0] peak    = " + v_peak.to_string() + " (mV)")
  println("  v_d[0] final   = " + v_final.to_string() + " (mV)")
  println("")
  println("Note: this exercises the passive Dendrite compartment built in v0.10.12.")
  println("  Voltage should rise during the 200 ms drive (axial + I_ext), then")
  println("  decay back toward the parent's steady-state influence once I_ext=0.")
  println("  No active channels; no spikes — pure RC + axial coupling.")
}