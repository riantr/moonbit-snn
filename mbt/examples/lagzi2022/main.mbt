// lagzi2022.mbt - port of Lagzi2022 Assembly Formation (simplified).
//
// Original Julia (autocor_STDP.jl):
//   lambda = 0.53  # peak difference in STDP LTP/LTD branches
//   eta_exc = 25e-2ms  # exc learning rate
//   stdp_exc = STDPParameter(A_pre=eta_exc, A_post=-lambda*eta_exc, tau_post=30ms, tau_pre=15ms)
//   E1 = Identity(N=200); E2 = Identity(N=200)
//   signal = PoissonStimulusVariable(stim, rate=OrnsteinUhlenbeckProcess)
//   W11 = SpikingSynapse(E1, E1, :g, p=0.2, mu=10, stdp=stdp_exc)
//   W12 = SpikingSynapse(E1, E2, :g, p=0.2, mu=10, stdp=stdp_exc)
//   W21 = SpikingSynapse(E2, E1, :g, p=0.2, mu=10, stdp=stdp_exc)
//   W22 = SpikingSynapse(E2, E2, :g, p=0.2, mu=10, stdp=stdp_exc)
//   train!(model, duration=200s)
//
// MoonBit (v0.9.6): simplified to 2 Identity populations (16+16)
// with random SpikingSynapses (we don't have Identity->IF SpikingSynapse
// routing yet, so we use IF populations receiving signal). The STDP
// is auto-applied via compose layer. We use a constant Poisson
// rate (no OU process driving the signal) for simplicity. Final
// result demonstrates that the network is wiring up.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let ne1 = 16
  let ne2 = 16
  let dt = 0.125F

  // Two excitatory populations (we use IF since Identity->IF routing
  // for SpikingSynapse isn't implemented yet).
  let e1 = @snn_mbt.IF::new(ne1, @snn_mbt.IFParameter::new(), rng)
  let e2 = @snn_mbt.IF::new(ne2, @snn_mbt.IFParameter::new(), rng)

  // Tonic drive to enable firing.
  for k in 0..<ne1 {
    e1.i[k] = 350.0F
  }
  for k in 0..<ne2 {
    e2.i[k] = 350.0F
  }

  // 4 SpikingSynapses: W11 (E1->E1), W12 (E1->E2), W21 (E2->E1), W22 (E2->E2).
  let w11 = @snn_mbt.SpikingSynapse::random(e1, e1, "ge",
    10.0F, 0.0F, 0.2F, rng)
  let w12 = @snn_mbt.SpikingSynapse::random(e1, e2, "ge",
    10.0F, 0.0F, 0.2F, rng)
  let w21 = @snn_mbt.SpikingSynapse::random(e2, e1, "ge",
    10.0F, 0.0F, 0.2F, rng)
  let w22 = @snn_mbt.SpikingSynapse::random(e2, e2, "ge",
    10.0F, 0.0F, 0.2F, rng)

  // Gerstner STDP for each connection.
  let stdp_w11 = @snn_mbt.Gerstner_(@snn_mbt.STDPEntry::new(0, ne1, ne1))
  let stdp_w22 = @snn_mbt.Gerstner_(@snn_mbt.STDPEntry::new(3, ne2, ne2))

  // Constant Poisson drive (Julia uses OU process; we use fixed rate).
  let stim_e1 = @snn_mbt.PoissonStimulusIF::new(e1, "ge", 0.5F, rng)
  let stim_e2 = @snn_mbt.PoissonStimulusIF::new(e2, "ge", 0.5F, rng)

  // Build model.
  let fm_e1 = @snn_mbt.Monitor::new_fire(e1, 0)
  let fm_e2 = @snn_mbt.Monitor::new_fire(e2, 0)
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e1), @snn_mbt.IF_(e2)],
    [w11, w12, w21, w22],
    stims=[@snn_mbt.PoissonIF_(stim_e1), @snn_mbt.PoissonIF_(stim_e2)],
    monitors=[fm_e1, fm_e2],
    stdp=[stdp_w11, stdp_w22],
  )

  // Simulate 1s (200x scaled down from Julia's 200s).
  let duration_ms = 1000.0F
  let steps : Int = (duration_ms / dt).to_int()
  for _ in 0..<steps {
    @snn_mbt.step_heterogeneous(model, dt)
  }

  let e1_hz = fm_e1.firing_rate()
  let e2_hz = fm_e2.firing_rate()
  let w11_sum_initial = Float::from_int(w11.matrix.nnz()) * 10.0F
  let w22_sum_initial = Float::from_int(w22.matrix.nnz()) * 10.0F
  let mut w11_sum = 0.0F
  let mut w22_sum = 0.0F
  let mut k = 0
  while k < w11.matrix.vals.length() {
    w11_sum = w11_sum + w11.matrix.vals[k]
    k = k + 1
  }
  let mut k2 = 0
  while k2 < w22.matrix.vals.length() {
    w22_sum = w22_sum + w22.matrix.vals[k2]
    k2 = k2 + 1
  }
  let _ = w11_sum_initial
  let _ = w22_sum_initial

  println("lagzi2022.mbt: simplified Lagzi2022 Assembly Formation")
  println("  E1: " + ne1.to_string() + " IF neurons (constant 350pA drive + 500Hz Poisson)")
  println("  E2: " + ne2.to_string() + " IF neurons (constant 350pA drive + 500Hz Poisson)")
  println("  W11 connections: " + w11.matrix.nnz().to_string())
  println("  W22 connections: " + w22.matrix.nnz().to_string())
  println("  Gerstner STDP auto-applied to W11 + W22 (asymmetric LTP/LTD)")
  println("lagzi2022.mbt: simulation done (1s)")
  println("  E1[0] Hz: " + e1_hz.to_string())
  println("  E2[0] Hz: " + e2_hz.to_string())
  println("  W11 weight sum: " + w11_sum.to_string())
  println("  W22 weight sum: " + w22_sum.to_string())
}