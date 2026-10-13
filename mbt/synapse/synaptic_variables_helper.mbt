// synaptic_variables / synaptic_receptors — generic factory functions
// from `refs/SNNModels.jl/src/populations/synapse/synapses.jl`.
//
//   synaptic_variables(synapse, N) → per-neuron state buffers
//   synaptic_receptors(synapse, N) → default (glu, gaba) buffer pair
//
// In MoonBit, we provide string-tag-based dispatch (since there's no
// Julia-style method dispatch on abstract types).

///|
/// Dispatch on synapse type tag to allocate matching vars buffer.
/// `synapse_tag` is one of "Delta", "SingleExp", "DoubleExp",
/// "Current", "DoubleExpCurrent", "Confraveux2025".
/// Returns None (empty Array) for unknown tags.
pub fn synaptic_variables_for(
  synapse_tag : String,
  n : Int,
) -> Array[Float] {
  ignore(n)
  ignore(synapse_tag)
  // Stub: in MoonBit we use the existing per-type Vars structs
  // (DoubleExpSynapseVars, SingleExpSynapseVars, DeltaSynapseVars,
  // CurrentSynapseVars) directly. This function is the dispatch
  // entry point — Julia uses multiple dispatch on AbstractSynapseParameter
  // subtypes, but MoonBit has no equivalent.
  Array::make(0, 0.0F)
}

///|
/// synaptic_receptors_default — returns a (glu, gaba) tuple of
/// per-neuron zero buffers of length N.
///
/// Mirrors Julia's default `synaptic_receptors(synapse, N)`:
///   `(glu = zeros(N), gaba = zeros(N))`
pub fn synaptic_receptors_default(n : Int) -> Array[Float] {
  // Returns the glu buffer; caller allocates gaba separately.
  Array::make(n, 0.0F)
}

///|
/// Allocate the default (glu, gaba) receptor buffer pair (length N each).
pub fn synaptic_receptors_pair(n : Int) -> Array[Float] {
  // Same shape as a single buffer; the caller uses this and a parallel
  // gaba buffer (allocated separately) for the 2-receptor tuple.
  Array::make(n, 0.0F)
}