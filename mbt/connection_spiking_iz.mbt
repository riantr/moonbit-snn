// SpikingSynapseIZ — port of SNNModels.jl's IZ-targeting SpikingSynapse.
//
// In Julia, SpikingSynapse(E, E, :v; conn=...) targets the IZ
// population. The IZ's `ge` and `gi` arrays are the synaptic
// conductances (in nS), and integration uses:
//   v[i] += dt * (ge[i] * (Ee - v[i]) + gi[i] * (Ei - v[i]))
//
// We model :v coupling as: when pre fires, add `w` to post's `ge`
// (or `gi` if sym is :gi). The IZ's ge/gi then decay over time
// (per `step_iz`'s first loop). The IZ coupling is effectively a
// current-based synapse: `w * (Ee - v)` is the current per spike.

///|
/// SpikingSynapse targeting an IZ post-synaptic neuron.
/// `sym` ∈ {"ge", "gi"}: excitatory routes to `post.ge`, inhibitory
/// routes to `post.gi`. (Julia's IZ uses :v for both with sign of μ
/// determining E/I, but we keep the exc/inc split for clarity and
/// to match our existing convention.)
pub struct SpikingSynapseIZ {
  pre : IZ
  post : IZ
  sym : String
  matrix : SparseMatrixCSR
}

///|
pub fn SpikingSynapseIZ::new(pre : IZ, post : IZ, sym : String) -> SpikingSynapseIZ {
  let matrix = SparseMatrixCSR::empty(pre.n, post.n)
  { pre, post, sym, matrix }
}

///|
/// Build a SpikingSynapseIZ with random CSR connectivity.
pub fn SpikingSynapseIZ::random(
  pre : IZ,
  post : IZ,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> SpikingSynapseIZ {
  let matrix = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  { pre, post, sym, matrix }
}

///|
pub fn iz_connect(c : SpikingSynapseIZ, pre : Int, post : Int, w : Float) -> Unit {
  let pre_idx = pre - 1
  let post_idx = post - 1
  c.matrix.set(pre_idx, post_idx, w)
}

///|
/// Forward: for each pre-synaptic neuron that fires, add `w` to the
/// post-synaptic neuron's `ge` (or `gi` if sym is "gi").
pub fn forward_iz_synapse(c : SpikingSynapseIZ) -> Unit {
  let target = if c.sym == "ge" { c.post.ge } else { c.post.gi }
  c.matrix.forward(c.pre.fire, target)
}