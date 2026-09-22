// stdp_kernel.mbt — port of STDP_kernel.jl (visualisation example).
//
// Original Julia (STDP_kernel.jl):
//   stdp_param = SNN.STDPGerstner(A_pre=5e-2, A_post=-5e-2, τpre=15ms,
//                                    τpost=25ms, Wmax=Inf, Wmin=-Inf)
//   p1 = SNN.stdp_kernel(stdp_param, title="Gerstner STDP", fill=true)
//
//   stdp_param = SNN.STDPMexicanHat(A=2e-1, τ=25ms, ...)
//   p2 = SNN.stdp_kernel(stdp_param, ...)
//
//   stdp_param = SNN.STDPSymmetric(αpre=0.0f0, A_x=1, A_y=1, ...)
//   p3 = SNN.stdp_kernel(stdp_param, ...)
//
//   stdp_param = SNN.STDPAntiSymmetric(αpre=0.0f0, A_x=1, A_y=1, ...)
//   p4 = SNN.stdp_kernel(stdp_param, ...)
//
// MoonBit (v0.9.0): we plot 2 of the 4 kernels — Gerstner (asymmetric,
// pre-only-side LTP) and a symmetric variant — using our
// `stdp_kernel_plot` function. We don't have STDPAntiSymmetric
// or STDPMexicanHat yet — these would require additional impl
// work (v0.9.1+ TODO).

///|
fn main {
  println("stdp_kernel.mbt: STDP kernel visualisations")
  println("")
  // Kernel 1: classical Gerstner 1996 (pre-only LTP, post-only LTD).
  let p1 = @snn_mbt.STDPGerstner::new()
  // Override defaults with Julia's typical values: A_pre = 0.05,
  // A_post = 0.05 (asymmetric: pre-only LTP, post-only LTD),
  // τ_pre = 15ms, τ_post = 25ms.
  let custom_p1 : @snn_mbt.STDPGerstner = {
    a_pre: 0.05F,
    a_post: 0.05F,
    tau_pre: 15.0F,
    tau_post: 25.0F,
    w_max: 1000.0F,
    w_min: -1000.0F,
  }
  println("--- Gerstner 1996 (asymmetric, exponential) ---")
  @snn_mbt.stdp_kernel_plot(custom_p1, t_max=80.0F, width=60, height=12)
  println("")

  // Kernel 2: symmetric kernel (both pre-only and post-only LTP).
  // We approximate this with custom_a_post = -a_pre (so negative Δt
  // → LTP, mimicking a "symmetric" Gerstner).
  let custom_p2 : @snn_mbt.STDPGerstner = {
    a_pre: 0.05F,
    a_post: -0.05F,  // negative → Δt<0 gives LTP (post before pre)
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 1000.0F,
    w_min: -1000.0F,
  }
  println("--- Symmetric Gerstner (mirror image) ---")
  @snn_mbt.stdp_kernel_plot(custom_p2, t_max=80.0F, width=60, height=12)
  println("")

  // Suppress unused warning for p1 default.
  let _ = p1
}