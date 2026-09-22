// ============================================================================
// set_LTP! / set_STP! — runtime toggle of LTP / STP per-entry.
//
// Port coverage: refs/SNNModels.jl/test/syn/plasticity_params.jl
//                  "set_LTP! / set_STP!" testset (lines 179-190).
//
// Julia's API takes the SpikingSynapse (s.LTPVars.active / s.STPVars.active)
// and mutates the active flag. Our port uses independent STDPEntry /
// MarkramSTPEntry structs held in `HeterogeneousModel.stdp_entries` /
// `.stp_entries`, so the equivalent API takes the entry directly and
// mutates `entry.vars.active[0]`. MoonBit identifier conventions drop
// the trailing `!` from Julia's name.
//
// Both helpers are no-ops when active is already set to the same value
// (set-then-set-with-same-value pattern is safe).
// ============================================================================

// =========================================================================
// STDPEntry (Gerstner 1996) — set_ltp_active / stdp_step no-op
// =========================================================================

///|
test "STDPEntry::set_ltp_active: toggles vars.active[0]" {
  let entry = STDPEntry::new(0, 2, 2)
  // Defaults to active=true.
  if !entry.vars.active[0] {
    fail("default active[0] should be true")
  }
  STDPEntry::set_ltp_active(entry, false)
  if entry.vars.active[0] {
    fail("after set_ltp_active(false), active[0] should be false")
  }
  STDPEntry::set_ltp_active(entry, true)
  if !entry.vars.active[0] {
    fail("after set_ltp_active(true), active[0] should be true")
  }
}

