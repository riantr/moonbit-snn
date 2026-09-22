// compartment_synapse.mbt — port of SpikingNeuralNetworks.jl/examples/
// multicompartment / Tripod + dendritic inputs (simplified).
//
// Exercises `CompartmentSynapseBall` / `CompartmentSynapseTripod`:
//   - IF pre → BallAndStick :soma (μ=2.0 nS, p=1.0)
//   - IF pre → BallAndStick :d   (μ=1.0 nS, p=1.0)
//   - IF pre → Tripod     :d1   (μ=1.5 nS, p=1.0)
//
// Demonstrates that pre-synaptic spikes are routed to the correct
// compartment buffer (glu_s / glu_d for BallAndStick, glu_d1 for
// Tripod) without leaking into other compartments.
//
// Bit-exact note: this is a structural exercise, not a network
// simulation. The neural dynamics step is skipped — we just verify
// routing semantics.

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()

  // Build IF pre (1 neuron) — will be forced to fire each step.
  let if_param = @snn_mbt.IFParameter::new()
  let pre = @snn_mbt.IF::new(1, if_param, rng)

  // Build BallAndStick (2 neurons) + Tripod (2 neurons).
  let soma_param = @snn_mbt.AdExParameter::new()
  let ball = @snn_mbt.BallAndStick::new(2, soma_param, rng)
  let tripod = @snn_mbt.Tripod::new(2, soma_param, rng)

  // Three compartment-targeted synapses:
  //   - IF → BallAndStick :soma (μ=2.0)
  //   - IF → BallAndStick :d    (μ=1.0)
  //   - IF → Tripod     :d1   (μ=1.5)
  let cs_ball_s = @snn_mbt.CompartmentSynapseBall::random(
    pre, ball, "ge", "soma", 2.0F, 0.0F, 1.0F, rng,
  )
  let cs_ball_d = @snn_mbt.CompartmentSynapseBall::random(
    pre, ball, "ge", "d", 1.0F, 0.0F, 1.0F, rng,
  )
  let cs_trip_d1 = @snn_mbt.CompartmentSynapseTripod::random(
    pre, tripod, "ge", "d1", 1.5F, 0.0F, 1.0F, rng,
  )

  println("compartment_synapse.mbt: 1 IF pre → multi-compartment posts")
  println("  connections:")
  println("    IF → BallAndStick :soma  (μ=2.0, p=1.0): " + cs_ball_s.matrix.nnz().to_string() + " edges")
  println("    IF → BallAndStick :d     (μ=1.0, p=1.0): " + cs_ball_d.matrix.nnz().to_string() + " edges")
  println("    IF → Tripod       :d1    (μ=1.5, p=1.0): " + cs_trip_d1.matrix.nnz().to_string() + " edges")

  // Force pre to fire and forward through each synapse.
  pre.fire[0] = true
  @snn_mbt.forward_compartment_ball(cs_ball_s, 0.0F)
  @snn_mbt.forward_compartment_ball(cs_ball_d, 0.0F)
  @snn_mbt.forward_compartment_tripod(cs_trip_d1, 0.0F)

  println("")
  println("After one pre-synaptic spike (forward at t=0):")
  println("  BallAndStick[0] glu_s = " + ball.glu_s[0].to_string() + " (expect 2.0)")
  println("  BallAndStick[0] glu_d = " + ball.glu_d[0].to_string() + " (expect 1.0)")
  println("  BallAndStick[0] gaba_s = " + ball.gaba_s[0].to_string() + " (expect 0.0)")
  println("  BallAndStick[0] gaba_d = " + ball.gaba_d[0].to_string() + " (expect 0.0)")
  println("  Tripod[0] glu_d1 = " + tripod.glu_d1[0].to_string() + " (expect 1.5)")
  println("  Tripod[0] glu_s = " + tripod.glu_s[0].to_string() + " (expect 0.0; routing target was :d1)")
  println("  Tripod[0] glu_d2 = " + tripod.glu_d2[0].to_string() + " (expect 0.0)")

  // Verify routing invariants.
  let mut pass = true
  let eps = 1.0e-5F
  if (ball.glu_s[0] - 2.0F).abs() > eps { pass = false }
  if (ball.glu_s[1] - 2.0F).abs() > eps { pass = false }
  if (ball.glu_d[0] - 1.0F).abs() > eps { pass = false }
  if (ball.glu_d[1] - 1.0F).abs() > eps { pass = false }
  if ball.gaba_s[0] != 0.0F || ball.gaba_s[1] != 0.0F { pass = false }
  if ball.gaba_d[0] != 0.0F || ball.gaba_d[1] != 0.0F { pass = false }
  if (tripod.glu_d1[0] - 1.5F).abs() > eps { pass = false }
  if tripod.glu_s[0] != 0.0F || tripod.glu_d2[0] != 0.0F { pass = false }
  if pass {
    println("")
    println("✓ routing invariants hold: each synapse delivered to its")
    println("  target compartment only; no cross-talk between buffers.")
  } else {
    println("")
    println("✗ routing invariant violated — see values above.")
  }
}