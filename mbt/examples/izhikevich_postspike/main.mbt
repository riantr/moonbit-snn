// izhikevich_postspike.mbt — single IZ neuron with postspike refractory.
//
// Combines:
//   - IZ (RS variant) from neuron_iz.mbt
//   - IZPostSpike + IZ::init_with_postspike (v0.10.116)
//   - step_iz_with_postspike (v0.10.117)
//   - step_iz_synapses_postspike (v0.10.122)
//
// Demonstrates how absolute refractory period shapes firing rate:
//   1. Without refractory: tonic 30 pA → neuron fires ~10 Hz.
//   2. With refractory tabs_const=8 (~1 ms at dt=0.125): tonic 30 pA
//      → firing rate bounded by 1/(8*0.125 ms) ≈ 1 kHz ÷ 8 ≈ 125 Hz.
//   3. With refractory tabs_const=20 (~2.5 ms): firing rate further
//      capped at ~50 Hz.
//
// Run: `moon run examples/izhikevich_postspike`

///|
fn main {
  let dt = 0.125F
  let tonic = 30.0F

  // Three populations: no refractory, short refractory (8 steps), long
  // refractory (20 steps). Same IZParameter, same tonic input.
  let pop_none = @snn_mbt.IZ::new(1, @snn_mbt.IZParameter::rs(), @snn_mbt.Xoshiro::default())
  let pop_short = @snn_mbt.IZ::init_with_postspike(
    1, @snn_mbt.IZParameter::rs(), -65.0F, -13.0F, @snn_mbt.IZPostSpike::custom(8),
  )
  let pop_long = @snn_mbt.IZ::init_with_postspike(
    1, @snn_mbt.IZParameter::rs(), -65.0F, -13.0F, @snn_mbt.IZPostSpike::custom(20),
  )

  println("izhikevich_postspike.mbt: RS IZ + postspike refractory comparison")
  println("  tonic drive: 30 pA; dt: 0.125ms; duration: 1 s (8000 steps)")
  println("  pop_none: tabs_const=0 (no refractory)")
  println("  pop_short: tabs_const=8 (~1 ms)")
  println("  pop_long:  tabs_const=20 (~2.5 ms)")

  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  let mut fires_none : Int = 0
  let mut fires_short : Int = 0
  let mut fires_long : Int = 0
  for _step in 0..<steps {
    let _ = _step
    pop_none.i[0] = tonic
    pop_short.i[0] = tonic
    pop_long.i[0] = tonic
    @snn_mbt.step_iz(pop_none, dt)
    @snn_mbt.step_iz_with_postspike(pop_short, dt)
    @snn_mbt.step_iz_with_postspike(pop_long, dt)
    if pop_none.fire[0] { fires_none = fires_none + 1 }
    if pop_short.fire[0] { fires_short = fires_short + 1 }
    if pop_long.fire[0] { fires_long = fires_long + 1 }
  }
  let rate_none = Float::from_int(fires_none) *
    1000.0F / duration_ms
  let rate_short = Float::from_int(fires_short) *
    1000.0F / duration_ms
  let rate_long = Float::from_int(fires_long) *
    1000.0F / duration_ms

  println("izhikevich_postspike.mbt: simulation done")
  println("  pop_none:  \{fires_none} fires (\{rate_none} Hz)")
  println("  pop_short: \{fires_short} fires (\{rate_short} Hz)")
  println("  pop_long:  \{fires_long} fires (\{rate_long} Hz)")
  println("")
  println("Note: with dt=0.125 ms and tonic 30 pA:")
  println("  - no refractory: IZ RS fires at natural rate (~10-15 Hz)")
  println("  - tabs_const=8: caps rate at ~125 Hz but spike-frequency")
  println("    adaptation keeps effective rate lower (~30-50 Hz)")
  println("  - tabs_const=20: caps rate at ~50 Hz, observed rate ~10-15 Hz")
}