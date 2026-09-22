// wilson_cowan.mbt — minimal Wilson-Cowan rate-model population example.
//
// Julia reference (SpikingNeuralNetworks.jl/test/pop/wilsoncowan.jl):
//   wc = WilsonCowan(N = 20)
//   monitor!(wc, [:r])
//   sim!([wc]; duration = 100ms)
//
// MoonBit (v0.10.18):
//   - 20 WilsonCowan neurons (rate model: x += dt*(-x + g + I); r = tanh(x))
//   - all-to-all self-connectivity via RateSynapse (μ=0.5, p=1.0,
//     weight std = μ/sqrt(p*N) ≈ 0.112)
//   - external tonic input I=0.2 (small drive to break symmetry)
//   - 100 ms sim at dt=0.125 ms
//   - prints final r values + a few time samples + mean rate over time
//
// This example demonstrates the simplest possible rate-network sim:
// one WilsonCowan population + one self-recurrent RateSynapse + an
// external current drive. The network settles toward a fixed point
// where each neuron's r is a weighted sum of all other r's (plus I),
// filtered through the leak `x += dt*(-x + g + I)`.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 20 WilsonCowan rate neurons.
  let n = 20
  let g = @snn_mbt.WilsonCowan::new(n, rng)
  // All-to-all self-connectivity (p=1.0) with μ=0.5. Julia's
  // RateSynapse normalises weight std by sqrt(p*N) so the network
  // stays bounded.
  let gg = @snn_mbt.RateSynapse::new(g, g, 0.5F, 0.0F, 1.0F, rng)
  // External tonic input I=0.2 on every neuron.
  for k in 0..<n {
    g.i[k] = 0.2F
  }

  println("wilson_cowan.mbt: " + n.to_string() + " WilsonCowan neurons + self-RateSynapse, 100ms sim")
  println("  μ=0.5, p=1.0 (all-to-all self-connectivity, sqrt(N) weight std)")
  println("  external I = 0.2 (tonic drive)")

  // 100 ms = 800 steps at dt=0.125 ms.
  let total_steps = 800
  // Sample r[0] at 4 time points to show transient + steady-state.
  let sample_steps : Array[Int] = [0, 100, 400, 799]
  let sample_labels : Array[String] = ["t=0ms   ", "t=12.5ms", "t=50ms  ", "t=99.9ms"]
  let r_samples : Array[Array[Float]] = Array::make(sample_steps.length(), [])
  for i in 0..<sample_steps.length() {
    r_samples[i] = []
  }
  let mut sum_abs_r : Float = 0.0F
  for step in 0..<total_steps {
    // 1. forward (rate → synaptic g)
    @snn_mbt.forward_rate_synapse(gg)
    // 2. integrate
    @snn_mbt.step_wc(g, dt)
    // Track envelope
    for k in 0..<n {
      let abs_r = if g.r[k] < 0.0F { -g.r[k] } else { g.r[k] }
      sum_abs_r = sum_abs_r + abs_r
    }
    // Sample at the chosen steps.
    for i in 0..<sample_steps.length() {
      if step == sample_steps[i] {
        for k in 0..<n {
          r_samples[i].push(g.r[k])
        }
      }
    }
  }

  let mean_abs_r = sum_abs_r / Float::from_int(total_steps) * Float::from_int(n)
  println("wilson_cowan.mbt: simulation done (100ms)")
  println("  mean |r| = " + mean_abs_r.to_string() + " (across all neurons, all time)")
  for i in 0..<sample_steps.length() {
    let rs = r_samples[i]
    let r0 = rs[0]
    let r10 = rs[10]
    let r19 = rs[19]
    println(
      "  " + sample_labels[i] + " r[0]=" + r0.to_string() + " r[10]=" + r10.to_string() +
      " r[19]=" + r19.to_string(),
    )
  }
  println("")
  println("Note: this exercises the WilsonCowan rate model (built v0.0.x)")
  println("  + RateSynapse (rate-mode forward). The network converges toward")
  println("  a fixed point where x_i = sum_j W_ij * tanh(x_j) + I.")
}