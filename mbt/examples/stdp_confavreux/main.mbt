// stdp_confavreux.mbt — STDPConfavreux2025 demo.
//
// Reference Julia (test/syn/with_plasticity.jl "STDPConfavreux2025"):
//   model, syn = _driven_model(ltp = STDPConfavreux2025())
//   train!(model, 200ms)
//   @test all(isfinite, syn.W)
//
// MoonBit (v0.10.32):
//   - Build a 4x4 dense SpikingSynapse + an STDPEntryConfavreux2025 entry.
//   - Drive with random pre/post spikes (Poisson via Xoshiro threshold)
//     for 100 steps.
//   - Show that all weights remain finite and within [w_min, w_max].
//
// Bit-exact note: rule body matches Julia's STDPConfavreux2025 with
// eta=0.01, alpha=beta=0, kappa=gamma=1.0, tau_pre=tau_post=20ms,
// w_min=0, w_max=30. The continuous-decay trace model in MoonBit
// (tpre *= exp(-dt/tau_pre) + spike bump) is mathematically
// equivalent to Julia's time-since-last-spike formulation.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_pre = 4
  let n_post = 4

  // Build the connectivity. Dense 4x4 with rowptr = [0,4,8,12,16] and
  // colptr = [0,1,2,3, 0,1,2,3, ...] (each pre connects to all post).
  let n_conn = n_pre * n_post
  let w : Array[Float] = Array::make(n_conn, 1.0F)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]

  // Per-step fire buffers.
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)

  // Build the entry with default STDPConfavreux2025 params.
  let entry = @snn_mbt.STDPEntryConfavreux2025::new(0, n_pre, n_post)
  println("stdp_confavreux.mbt: STDPConfavreux2025 (Confavreux 2025) demo")
  println("")
  println("=== STDPConfavreux2025 defaults ===")
  println("  eta = " + entry.param.eta.to_string())
  println("  alpha = " + entry.param.alpha.to_string())
  println("  beta = " + entry.param.beta.to_string())
  println("  kappa = " + entry.param.kappa.to_string())
  println("  gamma = " + entry.param.gamma.to_string())
  println("  tau_pre = " + entry.param.tau_pre.to_string())
  println("  tau_post = " + entry.param.tau_post.to_string())
  println("  w_max = " + entry.param.w_max.to_string())
  println("  w_min = " + entry.param.w_min.to_string())
  println("")
  println("=== Driving 4x4 dense synapse for 100 steps with random pre/post spikes ===")

  // Run 100 steps with random pre/post firing (Bernoulli p=0.3).
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
    @snn_mbt.stdp_confavreux_step(
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
      "✓ STDPConfavreux2025 step function: weights finite after random drive",
    )
  } else {
    println("✗ STDPConfavreux2025 produced NaN/Inf (BUG)")
  }
  if mn >= entry.param.w_min - 1.0e-5F && mx <= entry.param.w_max + 1.0e-5F {
    println("✓ weights clamped to [w_min, w_max]")
  } else {
    println("✗ weight bounds violated")
  }
}