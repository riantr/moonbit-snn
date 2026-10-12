// StimulusGroup — a container that bundles multiple stimuli and
// broadcasts operations (set_active!, stimulate!, etc.) across all
// elements. Port of `refs/SNNModels.jl/src/stimuli/stimulus_group.jl`.
//
// We use an enum-tagged `AnyStim` style to dispatch: each Stimulus
// carries a string tag (`"Poisson"`, `"SpikeTime"`, etc.) so
// group-level operations can dispatch without AbstractMethod-style
// type dispatch.

///|
/// StimulusGroup — a list of stimuli that can be operated on as one.
///
/// `elements` is a flat array of stimulus tags ("Poisson",
/// "SpikeTime", "Current", ...). Operations on the group
/// (set_active, stimulate) iterate and dispatch per tag.
pub struct StimulusGroup {
  name : String
  elements : Array[String]
  active : Array[Bool]
}

///|
/// Build a StimulusGroup from a name + list of stimulus tags.
pub fn StimulusGroup::new(
  name : String,
  elements : Array[String],
) -> StimulusGroup {
  let active : Array[Bool] = Array::make(elements.length(), true)
  { name, elements, active }
}

///|
/// Number of stimuli in the group.
pub fn StimulusGroup::n_elements(s : StimulusGroup) -> Int {
  s.elements.length()
}

///|
/// Set the active flag for ALL stimuli in the group.
pub fn StimulusGroup::set_active_all(s : StimulusGroup, active : Bool) -> Unit {
  let mut k : Int = 0
  while k < s.active.length() {
    ignore(s.active.set(k, active))
    k = k + 1
  }
}

///|
/// Get the active flag for element i.
pub fn StimulusGroup::is_active(s : StimulusGroup, i : Int) -> Bool {
  s.active[i]
}

///|
/// Set the active flag for a single element.
pub fn StimulusGroup::set_active_one(s : StimulusGroup, i : Int, active : Bool) -> Unit {
  ignore(s.active.set(i, active))
}

///|
/// Get the tag of element i.
pub fn StimulusGroup::tag(s : StimulusGroup, i : Int) -> String {
  s.elements[i]
}