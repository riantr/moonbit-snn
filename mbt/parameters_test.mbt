// Tests for population + synapse parameter types. Mirrors
// SpikingNeuralNetworks.jl/test/pop/parameters.jl "Population parameters"
// + "Synaptic parameter types" testsets.
//
// Julia's `hasproperty` checks are mapped to direct field access in
// MoonBit (since MoonBit structs are nominally typed and the field
// access is statically checked at compile time). If a field is
// missing, the test file won't compile — which is the equivalent
// of a Julia `hasproperty` failure.
//
// Note: MoonBit's parameter structs are not pub(all), so custom
// struct literals can only be created from inside their own module
// (or via dedicated `with_*` / `custom` constructors). Where those
// constructors don't exist, we use `::new()` + spot checks on the
// default fields rather than duplicating Julia's custom-value
// assertions.

///|
test "IFParameter::new: defaults match Julia + custom via IFParameter::custom" {
  let p = IFParameter::new()
  // Julia: hasproperty(p, :Vt), :Vr, :El, :τm. MoonBit fields: vt, vr, el, tm.
  if p.vt != -50.0F {
    fail("Vt (vt) = \{p.vt}, expected -50.0")
  }
  if p.vr != -60.0F {
    fail("Vr (vr) = \{p.vr}, expected -60.0")
  }
  if p.el != -70.0F {
    fail("El (el) = \{p.el}, expected -70.0")
  }
  if p.tm != 15.0F {
    fail("τm (tm) = \{p.tm}, expected 15.0")
  }
  // Custom values via the IFParameter::custom constructor.
  let p2 = IFParameter::custom(20.0F, -45.0F, -65.0F, -60.0F, 1.0F)
  if p2.vt != -45.0F {
    fail("custom Vt = \{p2.vt}, expected -45")
  }
  if p2.el != -60.0F {
    fail("custom El = \{p2.el}, expected -60")
  }
  if p2.vr != -65.0F {
    fail("custom Vr = \{p2.vr}, expected -65")
  }
}

///|
test "AdExParameter::new: defaults match Julia" {
  let p = AdExParameter::new()
  // Julia: p.C ≈ 281pF, p.gl ≈ 40nS, p.Vt ≈ -50mV, p.b ≈ 80.5pA.
  if (p.c - 281.0F).abs() > 0.1F {
    fail("C (c) = \{p.c}, expected ≈ 281.0")
  }
  if (p.gl - 40.0F).abs() > 0.1F {
    fail("gl = \{p.gl}, expected ≈ 40.0")
  }
  if p.vt != -50.0F {
    fail("Vt (vt) = \{p.vt}, expected -50.0")
  }
  if (p.b - 80.5F).abs() > 0.1F {
    fail("b = \{p.b}, expected ≈ 80.5")
  }
}

///|
test "AdExParameter::with_vr: custom Vr" {
  let p = AdExParameter::with_vr(-65.0F)
  if p.vr != -65.0F {
    fail("custom Vr = \{p.vr}, expected -65")
  }
  // Other fields preserved.
  if (p.c - 281.0F).abs() > 0.1F {
    fail("C preserved, got \{p.c}")
  }
}

///|
test "IZParameter::new / rs / fs: defaults match Julia" {
  let p = IZParameter::new()
  // Julia: hasproperty(p, :a), :b, :c, :d. MoonBit fields: a, b, c, d.
  if p.a != 0.01F {
    fail("a = \{p.a}, expected 0.01")
  }
  if p.b != 0.2F {
    fail("b = \{p.b}, expected 0.2")
  }
  if p.c != -65.0F {
    fail("c = \{p.c}, expected -65")
  }
  if p.d != 2.0F {
    fail("d = \{p.d}, expected 2.0")
  }
  // IZParameter::rs and fs return different parameter sets; verify
  // they're not the same as new().
  let rs = IZParameter::rs()
  let fs = IZParameter::fs()
  if rs.a == p.a && rs.b == p.b && rs.c == p.c {
    fail("rs should differ from new() defaults")
  }
  if fs.a == p.a && fs.b == p.b {
    fail("fs should differ from new() defaults")
  }
}

