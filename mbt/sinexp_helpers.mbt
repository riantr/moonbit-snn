// Small helper functions used by examples that drive AdExSinExp
// posts from IF pre-neurons (since `SpikingSynapse` is currently
// hardcoded to IF-to-IF in the connection_spiking module).
//
// These helpers provide:
//   - `make_random_weights(n_pre, n_post, mu, p, rng)` — Bernoulli
//     connectivity, returns dense n_pre × n_post Array[Float] of
//     connection weights (0 for non-connections, mu for connections).
//   - `route_pre_to_post(pre, post, weights, exc, inh)` — for each
//     pre-neuron that fires, add its outgoing weights to post.glu
//     (exc) or post.gaba (inh).
//   - `simulate_step_if(pop, dt)` — convenience wrapper for the
//     three IF integrate steps (step_synapses + synaptic_current
//     + step_neuron).
//   - `count_nnz(arr)` — count non-zero entries in Array[Float].

///|
/// Build a dense n_pre × n_post weight matrix with Bernoulli(p)
/// connectivity. Connections get weight `mu`; non-connections are 0.
pub fn make_random_weights(
  n_pre : Int,
  n_post : Int,
  mu : Float,
  p : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let total = n_pre * n_post
  let arr : Array[Float] = Array::make(total, 0.0F)
  for k in 0..<total {
    if next_f32(rng) < p {
      arr[k] = mu
    }
  }
  arr
}

///|
/// Route pre-synaptic spikes to post-synaptic glu/gaba buffers.
/// `weights` is a dense n_pre × n_post matrix (row-major). For each
/// pre-neuron that fires, the row's weights are added to `post.glu`
/// (if `exc == true`) or `post.gaba` (if `inh == true`).
pub fn route_pre_to_post(
  pre : IF,
  post : AdExSinExp,
  weights : Array[Float],
  exc : Bool,
  inh : Bool,
) -> Unit {
  let n_pre = pre.n
  let n_post = post.n
  for i in 0..<n_pre {
    if pre.fire[i] {
      for j in 0..<n_post {
        let w = weights[i * n_post + j]
        if w != 0.0F {
          if exc {
            post.glu[j] = post.glu[j] + w
          }
          if inh {
            post.gaba[j] = post.gaba[j] + w
          }
        }
      }
    }
  }
}

///|
/// Run one IF integrate step (synapse decay + synaptic current +
/// neuron update). Convenience wrapper for the three-step IF
/// integration.
pub fn simulate_step_if(pop : IF, dt : Float) -> Unit {
  step_synapses(pop, dt)
  synaptic_current(pop)
  step_neuron(pop, dt)
}

///|
/// Count non-zero entries in a Float array.
pub fn count_nnz(arr : Array[Float]) -> Int {
  let mut n = 0
  for k in 0..<arr.length() {
    if arr[k] != 0.0F {
      n = n + 1
    }
  }
  n
}

///|
/// Count the number of 1.0F entries in a fire monitor's data array
/// (each entry is 0.0F or 1.0F per step). This equals the total
/// spike count for the monitored neuron.
pub fn monitor_count_spikes(data : Array[Float]) -> Int {
  let mut n = 0
  for k in 0..<data.length() {
    if data[k] > 0.0F {
      n = n + 1
    }
  }
  n
}

///|
/// Compute the firing rate in Hz from a fire monitor's data array
/// (each entry is 0.0F or 1.0F per step). With dt=0.125ms, each
/// step is 0.000125s = 0.125ms = 0.125/1000 s.
/// rate_Hz = count / (n_steps * dt) = count / (n_steps * 0.000125)
/// Internal rate (in 0.001 Hz units) = count / (n_steps * 0.125)
pub fn monitor_firing_rate(data : Array[Float]) -> Float {
  let n = data.length()
  if n == 0 {
    return 0.0F
  }
  let count = monitor_count_spikes(data)
  // Each step is 0.125ms. Total sim time = n * 0.125 ms.
  // Hz = spikes/sec = count / (n * 0.125 / 1000) = count * 8000 / n
  let rate_hz : Float = Float::from_int(count) * 8000.0F / Float::from_int(n)
  rate_hz
}
