// infer_receptors — index receptors by their `target` field.
// Port of `infer_receptors(receptors::ReceptorArray)::NamedTuple`
// from `refs/SNNModels.jl/src/populations/synapse/receptors.jl`.
//
// Returns a struct with `glu` / `gaba` index arrays (per receptor
// type). When a target is absent, its index array is empty.

///|
/// ReceptorsByTarget — result of infer_receptors.
pub struct ReceptorsByTarget {
  glu : Array[Int]   // indices of receptors with target="glu"
  gaba : Array[Int]  // indices of receptors with target="gaba"
}

///|
/// Group receptor indices by their `target` field ("glu" / "gaba").
pub fn infer_receptors(receptors : Array[Receptor]) -> ReceptorsByTarget {
  let glu : Array[Int] = []
  let gaba : Array[Int] = []
  let mut i : Int = 0
  while i < receptors.length() {
    let r = receptors[i]
    if r.target == "glu" {
      ignore(glu.push(i))
    } else if r.target == "gaba" {
      ignore(gaba.push(i))
    }
    i = i + 1
  }
  { glu, gaba }
}

///|
/// Count of receptors per target (helper for verification).
pub fn count_by_target(recs : ReceptorsByTarget) -> Int {
  recs.glu.length() + recs.gaba.length()
}

///|
/// Number of receptors with target "glu" (AMPA + NMDA combined).
pub fn count_glu(recs : ReceptorsByTarget) -> Int {
  recs.glu.length()
}

///|
/// Number of receptors with target "gaba" (GABAa + GABAb combined).
pub fn count_gaba(recs : ReceptorsByTarget) -> Int {
  recs.gaba.length()
}

///|
/// Convenience: `infer_receptors_from(rs)` builds a `ReceptorsByTarget`
/// from a `Receptors` collection directly (without exposing `rs.rec`).
pub fn infer_receptors_from(rs : Receptors) -> ReceptorsByTarget {
  infer_receptors(rs.rec)
}