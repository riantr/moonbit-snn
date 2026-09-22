// SpikingSynapseHH — port of SNNModels.jl's HH-targeting SpikingSynapse.
//
// HH integration uses `ge` and `gi` as synaptic conductances:
//   v[i] += dt/Cm * (... + ge * (Ee - v) + gi * (Ei - v) + ...)
//
// Like SpikingSynapseIZ, this is structurally identical to the IZ
// version (both use ge/gi arrays). Kept separate for clarity; both
// forward pre-synaptic spikes into post's ge (sym="ge") or gi.

///|
/// SpikingSynapse targeting an HH post-synaptic neuron.
pub struct SpikingSynapseHH {
  pre : HH
  post : HH
  sym : String
  matrix : SparseMatrixCSR
}

///|
pub fn SpikingSynapseHH::new(pre : HH, post : HH, sym : String) -> SpikingSynapseHH {
  let matrix = SparseMatrixCSR::empty(pre.n, post.n)
  { pre, post, sym, matrix }
}

///|
pub fn SpikingSynapseHH::random(
  pre : HH,
  post : HH,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> SpikingSynapseHH {
  let matrix = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  { pre, post, sym, matrix }
}

///|
pub fn hh_connect(c : SpikingSynapseHH, pre : Int, post : Int, w : Float) -> Unit {
  let pre_idx = pre - 1
  let post_idx = post - 1
  c.matrix.set(pre_idx, post_idx, w)
}

///|
/// Forward: for each pre-synaptic neuron that fires, add `w` to the
/// post-synaptic neuron's `ge` (or `gi` if sym is "gi").
pub fn forward_hh_synapse(c : SpikingSynapseHH) -> Unit {
  let target = if c.sym == "ge" { c.post.ge } else { c.post.gi }
  c.matrix.forward(c.pre.fire, target)
}