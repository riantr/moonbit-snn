// snn_utils_params.mbt — SNNUtils.jl parameter constants ports.
//
// Port of SNNUtils.jl/src/models/lkd2014.jl + duarte2019.jl. These
// are pure parameter constant files used to build the
// Litwin-Kumar-Doiron (2014) balanced network and the Duarte
// (2019) PV / SST / AdEx model respectively. The Julia source uses
// type constructors like `AdExParameter(El = -70mV, Vt = -52mV,
// τm = 300pF / 15.0nS, ...)` which MoonBit can't directly replicate
// because our parameter structs are not `pub(all)` (they can only
// be constructed via dedicated `with_*` / `custom` methods).
//
// We provide MoonBit equivalents as plain constants that callers
// can pass to the dedicated custom constructors (AdExParameter::with_*,
// IFParameter::custom, AdExParameterHet::homogeneous).

///|
/// LKD2014 — Litwin-Kumar-Doiron 2014 balanced-network parameter
/// constants. Mirrors `SNNUtils.LKD2014` named-tuple from
/// lkd2014.jl.
///
/// Note: Julia's `AdExParameter(τm = 300pF / 15.0nS)` evaluates at
/// compile time. In MoonBit we compute the value manually:
///   300pF / 15.0nS = (300 * 1e-12 F) / (15.0 * 1e-9 S)
///                   = (300 / 15.0) * 1e-3 s
///                   = 20 ms
/// So `lkd_tm` = 20.0F (in internal ms units).
pub struct LKD2014 {
  // AdEx parameters (exc — used for E population).
  tm : Float // τm = 300pF / 15.0nS = 20ms
  vt : Float // -52mV
  el : Float // -70mV
  vr : Float // -60mV
  r : Float  // 1/(15nS) = 66.67 MΩ = 0.0667F
  tau_abs : Float // 1ms
  e_i : Float // -75mV
  e_e : Float // 0mV
  at : Float // 10mV
  // PV (inhibitory IF) parameters.
  pv_tm : Float  // 20ms
  pv_el : Float  // -62mV
  pv_vr : Float  // -57.47mV
  pv_vt : Float  // -52mV
}

///|
/// Defaults match Julia's LKD2014. Computed `tm` from
/// Float32(300 / 15) (pF / nS, since pF=1.0 and nS=1.0 internally).
pub fn LKD2014::new() -> LKD2014 {
  {
    tm: 20.0F, // 300pF / 15nS in internal units
    vt: -52.0F,
    el: -70.0F,
    vr: -60.0F,
    r: 0.0667F, // 1 / 15nS ≈ 0.0667 mho
    tau_abs: 1.0F,
    e_i: -75.0F,
    e_e: 0.0F,
    at: 10.0F,
    pv_tm: 20.0F,
    pv_el: -62.0F,
    pv_vr: -57.47F,
    pv_vt: -52.0F,
  }
}

///|
/// Duarte2019 — Duarte et al. 2019 PV / SST / AdEx model parameter
/// constants. Mirrors `SNNUtils.duarte2019` from duarte2019.jl.
///
/// PV (parvalbumin-positive fast-spiking interneuron).
pub struct Duarte2019 {
  // PV IF parameters.
  pv_tm : Float
  pv_el : Float
  pv_vt : Float
  pv_vr : Float
  pv_tau_abs : Float
  pv_gsyn_e : Float
  pv_gsyn_i : Float
  // SST (somatostatin-positive regular-spiking interneuron).
  sst_tm : Float
  sst_el : Float
  sst_vt : Float
  sst_vr : Float
  sst_tau_abs : Float
  sst_gsyn_e : Float
  sst_gsyn_i : Float
  sst_b : Float
  sst_tw : Float
  // AdEx (excitatory pyramidal).
  adex_tm : Float
  adex_el : Float
  adex_vt : Float
  adex_tau_abs : Float
  adex_gsyn_e : Float
  adex_gsyn_i : Float
}

