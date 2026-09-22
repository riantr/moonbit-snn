// istdp_rate.mbt — Vogels 2011 inhibitory STDP demo.
//
// Reference Julia (test/syn/with_plasticity.jl "iSTDPRate" testset):
//   pre  = IF(N = 15, param = _active_p)
//   post = IF(N = 10, param = _active_p)
//   syn  = SpikingSynapse(pre, post, :gi;
//       conn     = (p = 0.5f0, mu = 1.0f0),
//       LTPParam = iSTDPRate())
//   model = compose(pre = pre, post = post, syn = syn, silent = true)
//   train!(model, 200ms)
//   @test all(isfinite, syn.W)
//
// MoonBit (v0.10.33):
//   - 4x4 dense SpikingSynapse + IstdpRateEntry.
//   - Drive with random pre/post spikes for 100 steps.
//   - Show that all weights remain finite and within [w_min, w_max].
//
// Bit-exact note: rule body matches Julia's iSTDPRate with eta=0.01,
// r=3Hz (internal 0.003/ms), tau_y=50ms, w_min=0.01, w_max=243.
// Continuous-decay trace model: tpre += dt*(-tpre)/tau_y + spike bump.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_pre = 4
  let n_post = 4

  // Build dense 4x4 connectivity.
  let n_conn = n_pre * n_post
  let w : Array[Float] = Array::make(n_conn, 100.0F) // start near middle of [w_min, w_max]
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)

  // Build the entry with default IstdpRate params.
  let entry = @snn_mbt.IstdpRateEntry::new(0, n_pre, n_post)
  println("istdp_rate.mbt: IstdpRate (Vogels 2011 inhibitory STDP) demo")
  println("")
  println("=== IstdpRate defaults ===")
  println("  eta = " + entry.param.eta.to_string())
  println("  r = " + entry.param.r.to_string() + " (3 Hz internal)")
  println("  tau_y = " + entry.param.tau_y.to_string())
  println("  w_max = " + entry.param.w_max.to_string())
  println("  w_min = " + entry.param.w_min.to_string())
  println("")
  println("=== Driving 4x4 dense synapse for 100 steps with random pre/post spikes ===")

  let n_steps = 100
  for step in 0..<n_steps {
    let _ = step
    for k in 0..<n_pre {
      pre_fire[k] = @snn_mbt.next_f32(rng) < 0.3F
    }
    for k in 0..<n_post {
      post_fire[k] = @snn_mbt.next_f32(rng) < 0.3F
    }
    let t_now = Float::from_int(step) * dt
    @snn_mbt.istdp_rate_step(
      w, pre_fire, post_fire, colptr, rowptr, entry.vars, entry.param, t_now, dt,
    )
  }

  // Sanity checks.
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
      "✓ IstdpRate step function: weights finite after random drive",
    )
  } else {
    println("✗ IstdpRate produced NaN/Inf (BUG)")
  }
  if mn >= entry.param.w_min - 1.0e-5F && mx <= entry.param.w_max + 1.0e-5F {
    println("✓ weights clamped to [w_min, w_max]")
  } else {
    println("✗ weight bounds violated")
  }
}