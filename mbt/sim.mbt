// sim! — port of SNNModels.jl/src/utils/main.jl (record_zero! and the
// per-timestep loop).
//
// Bit-exact contract: each step does, in this order:
//   1. record!  : append current state of all monitored variables
//   2. stimulate! : apply external currents / inputs
//   3. forward!   : propagate pre-synaptic spikes to post-synaptic glu/gaba
//   4. integrate! : step_synapses, synaptic_current, step_neuron
//   5. update_time! : t += dt, tt += 1

///|
/// A monitor records a chosen variable from a population each step.
/// `recs` is a list of (key, snapshot) pairs; the simplest monitor
/// stores an Array[Float] per (key, neuron) pair.
pub struct Monitor {
  pop : IF
  sym : String
  // For :fire, this is one Float per (record_step, neuron).
  // For :v, same.
  // We use a flat Array[Float] and store the time axis separately.
  data : Array[Float]
  times : Array[Float]
  neuron : Int
  // Recording sample rate: record every step (1).
  rec_step : Int
  // Internal: count of integration steps since last recording.
  // Used to honour rec_step.
  mut step_count : Int
}

///|
/// Initialise a monitor for the `v` variable of a specific neuron.
pub fn Monitor::new_v(pop : IF, neuron : Int) -> Monitor {
  { pop, sym: "v", data: [], times: [], neuron, rec_step: 1, step_count: 0 }
}

///|
/// Initialise a monitor for the `v` variable with a sampling rate.
/// `sr_hz` is the sample rate in Hz (internal units: Hz = 0.001,
/// so 1 kHz = 1.0F in internal units). With dt=0.125F ms,
/// `sr_hz=1.0F` (1 kHz) gives rec_step = 8.
pub fn Monitor::new_v_sr(pop : IF, neuron : Int, sr_hz : Float) -> Monitor {
  // rec_step = 1 / (sr * dt_sim) in simulation steps.
  // sr is in internal Hz units (1 kHz = 1.0F), dt_sim = 0.125F ms.
  // For 1 kHz: 1 / (1.0 * 0.125) = 8.
  // For 8 kHz: 1 / (8.0 * 0.125) = 1.
  let denom = sr_hz * 0.125F
  let rec_step : Int = if denom > 0.0F { (1.0F / denom).to_int() } else { 1 }
  let rec_step = if rec_step < 1 { 1 } else { rec_step }
  { pop, sym: "v", data: [], times: [], neuron, rec_step, step_count: 0 }
}

///|
/// Initialise a monitor for the `fire` variable of a specific neuron.
pub fn Monitor::new_fire(pop : IF, neuron : Int) -> Monitor {
  { pop, sym: "fire", data: [], times: [], neuron, rec_step: 1, step_count: 0 }
}

///|
/// Take one snapshot, honouring rec_step.
pub fn record_one(m : Monitor, t : Float) -> Unit {
  m.step_count = m.step_count + 1
  if m.step_count % m.rec_step != 0 {
    return
  }
  let v = if m.sym == "v" {
    m.pop.v[m.neuron]
  } else if m.sym == "fire" {
    if m.pop.fire[m.neuron] { 1.0F } else { 0.0F }
  } else {
    0.0F
  }
  m.data.push(v)
  m.times.push(t)
}

///|
/// A simple model container: one population + a list of connections.
pub(all) struct Model {
  pops : Array[IF]
  conns : Array[SpikingSynapse]
  monitors : Array[Monitor]
}

///|
/// Record the initial state (t=0).
pub fn record_zero(model : Model) -> Unit {
  for m in model.monitors {
    record_one(m, 0.0F)
  }
}

///|
/// Run one simulation step.
pub fn step_model(model : Model, time : Time) -> Unit {
  // 1a. deliver pending spikes (delays scheduled in prior steps)
  let t_now = get_time(time)
  for c in model.conns {
    deliver_pending_synapse(c, t_now)
  }
  // 1b. forward!  : propagate spikes (no-delay: immediate; with delays:
  // schedule for future delivery)
  for c in model.conns {
    forward_synapse(c, t_now)
  }
  // 2. integrate! : step each population
  let dt = time.dt
  for p in model.pops {
    step_synapses(p, dt)
    synaptic_current(p)
    step_neuron(p, dt)
  }
  // 3. record!
  for m in model.monitors {
    record_one(m, time.t[0])
  }
  // 4. update_time!
  update_time(time, dt)
}

