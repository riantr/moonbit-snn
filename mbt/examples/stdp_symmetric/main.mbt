// stdp_symmetric.mbt — STDPSymmetric (Festa et al. 2024 symmetric
// inhibitory STDP) demo.
//
// Reference Julia (test/syn/plasticity_params.jl "STDPSymmetric /
// STDPAntiSymmetric" testset + STDP_structured.jl):
//   s = STDPSymmetric()
//   @test s isa STDPSymmetric
//   @test hasproperty(s, :A_x)
//
// MoonBit (v0.10.36):
//   - 4x4 dense SpikingSynapse + STDPEntrySymmetric.
//   - Drive with random pre/post firing for 100 steps.
//   - Show that all weights remain finite and within [w_min, w_max].
//
// Bit-exact note: rule body matches Julia's STDPSymmetric with
// a_x=a_y=3e-2, tau_x=50ms, tau_y=500ms, alpha_pre=alpha_post=0,
// w_max=30, w_min=0. Continuous-time Euler decay of 4 traces
// (tr_x, tr_y, to_x, to_y), all bumped by 1.0F on the appropriate
// spike (tr_x/tr_y on pre; to_x/to_y on post).

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let dt = 0.125F
  let n_pre = 4
  let n_post = 4

  let n_conn = n_pre * n_post
  let w : Array[Float] = Array::make(n_conn, 1.0F)
  let rowptr : Array[Int] = [0, 4, 8, 12, 16]
  let colptr : Array[Int] = [
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
  ]
  let pre_fire : Array[Bool] = Array::make(n_pre, false)
  let post_fire : Array[Bool] = Array::make(n_post, false)

  let entry = @snn_mbt.STDPSymmetric::new()
  let _ = entry
  let e = @snn_mbt.STDPEntrySymmetric::new(0, n_pre, n_post)
  println("stdp_symmetric.mbt: STDPSymmetric (Festa 2024) demo")
  println("")
  println("=== STDPSymmetric defaults ===")
  println("  a_x = " + e.param.a_x.to_string())
  println("  a_y = " + e.param.a_y.to_string())
  println("  tau_x = " + e.param.tau_x.to_string())
  println("  tau_y = " + e.param.tau_y.to_string())
  println("  alpha_pre = " + e.param.alpha_pre.to_string())
  println("  alpha_post = " + e.param.alpha_post.to_string())
  println("  w_max = " + e.param.w_max.to_string())
  println("  w_min = " + e.param.w_min.to_string())
  println("")
  println("=== Driving 4x4 dense synapse for 100 steps with random pre/post firing ===")

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
    @snn_mbt.stdp_symmetric_step(
      w, pre_fire, post_fire, colptr, rowptr, e.vars, e.param, t_now, dt,
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
  println("  w_min bound = " + e.param.w_min.to_string())
  println("  w_max bound = " + e.param.w_max.to_string())
  println("")
  if finite {
    println("✓ STDPSymmetric: weights finite after random drive")
  } else {
    println("✗ STDPSymmetric produced NaN/Inf (BUG)")
  }
  if mn >= e.param.w_min - 1.0e-5F && mx <= e.param.w_max + 1.0e-5F {
    println("✓ weights clamped to [w_min, w_max]")
  } else {
    println("✗ weight bounds violated")
  }
}