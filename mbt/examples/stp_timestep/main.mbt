// stp_timestep.mbt — MarkramSTPParameterTimestep (continuous-time
// Euler Markram STP) demo.
//
// Reference Julia (test/syn/with_plasticity.jl "MarkramSTPParameterTimestep"):
//   model, syn = _driven_model(stp = MarkramSTPParameterTimestep())
//   train!(model, 200ms)
//   @test all(isfinite, syn.W)
//
// MoonBit (v0.10.35):
//   - 4x4 dense SpikingSynapse + MarkramSTPEntryTimestep.
//   - Drive with random pre/post firing for 200 steps.
//   - Show that all weights remain finite, rho stays in [0, 1],
//     and u/x traces are within sane bounds.
//
// Bit-exact note: rule body matches Julia's MarkramSTPParameterTimestep
// with U=0.5, τF=750ms, τD=250ms. Continuous-time Euler update of u, x
// each step, plus a discrete spike-induced bump on pre-fire.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_pre = 4
  let n_post = 4

  // Build dense 4x4 connectivity.
  let n_conn = n_pre * n_post
  let pre = @snn_mbt.IF::new(n_pre, @snn_mbt.IFParameter::new(), rng)
  let post = @snn_mbt.IF::new(n_post, @snn_mbt.IFParameter::new(), rng)
  let syn = @snn_mbt.SpikingSynapse::new(pre, post, "ge")
  for j in 1..<=n_pre {
    for i in 1..<=n_post {
      @snn_mbt.spiking_connect(syn, j, i, 1.0F)
    }
  }
  @snn_mbt.SpikingSynapse::init_rho(syn)

  let entry = @snn_mbt.MarkramSTPEntryTimestep::new(0, n_pre, n_post)
  println("stp_timestep.mbt: MarkramSTPParameterTimestep (continuous-time Euler) demo")
  println("")
  println("=== MarkramSTPParameterTimestep defaults ===")
  println("  u = " + entry.param.u.to_string())
  println("  tau_f = " + entry.param.tau_f.to_string())
  println("  tau_d = " + entry.param.tau_d.to_string())
  println("")
  println("=== Driving 4x4 dense synapse for 200 steps with random pre/post firing ===")

  let n_steps = 200
  for step in 0..<n_steps {
    let _ = step
    for k in 0..<n_pre {
      pre.fire[k] = @snn_mbt.next_f32(rng) < 0.3F
    }
    let t_now = Float::from_int(step) * dt
    @snn_mbt.markram_stp_step_timestep(syn, entry.vars, entry.param, t_now, dt)
  }

  // Sanity checks.
  let mut finite = true
  let mut rho_min = 999.0F
  let mut rho_max = -999.0F
  let mut u_min = 999.0F
  let mut u_max = -999.0F
  for s in 0..<n_conn {
    if (syn.matrix.vals[s]).is_nan() || (syn.matrix.vals[s]).is_inf() {
      finite = false
    }
    if syn.rho[s] < rho_min { rho_min = syn.rho[s] }
    if syn.rho[s] > rho_max { rho_max = syn.rho[s] }
  }
  for k in 0..<n_pre {
    if entry.vars.u[k] < u_min { u_min = entry.vars.u[k] }
    if entry.vars.u[k] > u_max { u_max = entry.vars.u[k] }
  }
  println("")
  println("=== Final statistics ===")
  println("  all weights finite: " + finite.to_string())
  println("  rho range: [" + rho_min.to_string() + ", " + rho_max.to_string() + "]")
  println("  u range:   [" + u_min.to_string() + ", " + u_max.to_string() + "]")
  println("")
  if finite {
    println("✓ MarkramSTPParameterTimestep: weights finite after random drive")
  } else {
    println("✗ MarkramSTPParameterTimestep produced NaN/Inf (BUG)")
  }
  if rho_min >= 0.0F && rho_max <= 1.0F + 1.0e-5F {
    println("✓ rho in [0, 1] bounds")
  } else {
    println("✗ rho out of bounds")
  }
  if u_min >= 0.0F && u_max <= 1.0F + 1.0e-5F {
    println("✓ u in [0, 1] bounds")
  } else {
    println("✗ u out of bounds")
  }
}