///|
test "stdp_step respects vars.active=false" {
  // Verify the active-gate at the top of stdp_step: with active=false,
  // weights should not change.
  let rng = Xoshiro::default()
  let pre = IF::new(1, IFParameter::new(), rng)
  let post = IF::new(1, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  // Snapshot weight.
  let w_before = syn.matrix.vals[0]
  // Force fires (both pre and post on the same step).
  pre.fire[0] = true
  post.fire[0] = true
  // Active=true path — should change weight.
  let vars_on = STDPVariables::new(1, 1)
  vars_on.active[0] = true
  stdp_step(
    syn.matrix.vals, pre.fire, post.fire, syn.matrix.colptr, syn.matrix.rowptr,
    vars_on, STDPGerstner::new(), 0.0F, 0.125F,
  )
  let w_after_on = syn.matrix.vals[0]
  // Restore weight, then run with active=false — no change expected.
  ignore(syn.matrix.vals.set(0, w_before))
  vars_on.active[0] = false
  stdp_step(
    syn.matrix.vals, pre.fire, post.fire, syn.matrix.colptr, syn.matrix.rowptr,
    vars_on, STDPGerstner::new(), 0.0F, 0.125F,
  )
  let w_after_off = syn.matrix.vals[0]
  if w_after_on == w_before {
    fail("setup error: active=true path should modify weight")
  }
  if w_after_off != w_before {
    fail(
      "active=false path should be a no-op: w_before=\{w_before} vs w_after_off=\{w_after_off}",
    )
  }
}

// =========================================================================
// MarkramSTPEntry (event-based) — set_stp_active / markram_stp_step no-op
// =========================================================================

///|
test "MarkramSTPEntry::set_stp_active: toggles vars.active[0]" {
  let entry = MarkramSTPEntry::new(0, 2, 2)
  if !entry.vars.active[0] {
    fail("default active[0] should be true")
  }
  MarkramSTPEntry::set_stp_active(entry, false)
  if entry.vars.active[0] {
    fail("after set_stp_active(false), active[0] should be false")
  }
  MarkramSTPEntry::set_stp_active(entry, true)
  if !entry.vars.active[0] {
    fail("after set_stp_active(true), active[0] should be true")
  }
}

// =========================================================================
// MarkramSTPEntryHet — set_stp_active (heterogeneous τD/τF/U per-pre)
// =========================================================================

///|
test "MarkramSTPEntryHet::set_stp_active: toggles vars.active[0]" {
  let n_pre = 3
  let n_post = 2
  let param = MarkramSTPParameterHet::homogeneous(n_pre)
  let entry = MarkramSTPEntryHet::new(0, n_pre, n_post, param)
  if !entry.vars.active[0] {
    fail("default active[0] should be true")
  }
  MarkramSTPEntryHet::set_stp_active(entry, false)
  if entry.vars.active[0] {
    fail("after set_stp_active(false), active[0] should be false")
  }
  MarkramSTPEntryHet::set_stp_active(entry, true)
  if !entry.vars.active[0] {
    fail("after set_stp_active(true), active[0] should be true")
  }
}

// =========================================================================
// MarkramSTPEntryTimestep — set_stp_active (continuous-time Markram STP)
// =========================================================================

///|
test "MarkramSTPEntryTimestep::set_stp_active: toggles vars.active[0]" {
  let param = MarkramSTPParameterTimestep::new()
  let entry = MarkramSTPEntryTimestep::new(0, 2, 2, param=param)
  if !entry.vars.active[0] {
    fail("default active[0] should be true")
  }
  MarkramSTPEntryTimestep::set_stp_active(entry, false)
  if entry.vars.active[0] {
    fail("after set_stp_active(false), active[0] should be false")
  }
  MarkramSTPEntryTimestep::set_stp_active(entry, true)
  if !entry.vars.active[0] {
    fail("after set_stp_active(true), active[0] should be true")
  }
}

// =========================================================================
// Round-trip: set_ltp_active(false) → set_ltp_active(true) preserves state.
// =========================================================================

///|
test "STDPEntry::set_ltp_active: round-trip preserves the vars state" {
  let entry = STDPEntry::new(0, 2, 2)
  // Touch the vars so we can verify they're preserved across toggles.
  entry.vars.tpre[0] = 0.05F
  entry.vars.tpost[1] = 0.07F
  STDPEntry::set_ltp_active(entry, false)
  STDPEntry::set_ltp_active(entry, true)
  if entry.vars.tpre[0] != 0.05F {
    fail("tpre[0] should survive the round-trip toggle")
  }
  if entry.vars.tpost[1] != 0.07F {
    fail("tpost[1] should survive the round-trip toggle")
  }
  if !entry.vars.active[0] {
    fail("final active[0] should be true after round-trip")
  }
}

///|
test "MarkramSTPEntry::set_stp_active: round-trip preserves the vars state" {
  let entry = MarkramSTPEntry::new(0, 2, 2)
  entry.vars.u[0] = 0.123F
  entry.vars.x[1] = 0.456F
  MarkramSTPEntry::set_stp_active(entry, false)
  MarkramSTPEntry::set_stp_active(entry, true)
  if entry.vars.u[0] != 0.123F {
    fail("u[0] should survive the round-trip toggle")
  }
  if entry.vars.x[1] != 0.456F {
    fail("x[1] should survive the round-trip toggle")
  }
  if !entry.vars.active[0] {
    fail("final active[0] should be true after round-trip")
  }
}

// =========================================================================
// Alias-name path — Julia's `set_LTP! / set_STP!` (sans `!`) work via
// the MoonBit-canonical `set_ltp_active / set_stp_active`.
// =========================================================================

///|
test "set_LTP_active / set_STP_active: alias names match Julia convention (sans !)" {
  let stdp = STDPEntry::new(0, 2, 2)
  STDPEntry::set_ltp_active(stdp, false)
  if stdp.vars.active[0] {
    fail("alias path: STDP should be inactive")
  }
  let stp_entry = MarkramSTPEntry::new(0, 2, 2)
  MarkramSTPEntry::set_stp_active(stp_entry, false)
  if stp_entry.vars.active[0] {
    fail("alias path: STP should be inactive")
  }
}