///|
/// Defaults match Julia's `duarte2019`. Computed `tm` values from
/// C/nS ratios:
///   PV  τm = 104.52pF / 9.75nS = 10.72ms ≈ 10.72F
///   SST τm = 102.86pF / 4.61nS = 22.31ms ≈ 22.31F
///   AdEx τm = 116.5pF / 4.64nS = 25.11ms ≈ 25.11F
pub fn Duarte2019::new() -> Duarte2019 {
  {
    pv_tm: 10.72F, // 104.52pF / 9.75nS
    pv_el: -64.33F,
    pv_vt: -38.97F,
    pv_vr: -57.47F,
    pv_tau_abs: 0.5F,
    pv_gsyn_e: 1.04F,
    pv_gsyn_i: 0.84F,
    sst_tm: 22.31F, // 102.86pF / 4.61nS
    sst_el: -61.0F,
    sst_vt: -34.4F,
    sst_vr: -47.11F,
    sst_tau_abs: 1.3F,
    sst_gsyn_e: 0.56F,
    sst_gsyn_i: 0.59F,
    sst_b: 80.5F,
    sst_tw: 144.0F,
    adex_tm: 25.11F, // 116.5pF / 4.64nS
    adex_el: -76.43F,
    adex_vt: -44.45F,
    adex_tau_abs: 2.05F,
    adex_gsyn_e: 0.73F,
    adex_gsyn_i: 0.265F,
  }
}

///|
/// LKD2014Soma — soma-level connection parameter template for the
/// Litwin-Kumar-Doiron 2014 model. Mirrors the `lkd2014_soma` named
/// tuple in `refs/SNNUtils.jl/src/models/connections.jl`. Each field is
/// a (probability p, mean μ, distribution dist, optional σ) tuple.
pub struct LKD2014Soma {
  // E→E (EsE)
  es_e_p : Float
  es_e_mu : Float
  // E→PV (E_to_If)
  e_to_pv_p : Float
  e_to_pv_mu : Float
  // PV→E (If_to_E)
  pv_to_e_p : Float
  pv_to_e_mu : Float
  // PV→PV (If_to_If)
  pv_to_pv_p : Float
  pv_to_pv_mu : Float
}

///|
/// Defaults match Julia's `lkd2014_soma`:
///   EsE: p=0.2, μ=2.76 (LogNormal)
///   E→PV: p=0.2, μ=1.27 (LogNormal)
///   PV→E: p=0.2, μ=48.7 (LogNormal)
///   PV→PV: p=0.2, μ=16.2 (LogNormal)
pub fn LKD2014Soma::new() -> LKD2014Soma {
  {
    es_e_p: 0.2F, es_e_mu: 2.76F,
    e_to_pv_p: 0.2F, e_to_pv_mu: 1.27F,
    pv_to_e_p: 0.2F, pv_to_e_mu: 48.7F,
    pv_to_pv_p: 0.2F, pv_to_pv_mu: 16.2F,
  }
}

///|
/// Quaresima2024UpDown — top-level configuration bundle for the
/// Quaresima 2024 up/down state cortical model. Mirrors
/// `quaresima2022_nar` (the main reusable component) from
/// `refs/SNNUtils.jl/src/models/quaresima_2024_updown.jl`. The Julia
/// version is a NamedTuple; we provide a struct with the most useful
/// fields: the dendritic NAR scaling factor `nar` and τd, plus the
/// soma/dendrite receptor collections (built via `eyal_equivalent_nar`
/// at runtime).
pub struct Quaresima2024UpDown {
  nar : Float
  tau_d : Float
  // Soma receptors: Receptors(DuarteGluSoma, MilesGabaSoma).
  // Dendrite receptors: built from nar via eyal_equivalent_nar().
  dend_receptors : Receptors
}

