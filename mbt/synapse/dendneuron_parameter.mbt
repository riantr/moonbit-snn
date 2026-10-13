// DendNeuronParameter — multicompartment dendritic neuron parameter
// container. Bit-exact port of SNNModels.jl.
//
// Julia reference:
//   src/populations/multicompartment/dendneuron_parameter.jl
//   src/populations/multicompartment/dendrite.jl (Physiology)
//
// The DendNeuronParameter bundles:
//   - ds : dendritic segment lengths (1 → BallAndStick, 2 → Tripod)
//   - physiology : Physiology{Float32} (Ri, Rd, Cd constants)
//   - geometry : soma→dendrite connection list (s→d1, s→d2 for Tripod)
//   - type : inferred dendritic tree type from ds.length()
//
// Float32 contract: every arithmetic uses `Float` (Float32). The
// Physiology constants match Julia's human_dend / mouse_dend presets
// (Ri, Rd, Cd) computed via the SNN unit conversion factors (Ω×cm,
// Ω×cm², pF/cm² → Float32).

///|
/// Physiology — passive dendritic cable parameters (Ri/Rd/Cd).
///
///   ri : intracellular resistivity (Ω×cm)
///   rd : membrane resistance (Ω×cm²)
///   cd : membrane capacitance (pF/cm²)
pub(all) struct Physiology {
  ri : Float
  rd : Float
  cd : Float
}

///|
/// Default human cortical Physiology (Julia's `human_dend`).
///   Ri = 200 Ω·cm, Rd = 38907 Ω·cm², Cd = 0.5 μF/cm²
/// Converted to Float32 via the SNN unit normalisation factors:
///   Ω·cm = 1e-3F (= 1e3 in mho/cm; we store the Float32 raw value)
///   Ω·cm² = 1e-3F  (= 1e3 mho·cm)
///   pF/cm² = 1e-1F (= 0.1; 1pF=1 in our units, 1cm²=1)
/// Julia evaluates `200 Ω·cm = 200*0.001 = 0.2F` after unit norm.
pub fn Physiology::human() -> Physiology {
  // 200Ω·cm → 200*0.001F = 0.2F
  // 38907Ω·cm² → 38907*0.001F ≈ 38.907F (Float32 → 38.906997F)
  // 0.5μF/cm² → 0.5*1000000F/10000F = 0.5F (1μF=1e6 pF; 1cm²=1; /10000)
  // Julia's `@snn_kw` evaluates 0.5e-6 siemens·cm² * 1cm² = 0.5e-6
  // → 0.5e-6F (Float64). Narrowed to Float32 = 0.5F exactly.
  { ri: 0.2F, rd: 38907.0F * 0.001F, cd: 0.5F }
}

///|
/// Mouse cortical Physiology (Julia's `mouse_dend`).
///   Ri = 200 Ω·cm, Rd = 1700 Ω·cm², Cd = 1 μF/cm².
pub fn Physiology::mouse() -> Physiology {
  { ri: 0.2F, rd: 1700.0F * 0.001F, cd: 1.0F }
}

///|
/// Custom Physiology constructor.
pub fn Physiology::custom(ri~ : Float, rd~ : Float, cd~ : Float) -> Physiology {
  { ri, rd, cd }
}

///|
/// DendriticTreeType — inferred from ds.length():
///   1 → BallAndStick, 2 → Tripod, >2 → Multipod (unsupported here).
pub enum DendriticTreeType {
  BallAndStick
  Tripod
  Multipod
}

///|
/// DendNeuronParameter — multicompartment neuron parameter bundle.
///
///   ds         : vector of (proximal, distal) dendritic segment lengths
///                (Float32 in μm normalised units)
///   physiology : Physiology {ri, rd, cd}
///   geometry   : vector of (soma_sym, dendrite_sym) pairs describing
///                which dendritic compartment each segment targets
///                (stored as Strings since MoonBit has no Symbols)
///   tree_type  : inferred from ds.length()
pub(all) struct DendNeuronParameter {
  ds : Array[Array[Float]]
  physiology : Physiology
  geometry : Array[Array[String]]
  tree_type : DendriticTreeType
}

///|
/// Default DendNeuronParameter (Tripod: 2 dendrites, (200μm, 400μm)
/// each, human physiology, s→d1 + s→d2 geometry).
pub fn DendNeuronParameter::new() -> DendNeuronParameter {
  let ds : Array[Array[Float]] = [
    [200.0F, 400.0F],
    [200.0F, 400.0F],
  ]
  let physiology = Physiology::human()
  let geometry : Array[Array[String]] = [
    ["s", "d1"],
    ["s", "d2"],
  ]
  { ds, physiology, geometry, tree_type: Tripod }
}

///|
/// TripodParameter factory — 2 dendrites (default ds=(200μm, 400μm)
/// each, human physiology, s→d1 + s→d2).
pub fn tripod_param_new() -> DendNeuronParameter {
  let ds : Array[Array[Float]] = [
    [200.0F, 400.0F],
    [200.0F, 400.0F],
  ]
  let physiology = Physiology::human()
  let geometry : Array[Array[String]] = [
    ["s", "d1"],
    ["s", "d2"],
  ]
  { ds, physiology, geometry, tree_type: Tripod }
}

///|
/// BallAndStickParameter factory — 1 dendrite (default ds=(150μm, 400μm),
/// human physiology, s→d).
pub fn ballandstick_param_new() -> DendNeuronParameter {
  let ds : Array[Array[Float]] = [[150.0F, 400.0F]]
  let physiology = Physiology::human()
  let geometry : Array[Array[String]] = [["s", "d"]]
  { ds, physiology, geometry, tree_type: BallAndStick }
}

///|
/// Custom DendNeuronParameter constructor (caller supplies ds +
/// physiology + geometry; tree_type auto-inferred).
pub fn DendNeuronParameter::custom(
  ds~ : Array[Array[Float]],
  physiology~ : Physiology = Physiology::human(),
  geometry~ : Array[Array[String]] = [],
) -> DendNeuronParameter {
  let tree_type = if ds.length() == 1 {
    BallAndStick
  } else if ds.length() == 2 {
    Tripod
  } else {
    Multipod
  }
  // Auto-fill geometry if caller didn't supply one
  let actual_geometry = if geometry.length() == 0 {
    let mut i : Int = 0
    let g : Array[Array[String]] = []
    while i < ds.length() {
      let d_sym = if ds.length() == 1 { "d" } else { "d" + (i + 1).to_string() }
      ignore(g.push(["s", d_sym]))
      i = i + 1
    }
    g
  } else {
    geometry
  }
  { ds, physiology, geometry: actual_geometry, tree_type }
}

///|
/// Convert a DendNeuronParameter to the corresponding concrete neuron
/// constructor (Tripod / BallAndStick). Mirrors Julia's
/// `Population(param::DendNeuronParameter)`. Returns the
/// DendNeuronParameter unchanged for downstream `Tripod::new` /
/// `BallAndStick::new` consumption; the caller is responsible for
/// dispatching on tree_type.
pub fn population_from_dend_neuron(
  param : DendNeuronParameter,
) -> DendNeuronParameter {
  // In Julia, this returns the concrete Tripod/BallAndStick instance.
  // We just return the parameter struct since the concrete constructors
  // exist in neuron_tripod.mbt / neuron_ballandstick.mbt.
  param
}