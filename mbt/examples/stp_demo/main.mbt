// stp_demo.mbt — Markram STP driven by a scheduled spike train.
//
// Demonstrates Markram (1998) short-term plasticity dynamics under
// two protocols:
//   1. Single isolated spike — verifies first-spike priming (ρ = U).
//   2. Long spike train @ 50ms ISI — verifies ρ reaches steady-state
//      around 0.2 (with default U=0.2) and x settles below 1 due
//      to depression accumulation between fast spikes.
//
// Math (matches Julia update_traces!):
//   On pre fire at time T:
//     ΔT = max(0, T - last_spike)
//     u = U - (U - u) * expf(-ΔT / τF)
//     x = 1 - (1 - x) * expf(-ΔT / τD)
//     ρ = u * x   (broadcast to outgoing ρ[s])
//     u += U * (1 - u)
//     x -= u * x
//
// Reference: SNNModels.jl/src/connections/sparse_plasticity/STP.jl

///|
fn print_state(label : String, t : Float, u : Float, x : Float, rho : Float, glu : Float) -> Unit {
  println(
    label + " t=" + t.to_string() +
    "  u=" + u.to_string() +
    "  x=" + x.to_string() +
    "  rho=" + rho.to_string() +
    "  post.glu=" + glu.to_string(),
  )
}

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let pre = @snn_mbt.IF::new(1, @snn_mbt.IFParameter::new(), rng)
  let post = @snn_mbt.IF::new(1, @snn_mbt.IFParameter::new(), rng)
  let syn = @snn_mbt.SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(syn, 1, 1, 1.0F)
  @snn_mbt.SpikingSynapse::init_rho(syn)
  let p = @snn_mbt.MarkramSTPParameter::new()
  let vars = @snn_mbt.MarkramSTPVariables::new(1, 1, p)

  println("stp_demo.mbt: Markram STP dynamics on a single forced-fire pre neuron")
  println("  params: τD=200ms τF=1500ms U=0.2 W=1.0")
  println("")

  // ===== 1) Single isolated spike at t=10ms =====
  // (baseline: u=0.2, x=1, ρ=0.2)
  pre.fire[0] = true
  @snn_mbt.markram_stp_step(syn, vars, p, 10.0F)
  @snn_mbt.forward_synapse(syn, 10.0F)
  print_state("[isolated]", 10.0F, vars.u[0], vars.x[0], vars.rho_pre[0], post.glu[0])
  pre.fire[0] = false
  // Reset post for next event.
  post.glu[0] = 0.0F
  println("")

  // ===== 2) Paired-pulse: second spike at t=20ms (ISI=10ms) =====
  // Expected: ρ > 0.2 (facilitation > depression at short ISI).
  // ρ broadcast during 2nd spike ≈ 0.236
  pre.fire[0] = true
  @snn_mbt.markram_stp_step(syn, vars, p, 20.0F)
  @snn_mbt.forward_synapse(syn, 20.0F)
  print_state("[paired 10ms]", 20.0F, vars.u[0], vars.x[0], vars.rho_pre[0], post.glu[0])
  pre.fire[0] = false
  post.glu[0] = 0.0F
  println("")

  // ===== 3) Tetanic burst: 5 spikes @ 50ms ISI =====
  // Within τD (200ms), x drops substantially -> ρ falls.
  let burst_start = 100.0F
  let burst_isi = 50.0F  // 50ms ISI
  let mut k = 0
  while k < 5 {
    pre.fire[0] = true
    let t_now = burst_start + Float::from_int(k) * burst_isi
    @snn_mbt.markram_stp_step(syn, vars, p, t_now)
    @snn_mbt.forward_synapse(syn, t_now)
    print_state("[burst]", t_now, vars.u[0], vars.x[0], vars.rho_pre[0], post.glu[0])
    pre.fire[0] = false
    post.glu[0] = 0.0F
    k = k + 1
  }
  println("")

  // ===== 4) Recovery: spike at t=10000ms (long after burst) =====
  // Should restore u -> U = 0.2, x -> 1, ρ -> 0.2.
  pre.fire[0] = true
  @snn_mbt.markram_stp_step(syn, vars, p, 10000.0F)
  @snn_mbt.forward_synapse(syn, 10000.0F)
  print_state("[recovery]", 10000.0F, vars.u[0], vars.x[0], vars.rho_pre[0], post.glu[0])
  println("")

  // ===== 5) Steady-state with 200-spike train @ 50ms ISI =====
  // Resets vars to baseline, then drives 200 spikes @ 50ms ISI from
  // t=200ms to t=10150ms (10000ms total). Verifies ρ reaches a
  // steady-state around U*<x>·<u> per cycle. With default params,
  // τF=1500ms (slow facilitation recovery) and τD=200ms (fast
  // depression recovery), at ISI=50ms the system spends most of its
  // time depressed: x should oscillate around 0.06-0.10, u around
  // 0.7-0.8, and ρ around 0.05-0.08 in steady state.
  println("--- long spike train @ 50ms ISI (200 spikes) ---")
  // Reset state.
  let vars2 = @snn_mbt.MarkramSTPVariables::new(1, 1, p)
  // Reset ρ to 1.0F (init_rho already did this).
  let train_start = 200.0F
  let train_isi = 50.0F
  let n_spikes = 200
  let mut sample_idx = 0
  let sample_steps : Array[Int] = [0, 1, 4, 9, 19, 49, 99, 149, 199]
  let mut kk = 0
  while kk < n_spikes {
    pre.fire[0] = true
    let t_now = train_start + Float::from_int(kk) * train_isi
    @snn_mbt.markram_stp_step(syn, vars2, p, t_now)
    @snn_mbt.forward_synapse(syn, t_now)
    pre.fire[0] = false
    post.glu[0] = 0.0F
    if sample_idx < sample_steps.length() && kk == sample_steps[sample_idx] {
      print_state(
        "[train@" + kk.to_string() + "]",
        t_now,
        vars2.u[0], vars2.x[0], vars2.rho_pre[0], post.glu[0],
      )
      sample_idx = sample_idx + 1
    }
    kk = kk + 1
  }

  println("")
  println("stp_demo.mbt: done")
}