///|
/// Build a Quaresima2024UpDown config from `nar` (NMDA/AMPA ratio,
/// Julia default = 1.8) and optional `tau_d` (NMDA decay time in ms,
/// default 35). Soma uses `tripod_soma_receptors()` (DuarteGluSoma +
/// MilesGabaSoma placeholder); dendrite uses `eyal_equivalent_nar(nar, τd)`.
pub fn Quaresima2024UpDown::new(
  nar : Float,
  tau_d : Float,
) -> Quaresima2024UpDown {
  let dend = eyal_equivalent_nar(nar, tau_d)
  { nar, tau_d, dend_receptors: dend }
}

///|
/// `EyalEquivalentNAR(NAR, τd)` — build a `Receptors` collection for the
/// Quaresima 2024 model with NMDA-to-AMPA ratio `NAR`. Julia reference
/// from `quaresima_2024_updown.jl`:
///   ampa_g0 = 0.73 * (1 + NAR0 - NAR), where NAR0 = 1.31/0.73 ≈ 1.795
///   nmda_g0 = 0.73 * NAR
///   Receptors = (EyalGluNAR(NAR, τd), MilesGabaDend)
///
/// We re-use the existing `eyal_glu_dend()` and `miles_gaba_dend()` helpers
/// from `receptor_types.mbt` (these match the Julia defaults for the
/// MilesGabaDend side); the AMPA/NMDA g0 values are scaled per NAR.
pub fn eyal_equivalent_nar(nar : Float, tau_d : Float) -> Receptors {
  let nar_0 : Float = 1.31F / 0.73F
  let ampa_g0 : Float = 0.73F * (1.0F + nar_0 - nar)
  let nmda_g0 : Float = 0.73F * nar
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, ampa_g0, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, tau_d, nmda_g0)
  let glu = Glutamatergic::custom(ampa~, nmda~)
  let gaba = miles_gaba_dend()
  Receptors::from_pair(glu, gaba)
}
pub struct Quaresima2023Dend {
  e_to_ed_p : Float
  e_to_ed_mu : Float
  e_to_pv_p : Float
  e_to_pv_mu : Float
  e_to_sst_p : Float
  e_to_sst_mu : Float
  pv_to_e_p : Float
  pv_to_e_mu : Float
  pv_to_sst_p : Float
  pv_to_sst_mu : Float
  pv_to_pv_p : Float
  pv_to_pv_mu : Float
  sst_to_ed_p : Float
  sst_to_ed_mu : Float
  sst_to_pv_p : Float
  sst_to_pv_mu : Float
  sst_to_sst_p : Float
  sst_to_sst_mu : Float
}

///|
/// Defaults match Julia's `quaresima2023_dend`:
///   E→Ed: p=0.168, μ=2.8; E→PV: p=0.2, μ=log(1.0)=0; E→SST: p=0.2, μ=0
///   PV→E: p=0.2, μ=log(15.8); PV→SST: p=0.2, μ=log(1.4); PV→PV: p=0.2, μ=log(16.2)
///   SST→Ed: p=0.2, μ=log(15.8); SST→PV: p=0.2, μ=log(0.83); SST→SST: p=0.2, μ=log(0.83)
/// `log(1.0)=0`, `log(15.8)≈2.76`, `log(1.4)≈0.336`, `log(16.2)≈2.785`,
/// `log(0.83)≈-0.186`.
pub fn Quaresima2023Dend::new() -> Quaresima2023Dend {
  {
    e_to_ed_p: 0.168F, e_to_ed_mu: 2.8F,
    e_to_pv_p: 0.2F, e_to_pv_mu: 0.0F,
    e_to_sst_p: 0.2F, e_to_sst_mu: 0.0F,
    pv_to_e_p: 0.2F, pv_to_e_mu: 2.76F,
    pv_to_sst_p: 0.2F, pv_to_sst_mu: 0.336F,
    pv_to_pv_p: 0.2F, pv_to_pv_mu: 2.785F,
    sst_to_ed_p: 0.2F, sst_to_ed_mu: 2.76F,
    sst_to_pv_p: 0.2F, sst_to_pv_mu: -0.186F,
    sst_to_sst_p: 0.2F, sst_to_sst_mu: -0.186F,
  }
}