// spike_analysis.mbt — port of SpikingNeuralNetworks.jl/test/analysis/spikes_test.jl
//
// Exercises the `vecplot` analysis infrastructure on a small driven
// IF population. After the sim, computes per-neuron:
//   - spike count  (Monitor::count_spikes)
//   - firing rate  (Monitor::firing_rate)
//   - spike times  (Monitor::spike_times)
//   - v summary    (Monitor::mean/min/max/dump_summary)
//   - ASCII plot   (Monitor::ascii_plot)
//
// Reference Julia test (analysis/spikes_test.jl): drives 20 IF
// neurons with El=-49mV (highly excitable), 500 ms sim, exercises
// `spiketimes`, `firing_rate`, `bin_spiketimes`, etc.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F

  // 20 IF neurons with El=-49mV (highly excitable — 1 mV below vt=-50mV)
  let if_param = @snn_mbt.IFParameter::custom(20.0F, -50.0F, -60.0F, -49.0F, 1.0F)
  let n = 20
  let pop = @snn_mbt.IF::new(n, if_param, rng)

  // Sparse self-recurrent excitatory connectivity (p=0.2, μ=0.5 nS).
  let ee = @snn_mbt.SpikingSynapse::random(pop, pop, "ge", 0.5F, 0.0F, 0.2F, rng)

  // Fire monitors for all neurons + v monitor for neuron 0
  let fire_monitors : Array[@snn_mbt.Monitor] = []
  for k in 0..<n {
    fire_monitors.push(@snn_mbt.Monitor::new_fire(pop, k))
  }
  let v_mon = @snn_mbt.Monitor::new_v(pop, 0)
  let v_mon_sr = @snn_mbt.Monitor::new_v_sr(pop, 0, 1.0F) // 1 kHz sampling

  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(pop)],
    [ee],
    monitors=[..fire_monitors, v_mon, v_mon_sr],
  )

  // Simulate 500 ms — passive El=-49mV drive keeps most neurons firing.
  let duration_ms = 500.0F
  @snn_mbt.heterogeneous_sim_for(model, duration_ms)

  // Per-neuron analysis
  println("spike_analysis.mbt: " + n.to_string() + " IF neurons, " + duration_ms.to_string() + "ms sim")
  println("")
  println("Per-neuron spike count and firing rate:")
  let mut total_spikes = 0
  let mut max_rate = 0.0F
  let mut min_rate = 1.0e9F
  for k in 0..<n {
    let m = fire_monitors[k]
    let cnt = m.count_spikes()
    let rate = m.firing_rate()
    total_spikes = total_spikes + cnt
    if rate > max_rate { max_rate = rate }
    if rate < min_rate { min_rate = rate }
    if k < 5 {
      // Print first 5 in detail
      println(
        "  neuron " + k.to_string() + ": " + cnt.to_string() +
        " spikes, " + rate.to_string() + " Hz",
      )
    }
  }
  println("  ... (" + (n - 5).to_string() + " more)")
  println("")
  println("Population summary:")
  println("  total spikes = " + total_spikes.to_string())
  println("  mean rate    = " +
    (Float::from_int(total_spikes) * 1000.0F / duration_ms / Float::from_int(n)).to_string() +
    " Hz")
  println("  min rate     = " + min_rate.to_string() + " Hz")
  println("  max rate     = " + max_rate.to_string() + " Hz")

  // Spike times for one neuron (with rising-edge transitions)
  println("")
  println("Spike times for neuron 0 (count, then first 5 times):")
  let m0 = fire_monitors[0]
  let st = m0.spike_times()
  println("  total = " + st.length().to_string())
  let show = if st.length() < 5 { st.length() } else { 5 }
  for k in 0..<show {
    println("  t[" + k.to_string() + "] = " + st[k].to_string() + " ms")
  }

  // Sub-interval analysis: spikes in [100, 300] ms
  println("")
  println("Firing rate in [100, 300] ms window (neuron 0):")
  let cnt_win = m0.count_spikes_interval(100.0F, 300.0F)
  let rate_win = m0.firing_rate_interval(100.0F, 300.0F)
  println("  count = " + cnt_win.to_string())
  println("  rate  = " + rate_win.to_string() + " Hz")

  // v summary + ASCII plot for neuron 0
  println("")
  println("Voltage summary (neuron 0):")
  v_mon.dump_summary()
  println("")
  println("ASCII plot of v[0] over the simulation:")
  v_mon_sr.ascii_plot(width=40, height=12)
}