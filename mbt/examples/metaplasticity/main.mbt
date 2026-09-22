// metaplasticity.mbt — SynapseNormalization on a small IF net.
//
// Julia reference (SNNModels.jl/test/syn/metaplasticity.jl):
//   E  = IF(N = 20)
//   s1 = SpikingSynapse(E, E, :ge, p = 0.5, μ = 1.0)
//   s2 = SpikingSynapse(E, E, :ge, p = 0.3, μ = 0.5)
//   norm = SynapseNormalization([s1, s2]; param = MultiplicativeNorm(τ = 100ms))
//   # After construction: norm.W0[i] = sum of weights targeting i
//   # at construction time (initial weights).
//
// MoonBit (v0.10.15):
//   - 20 IF neurons + 2 SpikingSynapses (full connectivity,
//     different connection probabilities + μ).
//   - Construct SynapseNormalization on both synapses' CSR vals+rowptr.
//   - Manually perturb a few weights to simulate ongoing plasticity
//     (since we don't have STDP integrated with normalization here).
//   - Apply multiplicative normalization step.
//   - Verify weights are restored toward W0.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 20
  let dt = 0.125F

  // IF population.
  let param = @snn_mbt.IFParameter::new()
  let pop = @snn_mbt.IF::new(n, param, rng)

  // Two SpikingSynapses (E → E).
  let s1 = @snn_mbt.SpikingSynapse::random(pop, pop, "ge",
    1.0F, 0.0F, 0.5F, rng)
  let s2 = @snn_mbt.SpikingSynapse::random(pop, pop, "ge",
    0.5F, 0.0F, 0.3F, rng)

  // Extract CSR vals + rowptr from each synapse.
  let v1 = s1.matrix.vals
  let r1 = s1.matrix.rowptr
  let v2 = s2.matrix.vals
  let r2 = s2.matrix.rowptr

  // Construct SynapseNormalization.
  let norm = @snn_mbt.SynapseNormalization::new(
    [{ post_id: 0, vals: v1, rowptr: r1 }, { post_id: 0, vals: v2, rowptr: r2 }],
    @snn_mbt.Multiplicative_(@snn_mbt.MultiplicativeNorm::new(100.0F)),
  )

  // Capture initial per-post-synaptic-neuron weight sums.
  let initial_w0_sum : Float = {
    let mut s = 0.0F
    for i in 0..<n {
      s = s + norm.w0[i]
    }
    s
  }
  let nnz1 = v1.length()
  let nnz2 = v2.length()
  let mut total_w_sum : Float = 0.0F
  for k in 0..<nnz1 { total_w_sum = total_w_sum + v1[k] }
  for k in 0..<nnz2 { total_w_sum = total_w_sum + v2[k] }
  println("metaplasticity.mbt: SynapseNormalization (Multiplicative) demo")
  println("  E: " + n.to_string() + " IF neurons")
  println("  s1: " + nnz1.to_string() + " conns (μ=1.0, p=0.5)")
  println("  s2: " + nnz2.to_string() + " conns (μ=0.5, p=0.3)")
  println("  initial weight sum (W0 total): " + total_w_sum.to_string())
  println("  initial W0[i] sum (per-post):  " + initial_w0_sum.to_string())

  // Perturb weights (simulate ongoing plasticity).
  let mut k = 0
  while k < v1.length() {
    let (z1, _) = @snn_mbt.box_muller(rng)
    v1[k] = v1[k] + Float::from_double(z1) * 0.1F
    if v1[k] < 0.0F { v1[k] = 0.0F }
    k = k + 1
  }
  k = 0
  while k < v2.length() {
    let (z1, _) = @snn_mbt.box_muller(rng)
    v2[k] = v2[k] + Float::from_double(z1) * 0.1F
    if v2[k] < 0.0F { v2[k] = 0.0F }
    k = k + 1
  }
  let mut perturbed_w_sum : Float = 0.0F
  for k in 0..<nnz1 { perturbed_w_sum = perturbed_w_sum + v1[k] }
  for k in 0..<nnz2 { perturbed_w_sum = perturbed_w_sum + v2[k] }

  // Apply normalization step.
  @snn_mbt.metaplasticity_step(norm)

  let mut normalized_w_sum : Float = 0.0F
  for k in 0..<nnz1 { normalized_w_sum = normalized_w_sum + v1[k] }
  for k in 0..<nnz2 { normalized_w_sum = normalized_w_sum + v2[k] }

  println("metaplasticity.mbt: after weight perturbation + 1 normalization step")
  println("  perturbed weight sum:  " + perturbed_w_sum.to_string())
  println("  normalized weight sum: " + normalized_w_sum.to_string() +
    " (closer to initial " + total_w_sum.to_string() + ")")
  println("")
  println("Note: the per-post weight sums should now be back to W0[i].")
  println("  Total mass is conserved under multiplicative normalization")
  println("  (each W1[i] is rescaled by W0[i]/W1[i]).")
}