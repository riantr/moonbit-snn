// EmptyStimulus — a no-op stimulus that takes the place of an
// AbstractStimulus in tests where no input is desired. Bit-exact
// port of `refs/SNNModels.jl/src/stimuli/empty.jl`.
//
// Julia:
//   @snn_kw struct EmptyStimulus <: AbstractStimulus
//     param::EmptyParam = EmptyParam()
//     records::Dict = Dict()
//   end
//   function stimulate!(p::EmptyStimulus, param::EmptyParam, T::Time, dt::Float32) end

///|
/// EmptyStimulus — placeholder stimulus (no operation).
///
/// Uses the existing EmptyParam struct from structs_extra.mbt
/// (added in v0.10.54). `stimulate_empty` is a no-op.
pub struct EmptyStimulus {
  param : EmptyParam
  records : Array[Int] // empty dict proxy (MoonBit has no Dict)
}

///|
/// Default EmptyStimulus (EmptyParam type="empty").
pub fn EmptyStimulus::new() -> EmptyStimulus {
  { param: EmptyParam::new(), records: [] }
}

///|
/// Custom EmptyStimulus with explicit EmptyParam.
pub fn EmptyStimulus::with_param(p : EmptyParam) -> EmptyStimulus {
  { param: p, records: [] }
}

///|
/// stimulate_empty — no-op (matches Julia's empty body).
pub fn stimulate_empty(
  _stim : EmptyStimulus,
  _param : EmptyParam,
  _t : Float,
  _dt : Float,
) -> Unit {
  // Intentionally empty.
}