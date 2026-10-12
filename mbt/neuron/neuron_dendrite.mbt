// Dendrite — passive dendritic compartment, bit-exact port of
// SNNModels.jl/src/populations/multicompartment/dendrite.jl.
//
// The Dendrite is the simplest multi-compartment building block:
// a passive RC compartment with an axial conductance to a parent
// (soma) compartment. It has NO active ion channels — only leak
// + axial current. Used as a building block for BallAndStick
// (1 dendrite) and Tripod (2 dendrites) multi-compartment neurons.
//
// Float32 contract: every arithmetic uses `Float` (Float32).
//
// Per-neuron parameters (Julia: Dendrite{VFT} with N fields):
//   El : Float        — resting potential (default -70.6 mV)
//   C  : Float        — membrane capacitance (default 10 pF)
//   gax: Float        — axial conductance to parent (default 10 nS)
//   gm : Float        — membrane leak conductance (default 1 nS)
//   l  : Float        — length (μm, default 150)
//   d  : Float        — diameter (μm, default 4)
//
// Integration (passive, simple forward Euler):
//   ic = (v_parent - v_dendrite) * gax        # axial current from parent
//   dv_d/dt = (El - v_d) * gm / C + ic / C    # + synaptic input / C (added by caller)
// Note: we return the per-neuron Δv; caller adds external synaptic
// inputs and combines with parent axial currents. This matches the
// Tripod.jl update_neuron! pattern where Δv[i, 2] for the dendrite
// is `(El - v) * gm - is_syn + ic / C`.

///|
/// Dendrite — passive dendritic compartment parameters (per-neuron).
/// All fields are Float32. N is implicit from the population size.
pub struct Dendrite {
  n : Int
  el : Array[Float]
  c : Array[Float]
  gax : Array[Float]
  gm : Array[Float]
  l : Array[Float]
  d : Array[Float]
  // Optional parent axial conductance (gax_parent) — for multi-dendrite
  // models where dendrites also connect to each other. Default 0.
  gax_parent : Array[Float]
}

///|
/// Default Dendrite constructor (matches Julia's Dendrite defaults):
/// El=-70.6mV, C=10pF, gax=10nS, gm=1nS, l=150um, d=4um.
pub fn Dendrite::new(n : Int) -> Dendrite {
  let el : Array[Float] = Array::make(n, -70.6F)
  let c : Array[Float] = Array::make(n, 10.0F)
  let gax : Array[Float] = Array::make(n, 10.0F)
  let gm : Array[Float] = Array::make(n, 1.0F)
  let l : Array[Float] = Array::make(n, 150.0F)
  let d : Array[Float] = Array::make(n, 4.0F)
  let gax_parent : Array[Float] = Array::make(n, 0.0F)
  { n, el, c, gax, gm, l, d, gax_parent }
}

///|
/// Custom Dendrite with all parameters specified per-neuron.
pub fn Dendrite::custom(
  n : Int,
  el : Float,
  c : Float,
  gax : Float,
  gm : Float,
  l : Float,
  d : Float,
) -> Dendrite {
  let el_arr : Array[Float] = Array::make(n, el)
  let c_arr : Array[Float] = Array::make(n, c)
  let gax_arr : Array[Float] = Array::make(n, gax)
  let gm_arr : Array[Float] = Array::make(n, gm)
  let l_arr : Array[Float] = Array::make(n, l)
  let d_arr : Array[Float] = Array::make(n, d)
  let gax_parent_arr : Array[Float] = Array::make(n, 0.0F)
  { n, el: el_arr, c: c_arr, gax: gax_arr, gm: gm_arr,
    l: l_arr, d: d_arr, gax_parent: gax_parent_arr }
}

///|
/// Compute the dendritic voltage change for one forward-Euler step.
///   dv[i] = ((El[i] - v[i]) * gm[i] + (v_parent[i] - v[i]) * gax[i]
///            + i_ext[i]) / C[i]
/// where `i_ext` is any external current injected into the dendrite
/// (synaptic currents, etc.) added by the caller.
///
/// This returns Δv per neuron (not v itself). The caller adds
/// `dt * Δv[i]` to v_d[i] and handles parent axial currents.
pub fn dendrite_step(
  d : Dendrite,
  v : Array[Float],
  v_parent : Array[Float],
  i_ext : Array[Float],
) -> Array[Float] {
  let n = d.n
  let dv : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let ic = (v_parent[i] - v[i]) * d.gax[i]
    let leak = (d.el[i] - v[i]) * d.gm[i]
    dv[i] = (leak + ic + i_ext[i]) / d.c[i]
  }
  dv
}

///|
/// Bit-exact match for Julia's G_axial formula:
///   G_axial = π * d² / (4 * Ri * l)
/// in Float32. Ri is axial resistivity (Ω·cm). Output is in nS.
/// Default Ri for human dendrite: 200 Ω·cm (human_dend in Julia).
pub fn g_axial(ri : Float, d : Float, l : Float) -> Float {
  // π * d² / (4 * Ri * l)
  // pi ≈ 3.14159265358979... but Float32 truncates.
  let pi = 3.141592653589793F
  pi * d * d / (4.0F * ri * l)
}

///|
/// Bit-exact match for Julia's G_mem formula:
///   G_mem = l * d * π / Rd
/// in Float32. Rd is membrane specific resistance (Ω·cm²).
/// Output in nS.
pub fn g_mem(rd : Float, d : Float, l : Float) -> Float {
  let pi = 3.141592653589793F
  l * d * pi / rd
}

///|
/// Bit-exact match for Julia's C_mem formula:
///   C_mem = Cd * π * d * l
/// in Float32. Cd is specific capacitance (pF/cm²). Output in pF.
pub fn c_mem(cd : Float, d : Float, l : Float) -> Float {
  let pi = 3.141592653589793F
  cd * pi * d * l
}