///|
test "MorrisLecarParameter::new: defaults match Julia" {
  let p = MorrisLecarParameter::new()
  // Julia: hasproperty(p, :Cm), :El, :gCa, :gK. MoonBit: cm, el, gca, gk.
  if p.cm != 6.69F {
    fail("Cm (cm) = \{p.cm}, expected 6.69")
  }
  if p.el != -50.0F {
    fail("El (el) = \{p.el}, expected -50.0")
  }
  if p.gca != 1.1F {
    fail("gCa (gca) = \{p.gca}, expected 1.1")
  }
  if p.gk != 2.0F {
    fail("gK (gk) = \{p.gk}, expected 2.0")
  }
}

///|
test "HetRecParameter::new: defaults match Julia" {
  let p = HetRecParameter::new()
  // Julia: p.Nd=2, p.τm=20ms, p.τrate=100ms, p.steepness=1.0, p.τabs=5ms.
  if p.nd != 2 {
    fail("Nd (nd) = \{p.nd}, expected 2")
  }
  if p.tau_m != 20.0F {
    fail("τm (tau_m) = \{p.tau_m}, expected 20.0")
  }
  if p.tau_rate != 100.0F {
    fail("τrate (tau_rate) = \{p.tau_rate}, expected 100.0")
  }
  if p.steepness != 1.0F {
    fail("steepness = \{p.steepness}, expected 1.0")
  }
  if p.tau_abs != 5.0F {
    fail("τabs (tau_abs) = \{p.tau_abs}, expected 5.0")
  }
}

///|
test "PostSpike::new: defaults match Julia" {
  let sp = PostSpike::new()
  // Julia PostSpike has At, τA, τabs fields. MoonBit IF PostSpike
  // has tabs_const (τabs). Tripod uses AdExPostSpike with all three
  // fields; verify the IF PostSpike has the τabs field.
  if sp.tabs_const != 2.0F {
    fail("τabs (tabs_const) = \{sp.tabs_const}, expected 2.0")
  }
}

///|
test "AdExPostSpike (Tripod variant): all PostSpike fields present" {
  // The Tripod population uses AdExPostSpike which has the
  // Julia PostSpike fields: At, τA, plus AP_membrane, τabs, up.
  let sp = AdExPostSpike::new()
  if sp.at != 0.0F {
    fail("At (at) = \{sp.at}, expected 0.0")
  }
  if sp.tau_a != 10.0F {
    fail("τA (tau_a) = \{sp.tau_a}, expected 10.0")
  }
  if sp.tabs_const != 1.0F {
    fail("τabs (tabs_const) = \{sp.tabs_const}, expected 1.0")
  }
}

///|
test "IF has DoubleExpSynapse-style scalar time constants" {
  // The MoonBit IF struct has tre/tde/tri/tdi as scalar time constants
  // on the IF struct (DoubleExpSynapse-style). SingleExpSynapse is
  // the default IF constructor behaviour. Verify the time constants
  // exist with their Julia defaults.
  let p = IFParameter::new()
  let rng = Xoshiro::default()
  let pop = IF::new(2, p, rng)
  if pop.tre != 1.0F {
    fail("tre (τr of excitatory) = \{pop.tre}, expected 1.0")
  }
  if pop.tde != 6.0F {
    fail("tde (τd of excitatory) = \{pop.tde}, expected 6.0")
  }
  if pop.tri != 0.5F {
    fail("tri (τr of inhibitory) = \{pop.tri}, expected 0.5")
  }
  if pop.tdi != 2.0F {
    fail("tdi (τd of inhibitory) = \{pop.tdi}, expected 2.0")
  }
}

///|
test "Receptor / NMDAVoltageDependency constructors work" {
  // Receptor — minimal AMPA-like synapse kinetics.
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  if ampa.e_rev != 0.0F {
    fail("AMPA E_rev = \{ampa.e_rev}, expected 0.0")
  }
  if (ampa.tau_r - 0.26F).abs() > 1.0e-6F {
    fail("AMPA τr = \{ampa.tau_r}, expected 0.26")
  }
  // NMDAVoltageDependency — defaults (Eyal-style).
  let nmda_dep = NMDAVoltageDependency::eyal()
  if (nmda_dep.mg - 1.0F).abs() > 1.0e-6F {
    fail("NMDA mg = \{nmda_dep.mg}, expected 1.0")
  }
  // Soma NMDA variant.
  let nmda_soma = NMDAVoltageDependency::soma()
  if (nmda_soma.b - 3.57F).abs() > 1.0e-6F {
    fail("Soma NMDA b = \{nmda_soma.b}, expected 3.57")
  }
}