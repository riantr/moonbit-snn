// stdp_compose_demo.mbt - simplified port of stdp_demo.jl using the
// compose layer's auto STDP integration (v0.7.8+).
//
// Original Julia (stdp_demo.jl):
//   N = 100
//   E1 = IF(N=N); E2 = IF(N=N)
//   EE = SpikingSynapse(E1, E2, :ge; conn = (mu = 0.5, p = 1.0))
//   for n = 1:N; connect!(EE, n, n); end
//   for t = 1:N
//     E1.v[t] = 100     # force E1[t] to fire
//     E2.v[N-t+1] = 100 # force E2[N-t+1] to fire
//     train!(model, duration = 5ms)
//   end
//   # Plot delta-W from EE.records[:W]
//
// MoonBit (v0.7.8): 4 IF neurons + identity EE connection. The
// user constructs the HeterogeneousModel with stdp=[entry] and
// `heterogeneous_sim_for` applies the plasticity automatically -
// no manual stdp_step calls required.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 4
  let e1 = @snn_mbt.IF::new(n, @snn_mbt.IFParameter::new(), rng)
  let e2 = @snn_mbt.IF::new(n, @snn_mbt.IFParameter::new(), rng)
  // Identity synapse: E1[i] -> E2[i].
  let ee = @snn_mbt.SpikingSynapse::new(e1, e2, "ge")
  for k in 0..<n {
    @snn_mbt.spiking_connect(ee, k + 1, k + 1, 1.0F)
  }

  // STDP entry for the EE connection at conns[0].
  let stdp_entry = @snn_mbt.Gerstner_(@snn_mbt.STDPEntry::new(0, n, n))

  println("stdp_compose_demo.mbt: STDP pairing via compose layer (Gerstner 1996)")
  println("  initial weights: " + ee.matrix.vals.to_string())

  // Build the model with STDP auto-integration.
  let model = @snn_mbt.compose(
    [@snn_mbt.IF_(e1), @snn_mbt.IF_(e2)],
    [ee],
    stdp=[stdp_entry],
  )

  let pair_steps : Int = (5.0F / 0.125F).to_int()  // 40 steps x 0.125ms = 5ms
  for t_step in 0..<n {
    let pre_idx = t_step
    let post_idx = n - 1 - t_step
    // Force fire at the start of the pairing window.
    e1.v[pre_idx] = 100.0F
    e2.v[post_idx] = 100.0F
    let mut s = 0
    while s < pair_steps {
      @snn_mbt.step_heterogeneous(model, 0.125F)
      s = s + 1
    }
  }

  println("  final weights: " + ee.matrix.vals.to_string())
  let initial_sum = Float::from_int(n) * 1.0F
  let final_sum : Float = {
    let mut acc = 0.0F
    let mut k = 0
    while k < n { acc = acc + ee.matrix.vals[k]; k = k + 1 }
    acc
  }
  let delta = final_sum - initial_sum
  println("  total delta-W across " + n.to_string() + " synapses: " + delta.to_string())
}