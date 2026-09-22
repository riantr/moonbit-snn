// izhikevich_net.mbt — Brunel-style IZ network with EE + EI + IE + II.
//
// Port of `refs/SNNModels.jl/test/network/iz_net.jl`:
//   Ne = 800, Ni = 200
//   E = IZ(; N = Ne, param = IZParameter(; a = 0.02, b = 0.2, c = -65, d = 8))
//   I = IZ(; N = Ni, param = IZParameter(; a = 0.1,  b = 0.2, c = -65, d = 2))
//   EE = SpikingSynapse(E, E, :v; conn = (μ = 0.5,  p = 0.8))
//   EI = SpikingSynapse(E, I, :v; conn = (μ = 0.5,  p = 0.8))
//   IE = SpikingSynapse(I, E, :v; conn = (μ = -1.0, p = 0.8))
//   II = SpikingSynapse(I, I, :v; conn = (μ = -1.0, p = 0.8))
//
//   E.I .= 5randn(Ne);  I.I .= 2randn(Ni)
//   sim!(P, C, [EmptyStimulus()], 1.0f0, Time())
//
// MoonBit port uses a smaller network (Ne=80, Ni=20) for fast runtime.
// The dynamics (E/I ratio, sign of weights, σ of input) match the Julia
// reference at the qualitative level.
//
// Run: `moon run examples/izhikevich_net`

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_e = 80
  let n_i = 20

  // Build E population with a=0.02, b=0.2, c=-65, d=8 (regular spiking).
  // IZParameter::rs() gives a=0.02, b=0.2, c=-65, d=8 exactly.
  let param_e = @snn_mbt.IZParameter::rs()
  let e = @snn_mbt.IZ::new(n_e, param_e, rng)

  // Build I population with a=0.1, b=0.2, c=-65, d=2 (fast spiking).
  let param_i = @snn_mbt.IZParameter::fs()
  let i = @snn_mbt.IZ::new(n_i, param_i, rng)

  // Build the 4 synapses with random connectivity (Julia's conn = (μ, p)).
  let ee = @snn_mbt.SpikingSynapseIZ::random(e, e, "ge", 0.5F, 0.0F, 0.8F, rng)
  let ei = @snn_mbt.SpikingSynapseIZ::random(e, i, "ge", 0.5F, 0.0F, 0.8F, rng)
  let ie = @snn_mbt.SpikingSynapseIZ::random(i, e, "gi", 1.0F, 0.0F, 0.8F, rng)
  let ii = @snn_mbt.SpikingSynapseIZ::random(i, i, "gi", 1.0F, 0.0F, 0.8F, rng)

  println("izhikevich_net.mbt: IZ Brunel-style network")
  println("  Ne = " + n_e.to_string() + ", Ni = " + n_i.to_string())
  println("  EE μ=+0.5, EI μ=+0.5, IE μ=-1.0 (gi), II μ=-1.0 (gi); p=0.8 each")
  println("  E input σ=5; I input σ=2 (per step Gaussian)")

  // Simulate 1 second.
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut e_spikes = 0
  let mut i_spikes = 0
  for step in 0..<steps {
    let _ = step
    // 5randn(Ne) on E, 2randn(Ni) on I (Box-Muller).
    for j in 0..<n_e {
      let (z1, _) = @snn_mbt.box_muller(rng)
      e.i[j] = 5.0F * Float::from_double(z1)
    }
    for j in 0..<n_i {
      let (z1, _) = @snn_mbt.box_muller(rng)
      i.i[j] = 2.0F * Float::from_double(z1)
    }
    // Propagate spikes + integrate.
    @snn_mbt.forward_iz_synapse(ee)
    @snn_mbt.forward_iz_synapse(ei)
    @snn_mbt.forward_iz_synapse(ie)
    @snn_mbt.forward_iz_synapse(ii)
    @snn_mbt.step_iz(e, dt)
    @snn_mbt.step_iz(i, dt)
    for j in 0..<n_e {
      if e.fire[j] {
        e_spikes = e_spikes + 1
      }
    }
    for j in 0..<n_i {
      if i.fire[j] {
        i_spikes = i_spikes + 1
      }
    }
  }
  let e_rate = Float::from_int(e_spikes) *
    1000.0F / duration_ms / Float::from_int(n_e)
  let i_rate = Float::from_int(i_spikes) *
    1000.0F / duration_ms / Float::from_int(n_i)

  println("izhikevich_net.mbt: simulation done (1s, dt=0.125ms)")
  println("  E spikes = " + e_spikes.to_string() +
    " (rate " + e_rate.to_string() + " Hz per neuron)")
  println("  I spikes = " + i_spikes.to_string() +
    " (rate " + i_rate.to_string() + " Hz per neuron)")
  println("")
  println("Note: regular-spiking E cells (a=0.02, b=0.2, d=8) accumulate")
  println("  strong spike-frequency adaptation; fast-spiking I cells")
  println("  (a=0.1, d=2) sustain higher rates. Network is inhibition-")
  println("  dominated (IE μ=1.0 in gi is the strongest coupling), so")
  println("  E rate < I rate in steady state.")
}