// hetrec.mbt — minimal HetRec (heterogeneous-timescale non-recurrent)
// population example.
//
// Julia reference (SpikingNeuralNetworks.jl/test/pop/hetrec.jl, simplified):
//   E = HetRec(N = 100, ...)
//   pre = Poisson(N = 100, rate = 5Hz)
//   synapse = SpikingSynapse(pre, E, :ge, w = 1.0)
//   sim!([E], duration = 1s)
//
// MoonBit (v0.10.17):
//   - 1 HetRec population (N=50, default HetRecParameter).
//   - Manual dendritic current injection via `is_` (random Poisson-like
//     bursts every 8 ms), simulating SpikingSynapse output without
//     wiring up the full synapse framework. HetRec is non-recurrent
//     (no EE connections), so the only input is the external drive.
//   - 1 s sim at dt = 0.125 ms.
//   - Reports final mean |v_s|, total spikes, mean firing rate, and
//     the per-compartment τd range.
//
// Why manual is_ injection? Wiring SpikingSynapse to HetRec requires
// a CurrentSynapse equivalent (Julia uses `synapse::SYN = CurrentSynapse()`)
// which we haven't built yet. For the first HetRec example, direct
// injection into `is_` is sufficient to demonstrate the population
// dynamics (dendritic filtering + stochastic soma firing).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 50 HetRec neurons with default parameters (Nd=2, overlap=0.5,
  // τd ∈ Uniform(10, 100) ms, rate ∈ Uniform(0, 1) Hz, τm=20ms,
  // τrate=100ms, τabs=5ms).
  let param = @snn_mbt.HetRecParameter::new()
  let pop = @snn_mbt.HetRec::new(50, param, rng)

  // 1 s sim with random Poisson-like bursts every 8 ms.
  let total_steps : Int = (1000.0F / dt).to_int()
  let mut total_spikes = 0
  let mut sum_abs_v_s : Float = 0.0F
  let burst_period = 8
  for step in 0..<total_steps {
    // Burst drive every `burst_period` steps: inject random current
    // into a random subset of dendrites.
    if step % burst_period == 0 {
      for d in 0..<pop.is_.length() {
        let u = @snn_mbt.next_f32(rng)
        pop.is_[d] = if u < 0.3F { 5.0F } else { 0.0F }
      }
    } else {
      for d in 0..<pop.is_.length() {
        pop.is_[d] = 0.0F
      }
    }
    // Refresh random cache for stochastic firing, then step.
    @snn_mbt.hetrec_refresh_random(pop, rng)
    @snn_mbt.step_hetrec(pop, dt)
    for i in 0..<pop.n {
      if pop.fire[i] {
        total_spikes = total_spikes + 1
      }
      let abs_v = if pop.v_s[i] < 0.0F { -pop.v_s[i] } else { pop.v_s[i] }
      sum_abs_v_s = sum_abs_v_s + abs_v
    }
  }

  let mean_abs_v_s = sum_abs_v_s / Float::from_int(total_steps) * Float::from_int(pop.n)
  let mean_rate_hz = Float::from_int(total_spikes) / Float::from_int(pop.n) / 1.0F
  // τd range
  let mut min_tau_d : Float = 1.0e9F
  let mut max_tau_d : Float = 0.0F
  for d in 0..<pop.tau_d.length() {
    if pop.tau_d[d] < min_tau_d {
      min_tau_d = pop.tau_d[d]
    }
    if pop.tau_d[d] > max_tau_d {
      max_tau_d = pop.tau_d[d]
    }
  }

  println("hetrec.mbt: 50 HetRec neurons (Nd=2, overlap=0.5), 1s sim")
  println("  params: HetRecParameter default (τm=20ms, τrate=100ms, τabs=5ms)")
  println("  drive:  random Poisson-like bursts (30% prob @ 5 pA every 8 ms)")
  println("hetrec.mbt: simulation done (1s, dt=0.125ms)")
  println("  total spikes = " + total_spikes.to_string() + " across " + pop.n.to_string() + " neurons")
  println("  mean rate = " + mean_rate_hz.to_string() + " Hz")
  println("  mean |v_s| = " + mean_abs_v_s.to_string() + " (mV)")
  println("  τd range = [" + min_tau_d.to_string() + ", " + max_tau_d.to_string() + "] ms")
  println("")
  println("Note: this exercises the HetRec neuron built in v0.10.17.")
  println("  The Julia hetrec.jl uses SpikingSynapse(pre, E, :ge, w) to")
  println("  inject current via the synapse framework; we inject directly")
  println("  into pop.is_ for now (CurrentSynapse stub is TODO).")
}