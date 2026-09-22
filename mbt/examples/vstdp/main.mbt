// vstdp.mbt — visualises vSTDPParameter rule + minimal usage.
//
// Mirrors SNNModels.jl's `vstdp_kernel.jl` example (LKD 2014):
//   - Plots the LTP/LTD/no-change grid over (v_pre, v_post).
//   - Runs a small step where pre[0] fires (with post[0] depolarized),
//     triggers LTD, decrements w[0].
//   - Runs a post[0] fire (with pre[0] depolarized), triggers LTP,
//     increments w[0].

///|
fn main {
  println("vstdp.mbt: Litwin-Kumar-Doiron 2014 voltage-dependent STDP")
  println("")
  println("=== Voltage rule (v_pre × v_post quadrants) ===")
  @snn_mbt.vstdp_plot(@snn_mbt.VstdpParameter::new(), width=40, height=12)

  println("")
  println("=== Single-connection step demonstration ===")
  let vars = @snn_mbt.VstdpVariables::new(1, 1)
  vars.w[0] = 0.01F
  let p = @snn_mbt.VstdpParameter::new()
  println("  initial w[0] = " + vars.w[0].to_string())

  // Pre-fire (LTD): if post_v[0] > θ_LTD, decrement.
  let pre_v_1 : Array[Float] = [-50.0F]
  let post_v_1 : Array[Float] = [10.0F]
  let pre_fire_1 : Array[Bool] = [true]
  let post_fire_1 : Array[Bool] = [false]
  @snn_mbt.vstdp_step(vars, p, pre_v_1, post_v_1, pre_fire_1, post_fire_1)
  println("  after pre-fire + high post (LTD): w[0] = " + vars.w[0].to_string())

  // Post-fire (LTP): if pre_v[0] > θ_LTP, increment.
  let pre_v_2 : Array[Float] = [10.0F]
  let post_v_2 : Array[Float] = [-50.0F]
  let pre_fire_2 : Array[Bool] = [false]
  let post_fire_2 : Array[Bool] = [true]
  @snn_mbt.vstdp_step(vars, p, pre_v_2, post_v_2, pre_fire_2, post_fire_2)
  println("  after post-fire + high pre (LTP): w[0] = " + vars.w[0].to_string())

  println("")
  println("  Expected: 0.01 - 1e-3 + 2e-3 = 0.011")
  if (vars.w[0] - 0.011F).abs() < 1.0e-5F {
    println("  ✓ LTD then LTP sequence produces expected weight")
  } else {
    println("  ✗ weight doesn't match expected")
  }
}