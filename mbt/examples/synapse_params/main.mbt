// synapse_params.mbt — synapse parameter type demo.
//
// Reference Julia (test/pop/parameters.jl "Synaptic parameter types"
// testset):
//   @test DoubleExpSynapse()           isa DoubleExpSynapse
//   @test SingleExpSynapse()           isa SingleExpSynapse
//   @test DeltaSynapse()               isa DeltaSynapse
//   @test CurrentSynapse()             isa CurrentSynapse
//   r = Receptor(E_rev = 0.0, τr = 0.26, τd = 2.0, g0 = 0.73)
//   @test r isa Receptor
//   rs = Receptors(...); @test length(rs) == 4
//   nmda = NMDAVoltageDependency(mg=1.0, b=3.36, k=-0.077)
//   rsyn = ReceptorSynapse(...)
//
// MoonBit (v0.10.39):
//   - Construct DeltaSynapse, SingleExpSynapse, DoubleExpSynapse,
//     CurrentSynapse parameters + their vars.
//   - Run each through a step function with a synthetic spike input
//     (glu[0]=1.0F) and verify the ge/gi decay/integrate as expected.
//   - Print a final summary of the four synapse types' state.

///|
fn main {
  let dt = 0.125F
  let n = 4

  // Construct the four synapse parameter types.
  let delta = @snn_mbt.DeltaSynapse::new()
  let single = @snn_mbt.SingleExpSynapse::new()
  let double = @snn_mbt.DoubleExpSynapse::new()
  let current = @snn_mbt.CurrentSynapse::new()

  println("synapse_params.mbt: four synapse parameter types (v0.10.39)")
  println("")
  println("=== Parameter type defaults (matches Julia @snn_kw defaults) ===")
  println("  DeltaSynapse     : <no params>")
  println(
    "  SingleExpSynapse : tau_e=" +
    single.tau_e.to_string() +
    " tau_i=" +
    single.tau_i.to_string() +
    " e_e=" +
    single.e_e.to_string() +
    " e_i=" +
    single.e_i.to_string(),
  )
  println(
    "  DoubleExpSynapse : tau_re=" +
    double.tau_re.to_string() +
    " tau_de=" +
    double.tau_de.to_string() +
    " tau_ri=" +
    double.tau_ri.to_string() +
    " tau_di=" +
    double.tau_di.to_string(),
  )
  println(
    "  CurrentSynapse   : tau_e=" +
    current.tau_e.to_string() +
    " tau_i=" +
    current.tau_i.to_string(),
  )
  println("")

  // Build vars for each.
  let delta_vars = @snn_mbt.DeltaSynapseVars::new(n)
  let single_vars = @snn_mbt.SingleExpSynapseVars::new(n)
  let double_vars = @snn_mbt.DoubleExpSynapseVars::new(n)
  let current_vars = @snn_mbt.CurrentSynapseVars::new(n)

  // Synthetic input: glu = [1, 0, 0, 0]; gaba = [0, 0, 0, 0].
  let glu : Array[Float] = [1.0F, 0.0F, 0.0F, 0.0F]
  let gaba : Array[Float] = [0.0F, 0.0F, 0.0F, 0.0F]

  // Run a single step for each.
  @snn_mbt.delta_synapse_step(delta_vars, glu, gaba)
  @snn_mbt.single_exp_synapse_step(single_vars, single, glu, gaba, dt)
  @snn_mbt.double_exp_synapse_step(double_vars, double, glu, gaba, dt)
  @snn_mbt.current_synapse_step(current_vars, current, glu, gaba, dt)

  println("=== After 1 step with glu=[1,0,0,0], gaba=[0,0,0,0] ===")
  println("  DeltaSynapse     ge[0] = " + delta_vars.ge[0].to_string() + " (instantaneous)")
  println("  SingleExpSynapse ge[0] = " + single_vars.ge[0].to_string())
  println("  DoubleExpSynapse he[0] = " + double_vars.he[0].to_string())
  println("                  ge[0] = " + double_vars.ge[0].to_string())
  println("  CurrentSynapse   ge[0] = " + current_vars.ge[0].to_string())
  println("")

  // Compute synaptic currents.
  let v : Array[Float] = [-50.0F, -50.0F, -50.0F, -50.0F]
  let syn_curr_single : Array[Float] = Array::make(n, 0.0F)
  let syn_curr_double : Array[Float] = Array::make(n, 0.0F)
  @snn_mbt.conductance_synapse_current(single_vars, single, v, syn_curr_single)
  @snn_mbt.double_exp_conductance_current(
    double_vars, double, v, syn_curr_double,
  )
  let syn_curr_current : Array[Float] = Array::make(n, 0.0F)
  @snn_mbt.current_synapse_current(current_vars, syn_curr_current)

  println("=== Synaptic current at v=-50mV (syn_curr = ge*(v-E_e) for conductance-based) ===")
  println(
    "  SingleExpSynapse syn_curr[0] = " + syn_curr_single[0].to_string(),
  )
  println(
    "  DoubleExpSynapse syn_curr[0] = " + syn_curr_double[0].to_string(),
  )
  println(
    "  CurrentSynapse   syn_curr[0] = " + syn_curr_current[0].to_string(),
  )
  println("")
  println(
    "Note: conductance-based synapses (Single / Double Exp) drive current",
  )
  println("  proportional to (v - E_rev). Current-based synapses drive")
  println("  syn_curr = -(ge - gi) directly (sign convention from Julia).")
}