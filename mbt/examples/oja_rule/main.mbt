// oja_rule.mbt — port of Oja_rule.jl.
//
// Original Julia (Oja_rule.jl):
//   G = SNN.Rate(; N = 100)
//   GG = SNN.RateSynapse(G, G; μ = 1.2, p = 1.0)
//   SNN.monitor!(G, [:r])
//   SNN.train!([G], [GG]; duration = 100ms)
//
// MoonBit (v0.10.0): 100 WC (Wilson-Cowan) rate neurons with
// all-to-all RateSynapse connectivity (μ=1.2, p=1.0). The
// dynamics are qualitatively similar to Julia's Rate model.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 100
  let g_pop = @snn_mbt.WilsonCowan::new(n, rng)

  // Random rate synapses (μ=1.2, p=1.0 → all-to-all).
  let gg = @snn_mbt.RateSynapse::new(g_pop, g_pop, 1.2F, 0.0F, 1.0F, rng)

  println("oja_rule.mbt: simplified Oja rate rule port")
  println("  G: " + n.to_string() + " Wilson-Cowan rate neurons")
  println("  GG connections: " + gg.matrix.nnz().to_string() + " (all-to-all)")

  // Simulate 100 ms.
  let dt = 0.125F
  let duration_ms = 100.0F
  let steps : Int = (duration_ms / dt).to_int()
  let g_sum_initial = g_pop.r[0]
  for _ in 0..<steps {
    @snn_mbt.step_wc(g_pop, dt)
    gg.matrix.forward_rate(g_pop.r, g_pop.g)
  }

  // Print final rates.
  let mut mn = g_pop.r[0]
  let mut mx = g_pop.r[0]
  let mut sum = 0.0F
  let mut k = 0
  while k < n {
    if g_pop.r[k] < mn { mn = g_pop.r[k] }
    if g_pop.r[k] > mx { mx = g_pop.r[k] }
    sum = sum + g_pop.r[k]
    k = k + 1
  }
  let mean = sum / Float::from_int(n)
  println("oja_rule.mbt: simulation done (" + duration_ms.to_string() + " ms)")
  println("  r[0] initial: " + g_sum_initial.to_string())
  println("  r[0] final:   " + g_pop.r[0].to_string())
  println("  r[min]:       " + mn.to_string())
  println("  r[max]:       " + mx.to_string())
  println("  r[mean]:      " + mean.to_string())
}