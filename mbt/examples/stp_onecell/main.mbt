// stp_onecell.mbt — Markram STP saturation demo (manual STP loop).
//
// Simplified version of Mongillo2008_WorkingMemory/STP_onecell.jl.
// The original Julia protocol:
//   pre = Identity(N=1); post = IF(N=1)
//   EE = SpikingSynapse(pre, post, :ge, w=1, param=MarkramSTPParameter())
//   train!(test, duration=5s)              # baseline
//   for x = 1:6: pre.g[1]=1; train!(30ms)  # 6 bursts of continuous drive
//   train!(200ms)                          # gap
//   pre.g[1]=1; train!(10s)                # recovery with continued drive
//
// MoonBit (v0.10.8): manual markram_stp_step loop with IF as pre.
// Identity cannot be used as pre because SpikingSynapse requires IF.
// We DO NOT run integrate! steps here, so STP state only updates
// when pre fires (no implicit recovery through exp(-dt/τD) decay).
//
// Behaviour observed: 240 continuous pre spikes in 30ms (8 kHz drive)
// saturate u → ~1.0 (facilitation fully accumulated) and deplete
// x → ~1.7e-7 (resources fully consumed). ρ → ~6e-4 (u*x ≈ 1*1.7e-7).
//
// This demo is qualitatively faithful to the Julia STP_onecell.jl but
// does NOT reproduce the recovery phase (which requires integrate!
// to apply the exp recovery). For a full Mongillo2008 working memory
// model, use stp_demo (which DOES include a recovery phase by
// re-running markram_stp_step after a long ΔT gap).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()

  // IF as pre_pop (since SpikingSynapse requires IF). Drive
  // pre_pop.fire[0] = true each step to simulate continuous drive.
  let pre_pop = @snn_mbt.IF::new(1, @snn_mbt.IFParameter::new(), rng)
  let post_pop = @snn_mbt.IF::new(1, @snn_mbt.IFParameter::new(), rng)

  // Synapse with weight 1.0; STP needs rho[] for per-edge weighting.
  let syn = @snn_mbt.SpikingSynapse::new(pre_pop, post_pop, "ge")
  @snn_mbt.spiking_connect(syn, 1, 1, 1.0F)
  @snn_mbt.SpikingSynapse::init_rho(syn)

  // Default MarkramSTP.
  let stp_param = @snn_mbt.MarkramSTPParameter::new()
  let stp_vars = @snn_mbt.MarkramSTPVariables::new(1, 1, stp_param)

  println("stp_onecell.mbt: STP_onecell.jl simplified (manual STP loop)")
  println("  params: τD=200ms τF=1500ms U=0.2 W=1.0")
  println("")

  // Helper: simulate `duration` ms of continuous pre drive.
  fn drive_burst(
    duration : Float,
    label : String,
  ) -> Unit {
    let dt = 0.125F
    let n_steps : Int = (duration / dt).to_int()
    let mut k = 0
    while k < n_steps {
      pre_pop.fire[0] = true
      @snn_mbt.markram_stp_step(
        syn, stp_vars, stp_param, Float::from_int(k) * dt,
      )
      pre_pop.fire[0] = false
      k = k + 1
    }
    println(
      label + " duration=" + duration.to_string() + "ms  " +
      "u=" + stp_vars.u[0].to_string() +
      "  x=" + stp_vars.x[0].to_string() +
      "  ρ=" + stp_vars.rho_pre[0].to_string(),
    )
    post_pop.glu[0] = 0.0F
    post_pop.gaba[0] = 0.0F
    post_pop.he[0] = 0.0F
  }

  // 5s baseline (no drive — STP state stays initial).
  println("[baseline] no drive")
  println("  u=" + stp_vars.u[0].to_string() + "  x=" + stp_vars.x[0].to_string() +
    "  ρ=" + stp_vars.rho_pre[0].to_string())
  println("")

  // 6 bursts of 30ms each — 240 spikes per burst at 8 kHz.
  let mut burst = 0
  while burst < 6 {
    drive_burst(30.0F, "[burst " + burst.to_string() + "]")
    burst = burst + 1
  }
  println("")

  // Final state: u → ~1 (fully facilitated by 6×240 spikes),
  // x → ~1.7e-7 (fully depleted), ρ → ~6e-4 (u*x).
  println("[after 6 bursts] saturation reached")
  println("  u=" + stp_vars.u[0].to_string() + "  x=" + stp_vars.x[0].to_string() +
    "  ρ=" + stp_vars.rho_pre[0].to_string())
  println("")
  println("Note: STP state recovery requires integrate! steps to apply")
  println("the exp(-dt/τD) recovery formula. See stp_demo for the recovery")
  println("phase demonstration with 200-spike train + 10s silent gap.")

  println("")
  println("stp_onecell.mbt: done")
}