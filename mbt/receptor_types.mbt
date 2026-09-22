// receptor_types.mbt — receptor presets from SNNModels.jl
//
// Port of `refs/SNNModels.jl/src/populations/synapse/receptor_types.jl`.
// These are Julia-side `let ... end` blocks that bind named-tuple
// constants like `EyalGluDend`, `MilesGabaSoma`, `TripodSomaSynapse`,
// etc. We expose the same set of presets via top-level functions that
// return the constructed value.
//
// All values are bit-exact Float32. Julia uses Unitful annotations
// (1mM, 1ms, 0.27/ms) — we pre-compute these as raw Float32 numbers
// to match the `@snn_kw` normalised form.

///|
/// Eyal NMDA voltage-dependency preset. `mg = 1.0` (1mM normalised),
/// `b = 3.36`, `k = -0.077`. Matches Julia `EyalNMDA`.
/// (Functionally a no-op alias for the existing `NMDAVoltageDependency::eyal()`.)
pub fn eyal_nmda() -> NMDAVoltageDependency {
  NMDAVoltageDependency::eyal()
}

///|
/// Soma NMDA voltage-dependency preset. `mg = 1.0`, `b = 3.57`,
/// `k = -0.062`. Matches Julia `SomaNMDA`.
/// (Functionally a no-op alias for the existing `NMDAVoltageDependency::soma()`.)
pub fn soma_nmda() -> NMDAVoltageDependency {
  NMDAVoltageDependency::soma()
}

///|
/// Miles GABA soma receptor: E_rev=-70, τr=0.1, τd=15.0, g0=0.38,
/// target="gaba". Matches Julia `MilesGabaSoma = Receptor(E_rev=-70, τr=0.1, τd=15, g0=0.38, target=:gaba)`.
pub fn miles_gaba_soma() -> Receptor {
  Receptor::simple(-70.0F, 0.1F, 15.0F, 0.38F, "gaba")
}

///|
/// Duarte Glu soma receptor: E_rev=0, τr=0.26, τd=2.0, g0=0.73,
/// target="glu". Matches Julia `DuarteGluSoma`.
pub fn duarte_glu_soma() -> Receptor {
  Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
}

///|
/// Eyal Glu dendrite (Glutamatergic bundle): AMPA(0.0, 0.26, 2.0, 0.73)
/// + NMDA(0.0, 8, 35, 1.31). Matches Julia `EyalGluDend`.
pub fn eyal_glu_dend() -> Glutamatergic {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  Glutamatergic::custom(ampa~, nmda~)
}

///|
/// Miles GABA dendrite (GABAergic bundle): GABAa(-70, 4.8, 29, 0.27)
/// + GABAb(-90, 30, 400, 0.006). Matches Julia `MilesGabaDend`.
pub fn miles_gaba_dend() -> GABAergic {
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  GABAergic::custom(gabaa~, gabab~)
}

///|
/// Soma Glu (Glutamatergic for IF / AdEx soma): AMPA(0, 1, 6, 0.7)
/// + NMDA(0, 1, 100, 0.15). Matches Julia `SomaGlu`.
pub fn soma_glu() -> Glutamatergic {
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 0.7F, "glu")
  let nmda = Receptor::nmda(0.0F, 1.0F, 100.0F, 0.15F)
  Glutamatergic::custom(ampa~, nmda~)
}

///|
/// Soma GABA (GABAergic for IF / AdEx soma): GABAa(-70, 0.5, 10, 2.0)
/// + GABAb(-90, 30, 400, 0.006). Matches Julia `SomaGABA`.
pub fn soma_gaba() -> GABAergic {
  let gabaa = Receptor::simple(-70.0F, 0.5F, 10.0F, 2.0F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  GABAergic::custom(gabaa~, gabab~)
}

///|
/// Tripod soma receptors (4-receptor collection). Julia equivalent:
///   `Receptors(DuarteGluSoma, MilesGabaSoma)` — but only AMPA + GABAa
///   slots populated; NMDA + GABAb slots set to "duplicate" presets
///   since the soma routing typically uses glu_receptors=[1] (AMPA-only).
///
/// Build pattern: ampa=DuarteGluSoma, nmda=DuarteGluSoma (placeholder),
/// gabaa=MilesGabaSoma, gabab=MilesGabaSoma (placeholder).
pub fn tripod_soma_receptors() -> Receptors {
  let ampa = duarte_glu_soma()
  let nmda = duarte_glu_soma()
  let gabaa = miles_gaba_soma()
  let gabab = miles_gaba_soma()
  let glu_struct = Glutamatergic::custom(ampa~, nmda~)
  let gaba_struct = GABAergic::custom(gabaa~, gabab~)
  Receptors::from_pair(glu_struct, gaba_struct)
}

///|
/// Tripod dendrite receptors: EyalGluDend + MilesGabaDend.
/// Matches Julia `TripodDendReceptors`.
pub fn tripod_dend_receptors() -> Receptors {
  let glu = eyal_glu_dend()
  let gaba = miles_gaba_dend()
  Receptors::from_pair(glu, gaba)
}

///|
/// Soma receptors: SomaGlu + SomaGABA. Matches Julia `SomaReceptors`.
pub fn soma_receptors() -> Receptors {
  Receptors::from_pair(soma_glu(), soma_gaba())
}