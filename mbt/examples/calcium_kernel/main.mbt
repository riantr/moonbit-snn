// calcium_kernel.mbt — port of CalciumPlasticity_kernel.jl (partial).
//
// Original Julia (CalciumPlasticity_kernel.jl):
//   stdp_param = STDPParameter(A_pre=-5e-1, A_post=5e-1, τpre=20ms, τpost=15ms)
//   SNN.stdp_kernel(stdp_param)
//   SNN.stdp_weight_decorrelated(stdp_param) |> violin
//
//   stdp_param = STDPParameter(A_pre=5e-2, A_post=-5e-2, τpre=15ms, τpost=25ms)
//   SNN.stdp_kernel(stdp_param)
//   ... etc.
//
//   stdp_param = STDPMexicanHat(A=-2e-1, τ=25ms)
//   ...
//
//   stdp_param = iSTDPTime()
//   ...
//
//   stdp_param = SymmetricSTDP(αpre=0.0f0, A_x=0.1, A_y=0.1)
//   ... etc.
//
// MoonBit (v0.10.1): plot the two STDPGerstner variants and the
// STDPMexicanHat kernel using `stdp_kernel_plot` / `stdp_mexican_hat_plot`.
// iSTDPTime and SymmetricSTDP are still TODO.

///|
fn main {
  println("calcium_kernel.mbt: STDP kernel visualisations + decorrelated weights")
  println("")

  // Kernel 1: reversed polarity (A_pre negative, A_post positive).
  let p1 : @snn_mbt.STDPGerstner = {
    a_pre: -0.5F,
    a_post: 0.5F,
    tau_pre: 20.0F,
    tau_post: 15.0F,
    w_max: 1000.0F,
    w_min: -1000.0F,
  }
  println("--- Reversed-polarity Gerstner (LTD on LTP side, LTP on LTD side) ---")
  @snn_mbt.stdp_kernel_plot(p1, t_max=80.0F, width=60, height=12)
  println("  stdp_weight_decorrelated: " + @snn_mbt.stdp_weight_decorrelated(p1).to_string())
  println("")

  // Kernel 2: classical Gerstner (asymmetric, classical 1996).
  let p2 : @snn_mbt.STDPGerstner = {
    a_pre: 0.05F,
    a_post: 0.05F,
    tau_pre: 15.0F,
    tau_post: 25.0F,
    w_max: 1000.0F,
    w_min: -1000.0F,
  }
  println("--- Classical Gerstner 1996 (asymmetric) ---")
  @snn_mbt.stdp_kernel_plot(p2, t_max=80.0F, width=60, height=12)
  println("  stdp_weight_decorrelated: " + @snn_mbt.stdp_weight_decorrelated(p2).to_string())
  println("")

  // Kernel 3: STDPMexicanHat (decorrelating sombrero kernel).
  let p3 : @snn_mbt.STDPMexicanHat = {
    a: -0.2F,
    tau: 25.0F,
    w_max: 1000.0F,
    w_min: -1000.0F,
  }
  println("--- STDPMexicanHat (sombrero / decorrelating) ---")
  @snn_mbt.stdp_mexican_hat_plot(p3, x_max=5.0F, width=60, height=12)
  println("  MexicanHat(0) = 1.0, MexicanHat(1) = 0, MexicanHat(x>1) < 0")
  println("")

  println("Note: iSTDPTime and SymmetricSTDP kernels are still TODO.")
}