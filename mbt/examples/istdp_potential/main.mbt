// istdp_potential.mbt — Vogels 2011 inhibitory STDP demo with
// potential-based post-synaptic trace.
//
// Reference Julia (test/syn/with_plasticity.jl "iSTDPPotential"):
//   pre  = IF(N = 10, param = _active_p)
//   post = IF(N = 8,  param = _active_p)
//   syn  = SpikingSynapse(pre, post, :gi;
//       conn     = (p = 0.5f0, mu = 1.0f0),
//       LTPParam = iSTDPPotential())
//   model = compose(pre = pre, post = post, syn = syn, silent = true)
//   train!(model, 200ms)
//   @test all(isfinite, syn.W)
//
// MoonBit (v0.10.34):
//   - 4x4 dense SpikingSynapse + IstdpPotentialEntry.
//   - Drive with random pre/post firing for 100 steps; v_post[i]
//     varies randomly in [-70, 50] mV each step.
//   - Show that all weights remain finite and within [w_min, w_max].
//
// Bit-exact note: rule body matches Julia's iSTDPPotential with
// eta=0.001pA, v0=-50mV, tau_y=200ms, w_min=0.01, w_max=243. The
// post-synaptic trace tpost[i] is a low-pass filter of v_post[i]
// (decays toward v_post with time constant tau_y), plus a +1 bump
// on each post-spike.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_pre = 4
  let n_post = 4

  let n_conn = n_pre * n_post
  let w : Array[Float] = Array::make(n_conn, 100.0F)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)
  let v_post : Array[Float] = Array::make(n_post, 0.0F)

  let entry = @snn_mbt.IstdpPotentialEntry::new(0, n_pre, n_post)
  println("istdp_potential.mbt: IstdpPotential (potential-based iSTDP) demo")
  println("")
  println("=== IstdpPotential defaults ===")
  println("  eta = " + entry.param.eta.to_string())
  println("  v0 = " + entry.param.v0.to_string())
  println("  tau_y = " + entry.param.tau_y.to_string())
  println("  w_max = " + entry.param.w_max.to_string())
  println("  w_min = " + entry.param.w_min.to_string())
  println("")
  println("=== Driving 4x4 dense synapse for 100 steps with random pre/post firing ===")
  println("    v_post[i] varied in [-70, 50] mV each step")

  let n_steps = 100
  for step in 0..<n_steps {
    let _ = step
    for k in 0..<n_pre {
      pre_fire[k] = @snn_mbt.next_f32(rng) < 0.3F
    }
    for k in 0..<n_post {
      post_fire[k] = @snn_mbt.next_f32(rng) < 0.3F
      v_post[k] = -70.0F + 120.0F * @snn_mbt.next_f32(rng)
    }
    let t_now = Float::from_int(step) * dt
    @snn_mbt.istdp_potential_step(
      w, pre_fire, post_fire, colptr, rowptr, v_post, entry.vars, entry.param, t_now, dt,
    )
  }

  let mut finite = true
  let mut mn = 999.0F
  let mut mx = -999.0F
  for s in 0..<n_conn {
    if w[s].is_nan() || w[s].is_inf() {
      finite = false
    }
    if w[s] < mn { mn = w[s] }
    if w[s] > mx { mx = w[s] }
  }
  println("")
  println("=== Final weight statistics ===")
  println("  all weights finite: " + finite.to_string())
  println("  min w = " + mn.to_string() + ", max w = " + mx.to_string())
  println("  w_min bound = " + entry.param.w_min.to_string())
  println("  w_max bound = " + entry.param.w_max.to_string())
  println("")
  if finite {
    println(
      "✓ IstdpPotential step function: weights finite after random drive",
    )
  } else {
    println("✗ IstdpPotential produced NaN/Inf (BUG)")
  }
  if mn >= entry.param.w_min - 1.0e-5F && mx <= entry.param.w_max + 1.0e-5F {
    println("✓ weights clamped to [w_min, w_max]")
  } else {
    println("✗ weight bounds violated")
  }
}