// rate_net.mbt — port of rate_net.jl.
//
// Julia:
//   G = SNN.Rate(N=100)
//   GG = SNN.RateSynapse(G, G; μ=1.2, p=1.0)
//   SNN.monitor!(G, [(:r, [1, 50, 10, 20])])
//   model = compose(G=G, GG=GG)
//   SNN.sim!(model, 100ms)
//   SNN.vecplot(G, :r)
//
// MoonBit: simple homogeneous rate network with all-to-all
// self-connectivity. We track r[0], r[10], r[20], r[50] across
// 100 ms and print the time trace summary.
//
// Note: the Julia RateSynapse normalises weights by sqrt(p*N) so
// the network stays bounded even with strong μ. Our port omits
// the normalisation (simpler), so we use a smaller μ=0.05 to
// avoid saturation. Result is a slow drift from random init toward
// a stable fixed point.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 100
  let g = @snn_mbt.WilsonCowan::new(n, rng)
  // All-to-all self-connection with μ=1.2, p=1.0. The
  // RateSynapse constructor applies Julia's sqrt(N) normalisation
  // so the network stays bounded.
  let gg = @snn_mbt.RateSynapse::new(g, g, 1.2F, 0.0F, 1.0F, rng)
  println("rate_net.mbt: network built")
  println("  G: " + n.to_string() + " WilsonCowan rate neurons")
  println("  GG connections: " + gg.matrix.nnz().to_string())
  println("  μ=1.2, p=1.0 (all-to-all, with sqrt(N) normalisation)")

  // Track rate samples at indices [0, 9, 19, 49] across the sim.
  let track_idx : Array[Int] = [0, 9, 19, 49]
  let track_vals : Array[Array[Float]] = Array::make(track_idx.length(), [])
  for i in 0..<track_idx.length() {
    track_vals[i] = []
  }

  // 100 ms = 800 steps at dt=0.125F.
  let dt = 0.125F
  let total_steps = 800
  for step in 0..<total_steps {
    // 1. forward (rate → synaptic g)
    @snn_mbt.forward_rate_synapse(gg)
    // 2. integrate
    @snn_mbt.step_wc(g, dt)
    // Sample r every 10 steps.
    if step % 10 == 0 {
      for i in 0..<track_idx.length() {
        track_vals[i].push(g.r[track_idx[i]])
      }
    }
  }
  println("rate_net.mbt: simulation done (100ms)")
  for i in 0..<track_idx.length() {
    let samples = track_vals[i]
    let first = samples[0]
    let last = samples[samples.length() - 1]
    println(
      "  r[" + track_idx[i].to_string() + "]: first=" + first.to_string() +
      " last=" + last.to_string() + " samples=" + samples.length().to_string(),
    )
  }
}