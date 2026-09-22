// change_plasticity.mbt — demonstrates runtime STDP parameter swap.
//
// Reference Julia (with_plasticity.jl "swap LTPParam" test):
//   model, syn = _driven_model(ltp = STDPGerstner())
//   change_plasticity!(syn; LTP = STDPConfavreux2025())
//   @test syn.LTPParam isa STDPConfavreux2025
//   @test syn.LTPVars isa STDPVariables
//
// MoonBit (v0.10.28):
//   - Build STDPEntry with default STDPGerstner
//   - Run a few manual pre/post firing pairs (via the entry's tpre/tpost)
//   - Show that the swap works and vars are preserved.
//   - Report final param + last_pre/last_post values.

///|
fn main {
  // Create an STDPEntry for a small pre/post population.
  let entry = @snn_mbt.STDPEntry::new(0, 2, 2)
  println("change_plasticity.mbt: STDP runtime parameter swap demo")
  println("")
  println("=== Initial STDPEntry state ===")
  println("  param.a_pre = " + entry.param.a_pre.to_string())
  println("  param.a_post = " + entry.param.a_post.to_string())
  println("  param.tau_pre = " + entry.param.tau_pre.to_string())
  println("  param.w_max = " + entry.param.w_max.to_string())

  // Simulate some spike timing (manually push to vars).
  entry.vars.last_pre[0] = 5.0F
  entry.vars.last_post[1] = 10.0F
  println("")
  println("  vars.last_pre[0] = " + entry.vars.last_pre[0].to_string())
  println("  vars.last_post[1] = " + entry.vars.last_post[1].to_string())

  // Swap plasticity: replace STDPGerstner with custom params.
  println("")
  println("=== Calling change_plasticity! with new params ===")
  let new_param : @snn_mbt.STDPGerstner = {
    a_pre: 0.5F,
    a_post: 0.3F,
    tau_pre: 30.0F,
    tau_post: 25.0F,
    w_max: 50.0F,
    w_min: 0.0F,
  }
  @snn_mbt.STDPEntry::change_plasticity(entry, new_param)

  println("  after swap:")
  println("  param.a_pre = " + entry.param.a_pre.to_string())
  println("  param.a_post = " + entry.param.a_post.to_string())
  println("  param.tau_pre = " + entry.param.tau_pre.to_string())
  println("  param.w_max = " + entry.param.w_max.to_string())

  // Verify vars are preserved across the swap.
  println("")
  println("  vars.last_pre[0] (should still be 5.0) = " + entry.vars.last_pre[0].to_string())
  println("  vars.last_post[1] (should still be 10.0) = " + entry.vars.last_post[1].to_string())
  println("")
  if entry.param.a_pre == 0.5F && entry.param.a_post == 0.3F {
    println("✓ change_plasticity! succeeded: param swapped correctly")
  } else {
    println("✗ change_plasticity! failed: param not swapped")
  }
  if entry.vars.last_pre[0] == 5.0F && entry.vars.last_post[1] == 10.0F {
    println("✓ vars preserved across swap")
  } else {
    println("✗ vars mutated by swap (BUG)")
  }
}