///|
/// Run the simulation for `duration` ms starting from t=0.
pub fn sim_for(model : Model, duration : Float) -> Unit {
  let dt = 0.125F
  let time = Time::new()
  set_dt(time, dt)
  let steps : Int = (duration / dt).to_int()
  record_zero(model)
  for _ in 0..<steps {
    step_model(model, time)
  }
}

///|
/// A model containing AdEx populations.
pub(all) struct AdExModel {
  pops : Array[AdEx]
  conns : Array[SpikingSynapseAdEx]
  monitors : Array[MonitorAdEx]
}

///|
/// A monitor for an AdEx population.
pub struct MonitorAdEx {
  pop : AdEx
  sym : String
  data : Array[Float]
  times : Array[Float]
  neuron : Int
}

///|
pub fn MonitorAdEx::new_v(pop : AdEx, neuron : Int) -> MonitorAdEx {
  { pop, sym: "v", data: [], times: [], neuron }
}

///|
pub fn record_one_adex(m : MonitorAdEx, t : Float) -> Unit {
  let v = if m.sym == "v" {
    m.pop.v[m.neuron]
  } else if m.sym == "fire" {
    if m.pop.fire[m.neuron] { 1.0F } else { 0.0F }
  } else if m.sym == "w" {
    m.pop.w[m.neuron]
  } else {
    0.0F
  }
  m.data.push(v)
  m.times.push(t)
}

///|
/// SpikingSynapse that targets an AdEx post-synaptic neuron.
/// Supports :ge / :he (excitatory, both routed to `glu`) and
/// :gi / :hi / :gaba (inhibitory, all routed to `gaba`). The Julia
/// SNN distinguishes :he from :ge via different rise-time constants,
/// but our simplified DoubleExp model uses the same path for both.
pub struct SpikingSynapseAdEx {
  pre : AdEx
  post : AdEx
  sym : String
  matrix : SparseMatrixCSR
}

///|
pub fn SpikingSynapseAdEx::new(pre : AdEx, post : AdEx, sym : String) -> SpikingSynapseAdEx {
  let matrix = SparseMatrixCSR::empty(pre.n, post.n)
  { pre, post, sym, matrix }
}

///|
/// Build a SpikingSynapseAdEx with random CSR connectivity.
/// `mu`/`sigma` control the Normal weight distribution;
/// `p` is the per-edge Bernoulli connection probability.
pub fn SpikingSynapseAdEx::random(
  pre : AdEx,
  post : AdEx,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> SpikingSynapseAdEx {
  let matrix = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  { pre, post, sym, matrix }
}

///|
/// Build a SpikingSynapseAdEx with an explicit connection rule.
pub fn SpikingSynapseAdEx::random_with_rule(
  pre : AdEx,
  post : AdEx,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rule : ConnectRule,
  rng : Xoshiro,
) -> SpikingSynapseAdEx {
  let matrix = SparseMatrixCSR::random_with_rule(
    pre.n, post.n, mu, sigma, p, rule, rng,
  )
  { pre, post, sym, matrix }
}

///|
pub fn adex_connect(c : SpikingSynapseAdEx, pre : Int, post : Int, w : Float) -> Unit {
  let pre_idx = pre - 1
  let post_idx = post - 1
  c.matrix.set(pre_idx, post_idx, w)
}

///|
pub fn forward_adex_synapse(c : SpikingSynapseAdEx) -> Unit {
  let target = if c.sym == "ge" || c.sym == "he" {
    c.post.glu
  } else {
    c.post.gaba
  }
  c.matrix.forward(c.pre.fire, target)
}

///|
pub fn step_adex_model(model : AdExModel, time : Time) -> Unit {
  for c in model.conns {
    forward_adex_synapse(c)
  }
  let dt = time.dt
  for p in model.pops {
    adex_step_synapses(p, dt)
    adex_synaptic_current(p)
    step_adex(p, dt)
  }
  for m in model.monitors {
    record_one_adex(m, time.t[0])
  }
  update_time(time, dt)
}

///|
pub fn adex_sim_for(model : AdExModel, duration : Float) -> Unit {
  let dt = 0.125F
  let time = Time::new()
  set_dt(time, dt)
  let steps : Int = (duration / dt).to_int()
  for m in model.monitors {
    record_one_adex(m, 0.0F)
  }
  for _ in 0..<steps {
    step_adex_model(model, time)
  }
}
