// snn_utils_params.mbt — SNNUtils.jl parameter constants demo
// (LKD2014 + Duarte2019).
//
// Reference Julia (SNNUtils.jl/src/models/lkd2014.jl + duarte2019.jl):
//   LKD2014 = (AdEx = AdExParameter(El=-70mV, Vt=-52mV,
//                                 τm=300pF/15nS, ...),
//              PV   = IFParameter(El=-62mV, Vr=-57.47mV, ...))
//   duarte2019 = (PV = IFParameterGsyn(τm=104.52pF/9.75nS, ...),
//                 SST = IFParameterGsyn(τm=102.86pF/4.61nS, ...),
//                 AdEx = AdExParameterGsyn(τm=116.5pF/4.64nS, ...))
//
// MoonBit (v0.10.40): construct LKD2014 + Duarte2019 constants and
// show the key parameters (τm, El, Vt, Vr, gsyn_e/i). Demonstrate
// that LKD2014 can be used to build a PV IFParameter via the
// existing IFParameter::custom constructor.
//
// Bit-exact note: Julia computes C/nS ratios at compile time
// (`300pF / 15.0nS = 20.0ms`). We compute the equivalent values
// manually as Float32 constants. The mapping is exact for C, nS
// both = 1.0 internally (which is the case after @snn_kw unit
// normalisation).

///|
fn main {
  let lkd = @snn_mbt.LKD2014::new()
  let duarte = @snn_mbt.Duarte2019::new()

  println("snn_utils_params.mbt: LKD2014 + Duarte2019 parameter constants (v0.10.40)")
  println("")
  println("=== LKD2014 (Litwin-Kumar-Doiron 2014 balanced-network) ===")
  println("  AdEx excitatory:")
  println("    τm  = " + lkd.tm.to_string() + " ms   (= 300pF/15nS)")
  println("    Vt  = " + lkd.vt.to_string() + " mV")
  println("    El  = " + lkd.el.to_string() + " mV")
  println("    Vr  = " + lkd.vr.to_string() + " mV")
  println("    R   = " + lkd.r.to_string() + " (1/15nS)")
  println("    τabs= " + lkd.tau_abs.to_string() + " ms")
  println("    E_i = " + lkd.e_i.to_string() + " mV")
  println("    E_e = " + lkd.e_e.to_string() + " mV")
  println("    At = " + lkd.at.to_string() + " mV")
  println("  PV inhibitory IF:")
  println("    τm  = " + lkd.pv_tm.to_string() + " ms")
  println("    El  = " + lkd.pv_el.to_string() + " mV")
  println("    Vr  = " + lkd.pv_vr.to_string() + " mV")
  println("    Vt  = " + lkd.pv_vt.to_string() + " mV")
  println("")
  println("=== Duarte2019 (PV / SST / AdEx model) ===")
  println("  PV IF (fast-spiking):")
  println(
    "    τm  = " +
    duarte.pv_tm.to_string() +
    " ms   (= 104.52pF/9.75nS)",
  )
  println("    El  = " + duarte.pv_el.to_string() + " mV")
  println("    Vt  = " + duarte.pv_vt.to_string() + " mV")
  println("    Vr  = " + duarte.pv_vr.to_string() + " mV")
  println("    τabs= " + duarte.pv_tau_abs.to_string() + " ms")
  println("    gsyn_e = " + duarte.pv_gsyn_e.to_string() + " nS")
  println("    gsyn_i = " + duarte.pv_gsyn_i.to_string() + " nS")
  println("  SST IF (regular-spiking):")
  println(
    "    τm  = " +
    duarte.sst_tm.to_string() +
    " ms   (= 102.86pF/4.61nS)",
  )
  println("    El  = " + duarte.sst_el.to_string() + " mV")
  println("    Vt  = " + duarte.sst_vt.to_string() + " mV")
  println("    Vr  = " + duarte.sst_vr.to_string() + " mV")
  println("    τabs= " + duarte.sst_tau_abs.to_string() + " ms")
  println("    gsyn_e = " + duarte.sst_gsyn_e.to_string() + " nS")
  println("    gsyn_i = " + duarte.sst_gsyn_i.to_string() + " nS")
  println("    b    = " + duarte.sst_b.to_string() + " pA")
  println("    τw   = " + duarte.sst_tw.to_string() + " ms")
  println("  AdEx (excitatory):")
  println(
    "    τm  = " +
    duarte.adex_tm.to_string() +
    " ms   (= 116.5pF/4.64nS)",
  )
  println("    El  = " + duarte.adex_el.to_string() + " mV")
  println("    Vt  = " + duarte.adex_vt.to_string() + " mV")
  println("    τabs= " + duarte.adex_tau_abs.to_string() + " ms")
  println("    gsyn_e = " + duarte.adex_gsyn_e.to_string() + " nS")
  println("    gsyn_i = " + duarte.adex_gsyn_i.to_string() + " nS")
  println("")

  // Demonstrate that LKD2014 can be used to build an IFParameter.
  let pv_if = @snn_mbt.IFParameter::custom(
    lkd.pv_tm, lkd.pv_vt, lkd.pv_vr, lkd.pv_el, 0.06F,
  )
  println("=== LKD2014 -> IFParameter::custom ===")
  println(
    "  PV IF.tm = " +
    pv_if.tm.to_string() +
    " (matches LKD2014.pv_tm = " +
    lkd.pv_tm.to_string() +
    ")",
  )
  println(
    "  PV IF.El = " +
    pv_if.el.to_string() +
    " (matches LKD2014.pv_el = " +
    lkd.pv_el.to_string() +
    ")",
  )
}