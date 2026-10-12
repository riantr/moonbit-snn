// structs_extra.mbt — additional struct helpers from
// `SNNModels.jl/src/utils/structs.jl` (EmptyParam + Time::from_ms
// numeric constructor + NetworkModel placeholder).
//
// Julia reference: SNNModels.jl/src/utils/structs.jl

///|
/// EmptyParam — a struct representing an empty parameter.
/// Mirrors Julia's `@snn_kw struct EmptyParam` with `type::Symbol = :empty`.
pub struct EmptyParam {
  p_type : String
}

///|
pub fn EmptyParam::new() -> EmptyParam {
  { p_type: "empty" }
}

///|
pub fn EmptyParam::with_type(p_type : String) -> EmptyParam {
  { p_type: p_type }
}

///|
/// Convenience: print type as a String for log output.
pub fn EmptyParam::type_str(p : EmptyParam) -> String {
  p.p_type
}

///|
/// NetworkModel — placeholder for the heterogeneous model aggregate.
/// Our `HeterogeneousModel` (in compose.mbt) is the MoonBit equivalent;
/// this stub exists to document the Julia-side abstraction and provide
/// the same field accessor names. The actual port lives in compose.mbt.
pub struct NetworkModel {
  // Marker / placeholder.
  is_valid : Bool
}

///|
pub fn NetworkModel::new() -> NetworkModel {
  { is_valid: true }
}

///|
pub fn isa_model(m : NetworkModel) -> Bool {
  m.is_valid
}