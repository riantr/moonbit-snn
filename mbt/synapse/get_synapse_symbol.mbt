// get_synapse_symbol — Maps short synaptic receptor names to the
// canonical :glu / :gaba domain. Port of `get_synapse_symbol`
// from `refs/SNNModels.jl/src/populations/synapse/synapses.jl`.
//
// Julia's mapping (line 43-51 of synapses.jl):
//   :glu → :glu
//   :gaba → :gaba
//   :he / :ge → :glu
//   :hi / :gi → :gaba
//   other → sym (passthrough)
//
// MoonBit has no Symbols so we operate on Strings.

///|
/// Map a synaptic receptor symbol to its canonical domain.
///   "glu"  → "glu"
///   "gaba" → "gaba"
///   "he" / "ge" → "glu"
///   "hi" / "gi" → "gaba"
///   anything else → passthrough
pub fn get_synapse_symbol(sym : String) -> String {
  if sym == "glu" || sym == "gaba" {
    sym
  } else if sym == "he" || sym == "ge" {
    "glu"
  } else if sym == "hi" || sym == "gi" {
    "gaba"
  } else {
    sym
  }
}