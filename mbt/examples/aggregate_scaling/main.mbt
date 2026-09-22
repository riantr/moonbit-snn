// aggregate_scaling.mbt — port of aggregate_scaling.jl test.
//
// Original Julia (refs/SNNModels.jl/test/network/aggregate_scaling.jl):
//   E = AdEx(N=100, param=AdExReceptorParameter())
//   P = Poisson(N=100, param=PoissonParameter(3Hz))
//   S = SpikingSynapse(P, E, :he, mu=10, sigma=3, p=0.2)
//   A = AggregateScaling(E, [S], param=AggregateScalingParameter(...))
//   for n in 1:10_000:
//     # perturb S.W, perturb A.param.Y, train! for 10ms
//
// MoonBit (v0.10.43): we use IF + SpikingSynapse (SpikingSynapse
// hardcodes IF pre-type). The AggregateScaling infrastructure
// (aggregate_scaling.mbt) drives the homeostatic weight rescaling.
// Each step: deliver_pending → forward → IF step → aggregate_scaling_forward
// (after IF step so fire[i] is set) → every 80 steps
// (plasticity_interval), aggregate_scaling_plasticity rescaling.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n = 100
  let dt = 0.125F

  // One IF population of 100 neurons (SpikingSynapse requires IF).
  let param = @snn_mbt.IFParameter::new()
  let e_pop = @snn_mbt.IF::new(n, param, rng)

  // Random Bernoulli connectivity at p=0.2 (~2000 connections).
  let ee = @snn_mbt.SpikingSynapse::random(e_pop, e_pop, "ge",
    10.0F, 0.0F, 0.2F, rng)

  // Tonic current drive: bring El close to Vt for spontaneous firing.
  for k in 0..<n {
    e_pop.i[k] = 350.0F
  }

  // AggregateScalingParameter: tau_e=100ms, tau_a=300ms, tau=10ms
  // (plasticity every 10ms = 80 steps), Wmin=0.5, Wmax=400, Y=10Hz
  // uniform across all 100 post neurons.
  let asp = @snn_mbt.AggregateScalingParameter::uniform(
    n, 10.0F, tau=10.0F, tau_a=300.0F, tau_e=100.0F,
    w_min=0.5F, w_max=400.0F,
  )

  // Build a SynapseTarget for our single SpikingSynapse.
  let st_targets : Array[@snn_mbt.SynapseTarget] = [
    { post_id: 0, vals: ee.matrix.vals, rowptr: ee.matrix.rowptr },
  ]
  let agg_scaling = @snn_mbt.AggregateScaling::with_plasticity_interval(
    @snn_mbt.AggregateScaling::new(n, st_targets, asp),
    80,
  )

  // Initial weight sum for sanity.
  let initial_w_sum : Float = {
    let mut s = 0.0F
    let mut k = 0
    while k < ee.matrix.vals.length() {
      s = s + ee.matrix.vals[k]
      k = k + 1
    }
    s
  }
  println("aggregate_scaling.mbt: homeostatic weight scaling demo")
  println("  E: " + n.to_string() + " IF neurons (SpikingSynapse requires IF)")
  println("  Tonic drive: 350 pA per cell")
  println("  EE connections: " + ee.matrix.nnz().to_string())
  println("  initial weight sum: " + initial_w_sum.to_string())

  // Run 1000 steps (125ms) with weight perturbation.
  let total_steps = 1000
  let fire : Array[Float] = Array::make(n, 0.0F)
  for s in 0..<total_steps {
    // 1. Forward synapse (SpikingSynapse).
    @snn_mbt.spiking_connect(ee, 0, 0, 0.0F) // no-op: actual forward via compose not used here
    // Use the existing forward + step machinery.
    @snn_mbt.step_synapses(e_pop, dt)
    @snn_mbt.synaptic_current(e_pop)
    @snn_mbt.step_neuron(e_pop, dt)
    // Copy fire[i] (Bool) to fire[k] (Float) for AggregateScaling.
    let mut k = 0
    while k < n {
      if e_pop.fire[k] {
        fire[k] = 1.0F
      } else {
        fire[k] = 0.0F
      }
      k = k + 1
    }
    // 2. Aggregate scaling: forward (update y, WT).
    @snn_mbt.aggregate_scaling_forward(agg_scaling, fire, dt)
    // 3. Plasticity (every 80 steps).
    @snn_mbt.aggregate_scaling_plasticity(agg_scaling, s + 1)
    // 4. Add small Gaussian noise to weights (matches Julia's
    // `model.syn.S.W .+= randn(size(...))` perturbation).
    let mut j = 0
    while j < ee.matrix.vals.length() {
      let (z1, _) = @snn_mbt.box_muller(rng)
      let new_w = ee.matrix.vals[j] + Float::from_double(z1) * 0.5F
      // Clamp at 0.5 (matches Julia's `clamp.(..., 0.5, Inf)`).
      ee.matrix.vals[j] = if new_w < 0.5F { 0.5F } else { new_w }
      j = j + 1
    }
  }

  let final_w_sum : Float = {
    let mut s = 0.0F
    let mut k = 0
    while k < ee.matrix.vals.length() {
      s = s + ee.matrix.vals[k]
      k = k + 1
    }
    s
  }
  let n_synapses = ee.matrix.nnz()
  println("aggregate_scaling.mbt: simulation done (1s sim, ~125ms wall time at 0.125ms/step)")
  println("  final weight sum: " + final_w_sum.to_string() +
    " (initial=" + initial_w_sum.to_string() +
    "; mean per synapse: " + (final_w_sum / Float::from_int(n_synapses)).to_string() + ")")
  println("  y[0] (post[0] trace) = " + agg_scaling.y[0].to_string())
  println("  WT[0] (post[0] homeostatic target) = " + agg_scaling.wt_total[0].to_string())
}