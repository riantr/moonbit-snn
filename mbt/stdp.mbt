// stdp.mbt — minimal Spike-Timing-Dependent Plasticity (STDP).
//
// Port of the Gerstner (1996) STDP rule from
// SNNModels.jl/src/connections/sparse_plasticity/STDP_traces.jl.
//
// Variables tracked per synapse:
//   tpre[j]   — pre-synaptic spike trace for neuron j (exponential decay)
//   tpost[i]  — post-synaptic spike trace for neuron i
//   last_pre[j], last_post[i] — time of most recent spike
//
// Update rule (Gerstner):
//   On each step, decay the traces:
//     tpre[j] *= exp(-dt / τpre)
//     tpost[i] *= exp(-dt / τpost)
//   On a pre spike:  tpre[j] += A_pre
//   On a post spike: tpost[i] += A_post
//   For each connection (j → i):
//     On post spike:  W[i,j] += A_post * tpre[j]
//     On pre spike:   W[i,j] += A_pre * tpost[i]
//   Clamp W to [Wmin, Wmax].

///|
/// STDPGerstner parameter struct.
pub(all) struct STDPGerstner {
  a_pre : Float   // LTP amplitude (pre spike → weight increase)
  a_post : Float  // LTD amplitude (post spike → weight change)
  tau_pre : Float // Pre-synaptic trace time constant (ms)
  tau_post : Float // Post-synaptic trace time constant (ms)
  w_max : Float   // Maximum weight
  w_min : Float   // Minimum weight
}

///|
pub fn STDPGerstner::new() -> STDPGerstner {
  // Defaults match Julia's STDPGerstner (A_pre/A_post scaled to 1e-3).
  { a_pre: 0.01F, a_post: 0.01F, tau_pre: 20.0F, tau_post: 20.0F,
    w_max: 30.0F, w_min: 0.0F }
}

///|
/// STDPVariables — tracks pre/post spike traces per synapse.
pub struct STDPVariables {
  // Per-neuron traces.
  tpre : Array[Float]
  tpost : Array[Float]
  // Time of last spike per neuron.
  last_pre : Array[Float]
  last_post : Array[Float]
  // Active flag.
  active : Array[Bool]
}

///|
/// Initialise STDP variables for a given pre/post population size.
pub fn STDPVariables::new(n_pre : Int, n_post : Int) -> STDPVariables {
  {
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
    last_pre: Array::make(n_pre, 0.0F),
    last_post: Array::make(n_post, 0.0F),
    active: [true],
  }
}

///|
/// STDPEntry — bundles a connection's plasticity rule with mutable
/// per-step state so the compose layer can apply it automatically.
/// The `t_now` field is a 1-element Float array; mutations happen
/// in compose.mbt's step_heterogeneous (cross-module mutation
/// works because Array indexing is always allowed).
pub struct STDPEntry {
  // Connection identifier (index into HeterogeneousModel.conns).
  conn_index : Int
  // Pre- and post-synaptic neuron counts (size of fire buffers).
  n_pre : Int
  n_post : Int
  // Mutable state — updated each step by the compose sim loop.
  vars : STDPVariables
  // Plasticity rule (mutable so `change_plasticity!` can swap params).
  mut param : STDPGerstner
  // Current sim time (ms), advanced by `dt` each step.
  t_now : Array[Float]
}

///|
/// Construct an STDPEntry for the synapse at `conn_index` in
/// `HeterogeneousModel.conns`. The vars are zero-initialised;
/// `t_now` starts at 0.0F.
pub fn STDPEntry::new(conn_index : Int, n_pre : Int, n_post : Int) -> STDPEntry {
  {
    conn_index,
    n_pre,
    n_post,
    vars: STDPVariables::new(n_pre, n_post),
    param: STDPGerstner::new(),
    t_now: [0.0F],
  }
}

///|
/// Toggle LTP (STDP) on/off for this entry — mirrors Julia's
/// `set_LTP!(s::SpikingSynapse, active)`. When `active=false`,
/// `stdp_step` becomes a no-op for this entry (traces don't decay,
/// weights don't update). When `active=true`, normal STDP resumes
/// from whatever trace state is currently in `vars`.
///
/// Note: MoonBit identifiers can't contain `!`, so the trailing bang
/// is dropped. The semantic is identical — mutate the entry's active
/// flag in place.
pub fn STDPEntry::set_ltp_active(entry : STDPEntry, active : Bool) -> Unit {
  if entry.vars.active.length() > 0 {
    entry.vars.active[0] = active
  }
}

// =========================================================================
// STDPEntryMexicanHat / STDPEntryAntiSymmetric — companion entry types
// for the MexicanHat and AntiSymmetric kernels. The compose layer
// dispatches on `STDPEntryKind` to pick the right step function.
// =========================================================================

///|
/// STDPEntryMexicanHat — bundles a connection's STDPMexicanHat rule with
/// the raw tpre/tpost trace arrays (MexicanHat has no wrapper struct
/// for its traces; they're just plain `Array[Float]`).
pub struct STDPEntryMexicanHat {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : STDPMexicanHat
  tpre : Array[Float]
  tpost : Array[Float]
  t_now : Array[Float]
}

///|
/// Construct an STDPEntryMexicanHat. `param` defaults to STDPMexicanHat::new().
pub fn STDPEntryMexicanHat::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : STDPMexicanHat = STDPMexicanHat::new(),
) -> STDPEntryMexicanHat {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
    t_now: [0.0F],
  }
}

///|
/// STDPEntryAntiSymmetric — bundles a connection's STDPAntiSymmetric rule
/// with the tr_x/to_y trace state.
pub struct STDPEntryAntiSymmetric {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : STDPAntiSymmetric
  vars : STDPAntiSymmetricVariables
  t_now : Array[Float]
}

///|
/// Construct an STDPEntryAntiSymmetric. `param` defaults to STDPAntiSymmetric::new().
pub fn STDPEntryAntiSymmetric::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : STDPAntiSymmetric = STDPAntiSymmetric::new(),
) -> STDPEntryAntiSymmetric {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    vars: STDPAntiSymmetricVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///|
/// STDPEntryKind — enum-dispatched wrapper around the three STDP entry
/// types. Use these variants in `compose(stdp=[...])` to register a
/// plasticity rule on a specific synapse. Each variant carries the
/// matching `*Entry` struct.
pub(all) enum STDPEntryKind {
  Gerstner_(STDPEntry)
  MexicanHat_(STDPEntryMexicanHat)
  AntiSymmetric_(STDPEntryAntiSymmetric)
  Confavreux2025_(STDPEntryConfavreux2025)
  IstdpRate_(IstdpRateEntry)
  IstdpPotential_(IstdpPotentialEntry)
  Symmetric_(STDPEntrySymmetric)
  CaPlasticity_(CaPlasticityEntry)
}

///|
/// Advance the entry's internal clock by `dt` ms. (Note: since
/// MoonBit passes structs by value, this only updates a local copy.
/// The compose layer mutates `entry.t_now[0]` directly instead.)
pub fn STDPEntry::advance(e : STDPEntry, dt : Float) -> Unit {
  e.t_now[0] = e.t_now[0] + dt
}

///|
/// Runtime swap of the STDPGerstner parameters for an entry.
/// Mirrors Julia's `change_plasticity!(syn; LTP = STDPConfavreux2025())`
/// pattern — caller passes a new STDPGerstner and the entry picks it up.
/// State (tpre/tpost, vars) is preserved.
pub fn STDPEntry::change_plasticity(e : STDPEntry, new_param : STDPGerstner) -> Unit {
  e.param = new_param
}

///|
/// Runtime swap of STDPMexicanHat parameters for an entry.
pub fn STDPEntryMexicanHat::change_plasticity(
  e : STDPEntryMexicanHat,
  new_param : STDPMexicanHat,
) -> Unit {
  e.param = new_param
}

///|
/// Runtime swap of STDPAntiSymmetric parameters for an entry.
pub fn STDPEntryAntiSymmetric::change_plasticity(
  e : STDPEntryAntiSymmetric,
  new_param : STDPAntiSymmetric,
) -> Unit {
  e.param = new_param
}

///|
/// Compute the Gerstner STDP kernel value ΔW at a given Δt = t_post - t_pre.
///
/// Convention: Δt > 0 means post fires after pre (LTP, positive ΔW).
/// Δt < 0 means post fires before pre (LTD, negative ΔW).
///
/// Kernel formula:
///   For Δt > 0: ΔW = A_pre * exp(-Δt / τ_pre)       (potentiation)
///   For Δt < 0: ΔW = -A_post * exp(Δt / τ_post)    (depression)
///   For Δt = 0: ΔW = 0
///
/// Returns 0 if |Δt| is very large (beyond ~5 τ in either direction).
/// `a_pre` is the LTP amplitude, `a_post` is the LTD amplitude (positive
/// values representing magnitude).
pub fn gerstner_kernel(
  dt : Float,
  tau_pre : Float,
  tau_post : Float,
  a_pre : Float,
  a_post : Float,
) -> Float {
  if dt > 0.0F {
    // LTP: post after pre.
    let arg = -dt / tau_pre
    a_pre * expf(arg)
  } else if dt < 0.0F {
    // LTD: post before pre.
    let arg = dt / tau_post
    -a_post * expf(arg)
  } else {
    0.0F
  }
}

///|
/// Plot the Gerstner STDP kernel to stdout using ASCII art.
/// Uses `gerstner_kernel` to compute ΔW(Δt) at each column.
/// Δt sweeps from `-t_max` to `+t_max` (default 100 ms), stepping
/// through `width` columns (default 60). Uses the y-axis to show
/// ΔW values and the x-axis to show Δt in ms.
pub fn stdp_kernel_plot(
  param : STDPGerstner,
  t_max? : Float = 100.0F,
  width? : Int = 60,
  height? : Int = 15,
) -> Unit {
  let n_cols = if width > 1 { width } else { 1 }
  let mut mn = 0.0F
  let mut mx = 0.0F
  // First pass: compute ΔW values and find min/max for y-axis.
  let vals : Array[Float] = []
  let mut i = 0
  while i < n_cols {
    // Δt sweeps from -t_max to +t_max (n_cols samples).
    let dt = -t_max + Float::from_int(i) * (2.0F * t_max) / Float::from_int(n_cols - 1)
    let w = gerstner_kernel(
      dt,
      param.tau_pre,
      param.tau_post,
      param.a_pre,
      param.a_post,
    )
    vals.push(w)
    if i == 0 {
      mn = w
      mx = w
    } else {
      if w < mn { mn = w }
      if w > mx { mx = w }
    }
    i = i + 1
  }
  if mx - mn < 1.0e-9F { mx = mn + 1.0e-9F }
  let vrange = mx - mn
  // Build canvas of n_cols columns and `height` rows.
  let canvas : Array[String] = Array::make(height, "")
  let pad = String::make(n_cols, ' ')
  let row_init = " "
  for r in 0..<height {
    canvas[r] = row_init + pad
  }
  // Place characters: '*' for ΔW ≥ 0 (LTP side), '.' for ΔW < 0 (LTD side).
  let mut c = 0
  while c < n_cols {
    let w = vals[c]
    let normalized = (w - mn) / vrange
    let row_from_top = (Float::from_int(height) - Float::from_int(1) * normalized).to_int()
    let r = if row_from_top < 0 {
      0
    } else if row_from_top >= height {
      height - 1
    } else {
      row_from_top
    }
    let ch = if w >= 0.0F { '*' } else { '.' }
    let row_str = canvas[r]
    let new_row = row_int_set_char(row_str, c + row_init.length(), ch)
    canvas[r] = new_row
    c = c + 1
  }
  // Print y-axis labels at top, mid, bottom.
  println("STDP kernel (Gerstner 1996): ΔW(Δt)")
  let max_label = format_axis_label_kernel(mx)
  let mid_label = format_axis_label_kernel((mx + mn) / 2.0F)
  let min_label = format_axis_label_kernel(mn)
  let label_width = {
    let a = max_label.length()
    let b = mid_label.length()
    let c = min_label.length()
    let m = if a > b { a } else { b }
    if m > c { m } else { c }
  }
  for r in 0..<height {
    let label = if r == 0 {
      max_label
    } else if r == height - 1 {
      min_label
    } else if r == height / 2 {
      mid_label
    } else {
      ""
    }
    let pad_count = if label_width > label.length() {
      label_width - label.length()
    } else {
      0
    }
    let padded = String::make(pad_count, ' ')
    println(padded + label + " |" + canvas[r])
  }
  // Footer: Δt axis.
  let pad_str = String::make(label_width + 3, ' ')
  println(pad_str + "Δt = -" + t_max.to_string() + " → +" + t_max.to_string() + " ms")
}

///|
/// Format a Float for kernel plot axis labels (4 decimal places).
fn format_axis_label_kernel(v : Float) -> String {
  let scaled = v * 10000.0F
  let rounded = scaled.round().to_int()
  let scaled_back = Float::from_int(rounded) / 10000.0F
  scaled_back.to_string()
}

// =========================================================================
// STDPMexicanHat kernel (port of STDP_traces.jl).
//
// Reference: STDPMexicanHat parameter struct + plasticity! function in
//   refs/SNNModels.jl/src/connections/sparse_plasticity/STDP_traces.jl
//
// Kernel form (Mexican-hat / sombrero):
//   MexicanHat(x) = (1 - x) * exp(-x / sqrt(2))
//   where x = (log(tpre / tpost))^2
//
// The kernel integrates to zero over a wide temporal range, so uncorrelated
// pre/post spike trains produce zero mean ΔW on average. This makes it a
// classic decorrelating STDP rule (Rubinov, Sporns, 2011).
//
// Trace dynamics (per step):
//   tpre[j]  += dt * (-tpre[j]) / τ        (continuous exponential decay)
//   tpost[i] += dt * (-tpost[i]) / τ
//   tpre[j]  += 1   if fireJ[j]
//   tpost[i] += 1   if fireI[i]
//
// Weight updates:
//   On pre spike (fireJ[j], iterate outgoing from j):
//     W[s] += A * MexicanHat((log(tpre[J[s]] / tpost[i]))^2)
//   On post spike (fireI[i], iterate incoming to i):
//     W[s] += A * MexicanHat((log(tpre[j] / tpost[I[s]]))^2)
//   Clamp W to [Wmin, Wmax].
// =========================================================================

///|
/// STDPMexicanHat parameter struct (Festa, Cusseddu, Gjorgjieva 2024).
pub(all) struct STDPMexicanHat {
  a : Float     // LTD/LTP learning rate (amplitude of the kernel)
  tau : Float   // Time constant for pre/post traces (ms)
  w_max : Float // Maximum weight
  w_min : Float // Minimum weight (negative for inhibition)
}

///|
/// Defaults match Julia's STDPMexicanHat (A=10e-2, τ=20ms, Wmax=30pF).
pub fn STDPMexicanHat::new() -> STDPMexicanHat {
  { a: 0.1F, tau: 20.0F, w_max: 30.0F, w_min: 0.0F }
}

///|
/// Pure MexicanHat kernel: MexicanHat(x) = (1 - x) * exp(-x / sqrt(2)).
///
/// Returns 0 when x is NaN (matches Julia's `isnan` guard).
pub fn mexican_hat_kernel(x : Float) -> Float {
  if x.is_nan() {
    0.0F
  } else {
    let arg = -x / 1.41421356F // sqrt(2)
    let v = (1.0F - x) * expf(arg)
    if v.is_nan() {
      0.0F
    } else {
      v
    }
  }
}

///|
/// Apply one step of STDPMexicanHat. Mirrors Julia's `plasticity!` for
/// `STDPMexicanHat` (STDP_traces.jl).
///
/// `w` is the CSR sparse weight buffer (vals), `pre_fire[j]` and
/// `post_fire[i]` are the firing booleans, `colptr[s]` is the post-syn
/// neuron for connection s, `rowptr[j]..rowptr[j+1]` are the connections
/// from pre-syn neuron j. `tpre[j]` and `tpost[i]` are exponentially
/// decaying traces (advanced in-place). `t_now` is the current sim time.
pub fn stdp_mexican_hat_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  tpre : Array[Float],
  tpost : Array[Float],
  param : STDPMexicanHat,
  dt : Float,
) -> Unit {
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let inv_tau = 1.0F / param.tau
  // Decay traces (continuous exponential).
  let mut i = 0
  while i < n_post {
    tpost[i] = tpost[i] + dt * (-tpost[i]) * inv_tau
    i = i + 1
  }
  let mut j = 0
  while j < n_pre {
    tpre[j] = tpre[j] + dt * (-tpre[j]) * inv_tau
    j = j + 1
  }
  // Spike increments.
  i = 0
  while i < n_post {
    if post_fire[i] {
      tpost[i] = tpost[i] + 1.0F
    }
    i = i + 1
  }
  j = 0
  while j < n_pre {
    if pre_fire[j] {
      tpre[j] = tpre[j] + 1.0F
    }
    j = j + 1
  }
  // Pre spike pass: iterate outgoing connections from each pre neuron j.
  j = 0
  while j < n_pre {
    if pre_fire[j] {
      let start = rowptr[j]
      let end = rowptr[j + 1]
      let mut s = start
      while s < end {
        let post_idx = colptr[s]
        let ratio = tpre[j] / tpost[post_idx]
        let lnx = logf(ratio)
        let x = lnx * lnx
        let dw = param.a * mexican_hat_kernel(x)
        w[s] = w[s] + dw
        s = s + 1
      }
    }
    j = j + 1
  }
  // Post spike pass: iterate all stored connections, check fireI[i].
  // (Linear scan of NNZ is fine for typical sparse matrices.)
  let nnz = w.length()
  let mut s2 = 0
  while s2 < nnz {
    let post_idx = colptr[s2]
    if post_fire[post_idx] {
      // Find pre neuron j = which row this connection came from. We
      // binary-search rowptr (rows are pre-synaptic).
      let j_pre = find_pre_for_conn(rowptr, s2)
      let ratio = tpre[j_pre] / tpost[post_idx]
      let lnx = logf(ratio)
      let x = lnx * lnx
      let dw = param.a * mexican_hat_kernel(x)
      w[s2] = w[s2] + dw
    }
    s2 = s2 + 1
  }
  // Clamp weights.
  let mut s3 = 0
  while s3 < nnz {
    if w[s3] < param.w_min {
      w[s3] = param.w_min
    } else if w[s3] > param.w_max {
      w[s3] = param.w_max
    }
    s3 = s3 + 1
  }
}

///|
/// Binary search rowptr to find which pre-synaptic neuron owns
/// connection index `s` (i.e., the largest j with rowptr[j] ≤ s).
pub fn find_pre_for_conn(rowptr : Array[Int], s : Int) -> Int {
  let n = rowptr.length() - 1
  let mut lo = 0
  let mut hi = n
  while lo < hi {
    let mid = (lo + hi + 1) / 2
    if rowptr[mid] <= s {
      lo = mid
    } else {
      hi = mid - 1
    }
  }
  lo
}

// =========================================================================
// STDPAntiSymmetric kernel (port of STDP_structured.jl).
//
// Reference: STDPAntiSymmetric parameter struct + plasticity! function in
//   refs/SNNModels.jl/src/connections/sparse_plasticity/STDP_structured.jl
//
// Trace dynamics:
//   tr_x[j]  — pre-synaptic trace (decays with τ_x, +1 on fireJ[j])
//   to_y[i]  — post-synaptic trace (decays with τ_y, +1 on fireI[i])
//
// Weight updates:
//   On pre spike (fireJ[j], iterate outgoing):
//     W[s] += αpre - (A_y / τ_y) * to_y[i]
//   On post spike (fireI[i], iterate incoming):
//     W[s] += αpost + (A_x / τ_x) * tr_x[j]
//   Clamp W to [Wmin, Wmax].
// =========================================================================

///|
/// STDPAntiSymmetric parameter struct (Festa et al. 2024, inhibitory STDP).
pub(all) struct STDPAntiSymmetric {
  a_x : Float   // LTP amplitude (post spike → + A_x / τ_x * tr_x)
  a_y : Float   // LTD amplitude (pre spike → - A_y / τ_y * to_y)
  tau_x : Float // Pre-synaptic trace time constant (ms)
  tau_y : Float // Post-synaptic trace time constant (ms)
  alpha_pre : Float  // Baseline additive term on pre spike
  alpha_post : Float // Baseline additive term on post spike
  w_max : Float
  w_min : Float
}

///|
/// Defaults match Julia's STDPAntiSymmetric (A_x=A_y=3e-2, τ=50ms, etc.).
pub fn STDPAntiSymmetric::new() -> STDPAntiSymmetric {
  {
    a_x: 0.03F,
    a_y: 0.03F,
    tau_x: 50.0F,
    tau_y: 50.0F,
    alpha_pre: 0.0F,
    alpha_post: 0.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
}

///|
/// STDPAntiSymmetric variables — tr_x[j] (pre) and to_y[i] (post) traces.
pub struct STDPAntiSymmetricVariables {
  tr_x : Array[Float] // pre-synaptic trace
  to_y : Array[Float] // post-synaptic trace
}

///|
/// Initialise STDPAntiSymmetric variables for a given pre/post size.
pub fn STDPAntiSymmetricVariables::new(
  n_pre : Int,
  n_post : Int,
) -> STDPAntiSymmetricVariables {
  { tr_x: Array::make(n_pre, 0.0F), to_y: Array::make(n_post, 0.0F) }
}

///|
/// Apply one step of STDPAntiSymmetric. Mirrors Julia's `plasticity!` for
/// `STDPAntiSymmetric` (STDP_structured.jl).
pub fn stdp_antisymmetric_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : STDPAntiSymmetricVariables,
  param : STDPAntiSymmetric,
  dt : Float,
) -> Unit {
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  // Pre-spike pass: iterate outgoing from pre j, update w with to_y[i].
  let mut j = 0
  while j < n_pre {
    if pre_fire[j] {
      let start = rowptr[j]
      let end = rowptr[j + 1]
      let mut s = start
      while s < end {
        let post_idx = colptr[s]
        w[s] = w[s] + param.alpha_pre - (param.a_y / param.tau_y) * vars.to_y[post_idx]
        s = s + 1
      }
    }
    j = j + 1
  }
  // Post-spike pass: iterate all connections, check fireI[i].
  let nnz = w.length()
  let a_x_over_tau_x = param.a_x / param.tau_x
  let mut s2 = 0
  while s2 < nnz {
    let post_idx = colptr[s2]
    if post_fire[post_idx] {
      let j_pre = find_pre_for_conn(rowptr, s2)
      w[s2] = w[s2] + param.alpha_post + a_x_over_tau_x * vars.tr_x[j_pre]
    }
    s2 = s2 + 1
  }
  // Trace decay (continuous).
  let inv_tau_x = 1.0F / param.tau_x
  let inv_tau_y = 1.0F / param.tau_y
  let mut i = 0
  while i < n_post {
    vars.to_y[i] = vars.to_y[i] + dt * (-vars.to_y[i]) * inv_tau_y
    i = i + 1
  }
  j = 0
  while j < n_pre {
    vars.tr_x[j] = vars.tr_x[j] + dt * (-vars.tr_x[j]) * inv_tau_x
    j = j + 1
  }
  // Spike increments.
  i = 0
  while i < n_post {
    if post_fire[i] {
      vars.to_y[i] = vars.to_y[i] + 1.0F
    }
    i = i + 1
  }
  j = 0
  while j < n_pre {
    if pre_fire[j] {
      vars.tr_x[j] = vars.tr_x[j] + 1.0F
    }
    j = j + 1
  }
  // Clamp.
  let mut s3 = 0
  while s3 < nnz {
    if w[s3] < param.w_min {
      w[s3] = param.w_min
    } else if w[s3] > param.w_max {
      w[s3] = param.w_max
    }
    s3 = s3 + 1
  }
}

///|
/// Plot the STDPMexicanHat kernel as a function of x = (ln(tpre/tpost))^2.
/// Sweeps x from 0 to x_max (default 5.0) and shows the resulting ΔW.
/// The kernel is `(1 - x) * exp(-x / sqrt(2))` which starts at 1 at x=0,
/// crosses zero at x=1, and decays to small negative values.
pub fn stdp_mexican_hat_plot(
  param : STDPMexicanHat,
  x_max? : Float = 5.0F,
  width? : Int = 60,
  height? : Int = 15,
) -> Unit {
  let n_cols = if width > 1 { width } else { 1 }
  let mut mn = 0.0F
  let mut mx = 0.0F
  // First pass: kernel values and min/max.
  let vals : Array[Float] = []
  let mut k = 0
  while k < n_cols {
    let x = Float::from_int(k) * x_max / Float::from_int(n_cols - 1)
    let m = param.a * mexican_hat_kernel(x)
    vals.push(m)
    if k == 0 {
      mn = m
      mx = m
    } else {
      if m < mn { mn = m }
      if m > mx { mx = m }
    }
    k = k + 1
  }
  if mx - mn < 1.0e-9F { mx = mn + 1.0e-9F }
  let vrange = mx - mn
  let canvas : Array[String] = Array::make(height, "")
  let pad = String::make(n_cols, ' ')
  let row_init = " "
  for r in 0..<height {
    canvas[r] = row_init + pad
  }
  let mut c = 0
  while c < n_cols {
    let w = vals[c]
    let normalized = (w - mn) / vrange
    let row_from_top = (Float::from_int(height) - Float::from_int(1) * normalized).to_int()
    let r = if row_from_top < 0 {
      0
    } else if row_from_top >= height {
      height - 1
    } else {
      row_from_top
    }
    let ch = if w >= 0.0F { '*' } else { '.' }
    let row_str = canvas[r]
    let new_row = row_int_set_char(row_str, c + row_init.length(), ch)
    canvas[r] = new_row
    c = c + 1
  }
  println("STDPMexicanHat kernel: A * (1-x) * exp(-x/sqrt(2))")
  let max_label = format_axis_label_kernel(mx)
  let mid_label = format_axis_label_kernel((mx + mn) / 2.0F)
  let min_label = format_axis_label_kernel(mn)
  let label_width = {
    let a = max_label.length()
    let b = mid_label.length()
    let c = min_label.length()
    let m = if a > b { a } else { b }
    if m > c { m } else { c }
  }
  for r in 0..<height {
    let label = if r == 0 {
      max_label
    } else if r == height - 1 {
      min_label
    } else if r == height / 2 {
      mid_label
    } else {
      ""
    }
    let pad_count = if label_width > label.length() {
      label_width - label.length()
    } else {
      0
    }
    let padded = String::make(pad_count, ' ')
    println(padded + label + " |" + canvas[r])
  }
  let pad_str = String::make(label_width + 3, ' ')
  println(pad_str + "x = (ln(tpre/tpost))^2 ∈ [0, " + x_max.to_string() + "]")
}

///|
/// Plot the STDPAntiSymmetric weight update as a function of post-synaptic
/// trace `to_y[i]`. Shows dW = αpre - (A_y / τ_y) * to_y[i] over a sweep
/// of to_y values from 0 to y_max (default 5.0).
pub fn stdp_antisymmetric_plot(
  param : STDPAntiSymmetric,
  y_max? : Float = 5.0F,
  width? : Int = 60,
  height? : Int = 15,
) -> Unit {
  let n_cols = if width > 1 { width } else { 1 }
  let mut mn = 0.0F
  let mut mx = 0.0F
  let vals : Array[Float] = []
  let mut k = 0
  while k < n_cols {
    let y = Float::from_int(k) * y_max / Float::from_int(n_cols - 1)
    let dw = param.alpha_pre - (param.a_y / param.tau_y) * y
    vals.push(dw)
    if k == 0 {
      mn = dw
      mx = dw
    } else {
      if dw < mn { mn = dw }
      if dw > mx { mx = dw }
    }
    k = k + 1
  }
  if mx - mn < 1.0e-9F { mx = mn + 1.0e-9F }
  let vrange = mx - mn
  let canvas : Array[String] = Array::make(height, "")
  let pad = String::make(n_cols, ' ')
  let row_init = " "
  for r in 0..<height {
    canvas[r] = row_init + pad
  }
  let mut c = 0
  while c < n_cols {
    let w = vals[c]
    let normalized = (w - mn) / vrange
    let row_from_top = (Float::from_int(height) - Float::from_int(1) * normalized).to_int()
    let r = if row_from_top < 0 {
      0
    } else if row_from_top >= height {
      height - 1
    } else {
      row_from_top
    }
    let ch = if w >= 0.0F { '*' } else { '.' }
    canvas[r] = row_int_set_char(canvas[r], c + row_init.length(), ch)
    c = c + 1
  }
  println("STDPAntiSymmetric (pre-spike): dW = αpre - (A_y/τ_y) * to_y")
  let max_label = format_axis_label_kernel(mx)
  let mid_label = format_axis_label_kernel((mx + mn) / 2.0F)
  let min_label = format_axis_label_kernel(mn)
  let label_width = {
    let a = max_label.length()
    let b = mid_label.length()
    let c = min_label.length()
    let m = if a > b { a } else { b }
    if m > c { m } else { c }
  }
  for r in 0..<height {
    let label = if r == 0 {
      max_label
    } else if r == height - 1 {
      min_label
    } else if r == height / 2 {
      mid_label
    } else {
      ""
    }
    let pad_count = if label_width > label.length() {
      label_width - label.length()
    } else {
      0
    }
    let padded = String::make(pad_count, ' ')
    println(padded + label + " |" + canvas[r])
  }
  let pad_str = String::make(label_width + 3, ' ')
  println(pad_str + "to_y ∈ [0, " + y_max.to_string() + "]")
}

///|
/// Compute the mean weight change for STDP under uncorrelated
/// Poisson pre/post spike trains (decorrelated regime). For
/// exponentially-decaying kernels (Gerstner), the integral of
/// the LTP side equals (A_pre * τ_pre - A_post * τ_post) when
/// post fires before pre (LTD).
///
/// This is the MoonBit equivalent of Julia's
/// `SNN.stdp_weight_decorrelated(stdp_param)` which computes the
/// mean ΔW analytically for an uncorrelated pre/post Poisson
/// regime.
///
/// Formula (from Rubinov et al 2011; assumes symmetric bounds):
///   <ΔW> = A_pre * τ_pre - A_post * τ_post
///
/// Returns the scalar mean ΔW.
pub fn stdp_weight_decorrelated(param : STDPGerstner) -> Float {
  param.a_pre * param.tau_pre - param.a_post * param.tau_post
}

///|
/// Apply one step of Gerstner STDP. Mutates `w` in place based on
/// `fire_pre[j]` (pre-synaptic neuron j fired this step) and
/// `fire_post[i]` (post-synaptic neuron i fired this step).
/// `t_now` is the current simulation time (ms).
///
/// `w` is laid out in the same row-major CSR format as
/// `SparseMatrixCSR.vals`: w[s] is the weight for the s-th
/// connection. To map s to (j, i), the caller can use the
/// SpikingSynapse's matrix.colptr / .rowptr.
pub fn stdp_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : STDPVariables,
  param : STDPGerstner,
  t_now : Float,
  dt : Float,
) -> Unit {
  // Skip when STDP is disabled (mirrors Julia's `set_LTP!(s, false)`).
  if vars.active.length() > 0 && !vars.active[0] {
    return
  }
  // Decay traces.
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let decay_pre : Float = expf(-dt / param.tau_pre)
  let decay_post : Float = expf(-dt / param.tau_post)
  let mut j = 0
  while j < n_pre {
    vars.tpre[j] = vars.tpre[j] * decay_pre
    if pre_fire[j] {
      vars.tpre[j] = vars.tpre[j] + param.a_pre
      vars.last_pre[j] = t_now
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    vars.tpost[i] = vars.tpost[i] * decay_post
    if post_fire[i] {
      vars.tpost[i] = vars.tpost[i] + param.a_post
      vars.last_post[i] = t_now
    }
    i = i + 1
  }
  // Update weights. CSR layout: row j (pre) has connections
  // rowptr[j]..rowptr[j+1]. Each connection s has colptr[s] = i (post).
  j = 0
  while j < n_pre {
    let start = rowptr[j]
    let end = rowptr[j + 1]
    let mut s = start
    while s < end {
      let post_idx = colptr[s]
      let pre_fired = pre_fire[j]
      let post_fired = post_fire[post_idx]
      if pre_fired {
        // Pre spike: add A_post * tpost[i]
        w[s] = w[s] + param.a_post * vars.tpost[post_idx]
      }
      if post_fired {
        // Post spike: add A_pre * tpre[j]
        w[s] = w[s] + param.a_pre * vars.tpre[j]
      }
      // Clamp.
      if w[s] < param.w_min { w[s] = param.w_min }
      if w[s] > param.w_max { w[s] = param.w_max }
      s = s + 1
    }
    j = j + 1
  }
}
///|
/// STDPConfavreux2025 — Confavreux 2025 STDP variant with separable
/// pre/post contributions and baseline rate dependencies (alpha, beta).
///
/// Reference: SpikingNeuralNetworks.jl/src/connections/sparse_plasticity/
/// STDP_traces.jl. Update rule per synapse (j -> i):
///   On pre spike (fireJ[j]): W[s] += eta * (kappa * Deltapost[i] + alpha)
///   On post spike (fireI[i]): W[s] += eta * (gamma * Deltapre[j] + beta)
///   Clamp W to [w_min, w_max].
///
/// Deltapre[j] and Deltapost[i] are the pre/post spike traces at the
/// current time (after continuous exponential decay + spike bump). The
/// `alpha` / `beta` baseline terms add a constant offset on every
/// spike event, regardless of the partner trace — these encode the
/// "baseline rate dependency" that drives competition.
///
/// Field naming: Julia's eta -> `eta`, alpha -> `alpha`, beta -> `beta`,
/// kappa -> `kappa`, gamma -> `gamma`. MoonBit doesn't accept Greek
/// letters in identifiers.
pub(all) struct STDPConfavreux2025 {
  eta : Float
  alpha : Float
  beta : Float
  kappa : Float
  gamma : Float
  tau_pre : Float
  tau_post : Float
  w_max : Float
  w_min : Float
}

///|
/// Defaults match Julia's STDPConfavreux2025 (eta=0.01, alpha=0, beta=0,
/// kappa=1, gamma=1, tau_pre=20ms, tau_post=20ms, w_min=0, w_max=30).
pub fn STDPConfavreux2025::new() -> STDPConfavreux2025 {
  {
    eta: 0.01F,
    alpha: 0.0F,
    beta: 0.0F,
    kappa: 1.0F,
    gamma: 1.0F,
    tau_pre: 20.0F,
    tau_post: 20.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
}

///|
/// STDPEntryConfavreux2025 — bundles a connection's STDPConfavreux2025
/// rule with the per-step state (traces + last-spike times) plus an
/// internal `t_now` clock so the compose layer can advance simulation
/// time across step calls.
pub struct STDPEntryConfavreux2025 {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : STDPConfavreux2025
  vars : STDPVariables
  t_now : Array[Float]
}

///|
/// Construct an STDPEntryConfavreux2025. `vars` is zero-initialised;
/// `t_now` starts at 0.0F. Caller wires the entry into
/// `HeterogeneousModel.stdp_entries` via `Confavreux2025_(entry)`.
pub fn STDPEntryConfavreux2025::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : STDPConfavreux2025 = STDPConfavreux2025::new(),
) -> STDPEntryConfavreux2025 {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    vars: STDPVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///|
/// Runtime swap of STDPConfavreux2025 parameters. Preserves trace state.
pub fn STDPEntryConfavreux2025::change_plasticity(
  e : STDPEntryConfavreux2025,
  new_param : STDPConfavreux2025,
) -> Unit {
  e.param = new_param
}

///|
/// One step of the Confavreux 2025 STDP rule. Mirrors the loop structure
/// of `stdp_step` (Gerstner): continuous decay of tpre/tpost traces +
/// spike bump, then a single pass over all stored connections that
/// applies both the pre-fire and post-fire contributions using the
/// post index recovered from `colptr[s]`. Weights are clamped to
/// [w_min, w_max] after each connection update.
///
/// CSR layout (matches the rest of the SNN port):
///   - `rowptr[j]..rowptr[j+1]` lists the non-zero positions for row j
///     (pre-neuron j). Each connection s in that range has
///     `colptr[s]` = i (post-neuron).
///
/// Trace model: `vars.tpre[j]` and `vars.tpost[i]` are continuously
/// decayed each step with `exp(-dt/tau_pre)` and `exp(-dt/tau_post)`,
/// then bumped by 1.0F on a spike. This is mathematically equivalent
/// to Julia's time-since-last-spike formulation: Δpre[j] after spike
/// at t_1 and dt later is `tpre[j] * exp(-dt/tau_pre)`, matching
/// `tpre_0 * exp(-(t_2 - t_1)/tau_pre) + 1f0 * exp(-(t_3 - t_2)/tau_pre)`.
pub fn stdp_confavreux_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : STDPVariables,
  param : STDPConfavreux2025,
  t_now : Float,
  dt : Float,
) -> Unit {
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let decay_pre : Float = expf(-dt / param.tau_pre)
  let decay_post : Float = expf(-dt / param.tau_post)
  // 1. Decay traces + bump on spike.
  let mut j = 0
  while j < n_pre {
    vars.tpre[j] = vars.tpre[j] * decay_pre
    if pre_fire[j] {
      vars.tpre[j] = vars.tpre[j] + 1.0F
      vars.last_pre[j] = t_now
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    vars.tpost[i] = vars.tpost[i] * decay_post
    if post_fire[i] {
      vars.tpost[i] = vars.tpost[i] + 1.0F
      vars.last_post[i] = t_now
    }
    i = i + 1
  }
  // 2. Walk all connections once. For each connection (j -> i):
  //    if pre_fire[j]:  w[s] += eta * (kappa * tpost[i] + alpha)
  //    if post_fire[i]: w[s] += eta * (gamma * tpre[j]  + beta)
  //    clamp w[s] to [w_min, w_max].
  j = 0
  while j < n_pre {
    let start = rowptr[j]
    let end = rowptr[j + 1]
    let pre_fired = pre_fire[j]
    let tpre_j = vars.tpre[j]
    let mut s = start
    while s < end {
      let post_idx = colptr[s]
      let post_fired = post_fire[post_idx]
      let tpost_i = vars.tpost[post_idx]
      if pre_fired {
        // Pre spike: kappa-scaled post trace + alpha baseline.
        w[s] = w[s] + param.eta * (param.kappa * tpost_i + param.alpha)
      }
      if post_fired {
        // Post spike: gamma-scaled pre trace + beta baseline.
        w[s] = w[s] + param.eta * (param.gamma * tpre_j + param.beta)
      }
      // Clamp.
      if w[s] < param.w_min { w[s] = param.w_min }
      if w[s] > param.w_max { w[s] = param.w_max }
      s = s + 1
    }
    j = j + 1
  }
}

// =========================================================================
// STDPSymmetric — symmetric inhibitory STDP from Festa, Cusseddu &
// Gjorgjieva (2024). Port of Julia's `STDPSymmetric` from
// STDP_structured.jl. Distinct from STDPAntiSymmetric (which uses 2
// traces and produces pure LTD / LTP based on pre/post firing).
// STDPSymmetric uses 4 traces (tr_x, tr_y for pre; to_x, to_y for
// post) and a kernel whose integral is zero — so the symmetric rule
// stabilises total network activity rather than driving runaway
// potentiation.
//
// The kernel (per spike event):
//   Pre-spike:  dW = alpha_pre + (A_x / 2*tau_x * to_x[i] - A_y / 2*tau_y * to_y[i])
//   Post-spike: dW = alpha_post + (A_x / 2*tau_x * tr_x[j] - A_y / 2*tau_y * tr_y[j])
//   (where j is the pre-synaptic index, i is the post-synaptic index)
// Both to_x/to_y traces bump on post-spike; both tr_x/tr_y traces
// bump on pre-spike. All four traces decay continuously every step.
//
// All four traces use exponential decay `dt * (-t) / tau` with their
// respective time constants. =========================================================================

///|
/// STDPSymmetric parameter struct (Festa et al. 2024, inhibitory STDP
/// with zero-integral kernel).
///
/// Fields:
///   - a_x : LTP learning rate (pre→post facilitation)
///   - a_y : LTD learning rate (post→pre depression)
///   - tau_x : time constant for `tr_x` / `to_x` traces (pre and post spike-traces)
///   - tau_y : time constant for `tr_y` / `to_y` traces (pre and post spike-traces)
///   - alpha_pre : constant offset on pre-spike
///   - alpha_post : constant offset on post-spike
///   - w_max / w_min : weight bounds
pub(all) struct STDPSymmetric {
  a_x : Float
  a_y : Float
  tau_x : Float
  tau_y : Float
  alpha_pre : Float
  alpha_post : Float
  w_max : Float
  w_min : Float
}

///|
/// Defaults match Julia's STDPSymmetric (A_x=A_y=3e-2, tau_x=50ms,
/// tau_y=500ms, alpha_pre=alpha_post=0, w_max=30, w_min=0).
pub fn STDPSymmetric::new() -> STDPSymmetric {
  {
    a_x: 0.03F,
    a_y: 0.03F,
    tau_x: 50.0F,
    tau_y: 500.0F,
    alpha_pre: 0.0F,
    alpha_post: 0.0F,
    w_max: 30.0F,
    w_min: 0.0F,
  }
}

///|
/// STDPSymmetricVariables — four trace arrays:
///   tr_x[j] : pre-synaptic trace that bumps on pre-spike, decays
///              toward 0 with time constant tau_x. Read on post-spike.
///   tr_y[j] : pre-synaptic trace that bumps on pre-spike, decays
///              toward 0 with time constant tau_y. Read on post-spike.
///   to_x[i] : post-synaptic trace that bumps on post-spike, decays
///              toward 0 with time constant tau_x. Read on pre-spike.
///   to_y[i] : post-synaptic trace that bumps on post-spike, decays
///              toward 0 with time constant tau_y. Read on pre-spike.
pub struct STDPSymmetricVariables {
  tr_x : Array[Float]
  tr_y : Array[Float]
  to_x : Array[Float]
  to_y : Array[Float]
}

///|
pub fn STDPSymmetricVariables::new(
  n_pre : Int,
  n_post : Int,
) -> STDPSymmetricVariables {
  {
    tr_x: Array::make(n_pre, 0.0F),
    tr_y: Array::make(n_pre, 0.0F),
    to_x: Array::make(n_post, 0.0F),
    to_y: Array::make(n_post, 0.0F),
  }
}

///|
/// STDPEntrySymmetric — bundles a connection's STDPSymmetric rule with
/// the per-step state plus an internal `t_now` clock.
pub struct STDPEntrySymmetric {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : STDPSymmetric
  vars : STDPSymmetricVariables
  t_now : Array[Float]
}

///|
pub fn STDPEntrySymmetric::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : STDPSymmetric = STDPSymmetric::new(),
) -> STDPEntrySymmetric {
  {
    conn_index,
    n_pre,
    n_post,
    param,
    vars: STDPSymmetricVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///|
/// Runtime swap of STDPSymmetric parameters. Preserves trace state.
pub fn STDPEntrySymmetric::change_plasticity(
  e : STDPEntrySymmetric,
  new_param : STDPSymmetric,
) -> Unit {
  e.param = new_param
}

///|
/// One step of STDPSymmetric.
///
/// Trace model (continuous-time Euler):
///   tr_x[j] += dt * (-tr_x[j]) / tau_x      if fireJ[j]: bump tr_x[j]
///   tr_y[j] += dt * (-tr_y[j]) / tau_y      if fireJ[j]: bump tr_y[j]
///   to_x[i] += dt * (-to_x[i]) / tau_x      if fireI[i]: bump to_x[i]
///   to_y[i] += dt * (-to_y[i]) / tau_y      if fireI[i]: bump to_y[i]
///
/// Weight update per stored connection (s = (j -> post_idx)):
///   if pre_fire[j]:
///     w[s] += alpha_pre + (a_x / (2*tau_x) * to_x[post_idx]
///                         - a_y / (2*tau_y) * to_y[post_idx])
///   if post_fire[post_idx]:
///     w[s] += alpha_post + (a_x / (2*tau_x) * tr_x[j]
///                          - a_y / (2*tau_y) * tr_y[j])
///   Clamp w[s] to [w_min, w_max].
///
/// CSR layout: rowptr[j]..rowptr[j+1] lists non-zero positions for
/// row j (pre-neuron j); colptr[s] = i (post-neuron). We walk
/// rowptr[j] once per j and apply both pre-fire and post-fire
/// contributions in the same loop (same fused pattern as the other
/// STDP variants).
pub fn stdp_symmetric_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : STDPSymmetricVariables,
  param : STDPSymmetric,
  t_now : Float,
  dt : Float,
) -> Unit {
  let _ = t_now
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let inv_tau_x : Float = 1.0F / param.tau_x
  let inv_tau_y : Float = 1.0F / param.tau_y
  // 1. Trace decay + spike bump.
  //    tr_x, tr_y bump on pre-fire; to_x, to_y bump on post-fire.
  let mut j = 0
  while j < n_pre {
    vars.tr_x[j] = vars.tr_x[j] + dt * (-vars.tr_x[j]) * inv_tau_x
    vars.tr_y[j] = vars.tr_y[j] + dt * (-vars.tr_y[j]) * inv_tau_y
    if pre_fire[j] {
      vars.tr_x[j] = vars.tr_x[j] + 1.0F
      vars.tr_y[j] = vars.tr_y[j] + 1.0F
    }
    j = j + 1
  }
  let mut i = 0
  while i < n_post {
    vars.to_x[i] = vars.to_x[i] + dt * (-vars.to_x[i]) * inv_tau_x
    vars.to_y[i] = vars.to_y[i] + dt * (-vars.to_y[i]) * inv_tau_y
    if post_fire[i] {
      vars.to_x[i] = vars.to_x[i] + 1.0F
      vars.to_y[i] = vars.to_y[i] + 1.0F
    }
    i = i + 1
  }
  // 2. Walk all connections. For each connection (j -> post_idx):
  //    if pre_fire[j]:
  //      w[s] += alpha_pre + (a_x/(2*tau_x) * to_x[post_idx]
  //                          - a_y/(2*tau_y) * to_y[post_idx])
  //    if post_fire[post_idx]:
  //      w[s] += alpha_post + (a_x/(2*tau_x) * tr_x[j]
  //                           - a_y/(2*tau_y) * tr_y[j])
  //    clamp w[s] to [w_min, w_max].
  let coef_x : Float = param.a_x / (2.0F * param.tau_x)
  let coef_y : Float = param.a_y / (2.0F * param.tau_y)
  j = 0
  while j < n_pre {
    let start = rowptr[j]
    let end = rowptr[j + 1]
    let pre_fired = pre_fire[j]
    let tr_x_j = vars.tr_x[j]
    let tr_y_j = vars.tr_y[j]
    let mut s = start
    while s < end {
      let post_idx = colptr[s]
      let post_fired = post_fire[post_idx]
      let to_x_i = vars.to_x[post_idx]
      let to_y_i = vars.to_y[post_idx]
      if pre_fired {
        let dw = param.alpha_pre + coef_x * to_x_i - coef_y * to_y_i
        w[s] = w[s] + dw
      }
      if post_fired {
        let dw = param.alpha_post + coef_x * tr_x_j - coef_y * tr_y_j
        w[s] = w[s] + dw
      }
      if w[s] < param.w_min { w[s] = param.w_min }
      if w[s] > param.w_max { w[s] = param.w_max }
      s = s + 1
    }
    j = j + 1
  }
}

///| CaPlasticityParameter (Brette-Gerstner 2005 pair-spike form with
/// explicit trace-amplitude scaling). Mirrors Julia's CaPlasticityParameter
/// from `refs/SNNModels.jl/src/connections/sparse_plasticity/CaRule.jl`.
/// Field units (in normalised SI):
///   A_pre : mV^-2  (LTP rate; bump on pre-spike enters the pre trace)
///   A_post: mV^-1   (LTD rate; bump on post-spike enters the post trace)
///   tau_pre, tau_post: ms   (trace time constants)
///   w_max, w_min: pF        (weight clamp bounds)
/// Default values match Julia's `@snn_kw struct CaPlasticityParameter`.
pub(all) struct CaPlasticityParameter {
  a_pre : Float      // = 10e-2pA / (mV * mV) = 0.1
  a_post : Float     // = 10e-2pA / mV        = 0.1
  tau_pre : Float    // = 20ms
  tau_post : Float   // = 20ms
  w_max : Float      // = 30.0pF
  w_min : Float      // = 0.0pF
}

///| Default CaPlasticityParameter (matches Julia).
pub fn CaPlasticityParameter::new() -> CaPlasticityParameter {
  { a_pre: 0.1F, a_post: 0.1F, tau_pre: 20.0F, tau_post: 20.0F, w_max: 30.0F, w_min: 0.0F }
}

///| Custom CaPlasticityParameter (matches Julia's STDP kwargs).
pub fn CaPlasticityParameter::custom(
  a_pre? : Float = 0.1F,
  a_post? : Float = 0.1F,
  tau_pre? : Float = 20.0F,
  tau_post? : Float = 20.0F,
  w_max? : Float = 30.0F,
  w_min? : Float = 0.0F,
) -> CaPlasticityParameter {
  { a_pre: a_pre, a_post: a_post, tau_pre: tau_pre, tau_post: tau_post,
    w_max: w_max, w_min: w_min }
}

///| Trace state for CaPlasticityParameter.
pub(all) struct CaPlasticityVariables {
  n_pre : Int
  n_post : Int
  tpre : Array[Float]   // pre-synaptic spike trace (length Npre)
  tpost : Array[Float]  // post-synaptic spike trace (length Npost)
  active : Array[Bool]  // active flag (set by set_ltp_active)
}

///| Allocate CaPlasticityVariables with zero-initialised traces.
pub fn CaPlasticityVariables::new(n_pre : Int, n_post : Int) -> CaPlasticityVariables {
  { n_pre: n_pre, n_post: n_post,
    tpre: Array::make(n_pre, 0.0F),
    tpost: Array::make(n_post, 0.0F),
    active: [true] }
}

///| Bundle a CaPlasticityParameter connection with trace state. Pattern
/// matches `STDPEntry` (Gerstner) — mut `param` enables runtime swap.
pub struct CaPlasticityEntry {
  conn_index : Int
  n_pre : Int
  n_post : Int
  mut param : CaPlasticityParameter
  vars : CaPlasticityVariables
  t_now : Array[Float]
}

///| Constructor.
pub fn CaPlasticityEntry::new(
  conn_index : Int,
  n_pre : Int,
  n_post : Int,
  param? : CaPlasticityParameter = CaPlasticityParameter::new(),
) -> CaPlasticityEntry {
  {
    conn_index: conn_index,
    n_pre: n_pre,
    n_post: n_post,
    param: param,
    vars: CaPlasticityVariables::new(n_pre, n_post),
    t_now: [0.0F],
  }
}

///| Runtime swap of CaPlasticityParameter. Preserves trace state.
pub fn CaPlasticityEntry::change_plasticity(
  e : CaPlasticityEntry,
  new_param : CaPlasticityParameter,
) -> Unit {
  e.param = new_param
}

///| Toggle the active flag (mirrors Julia's `set_LTP!(s, active)`).
pub fn CaPlasticityEntry::set_ltp_active(
  e : CaPlasticityEntry,
  active : Bool,
) -> Unit {
  if e.vars.active.length() > 0 {
    e.vars.active[0] = active
  }
}

///| One step of CaPlasticityParameter (trace-based pair-spike STDP).
///
/// Mirrors Julia's CaRule.jl `plasticity!` for `STDPParameter`:
///   1. Weight update on pre-fire:
///        W[s] += tpost[i]   (for s = (j -> i) where fireJ[j])
///   2. Weight update on post-fire:
///        W[s] += tpre[j]    (for s = (j -> i) where fireI[i])
///   3. Trace decay:
///        tpre[j]  += dt * (-tpre[j])  / tau_pre
///        tpost[i] += dt * (-tpost[i]) / tau_post
///   4. Spike bumps:
///        fireJ[j]: tpre[j]  += A_pre
///        fireI[i]: tpost[i] += A_post
///   5. Clamp weights to [Wmin, Wmax].
///
/// Note: Julia applies weight updates BEFORE the trace updates (so the
/// update uses the previous step's trace values, which is the same as
/// an exponential-decay integrator discretised at the start of the
/// interval). We mirror that order exactly for bit-exact reproducibility
/// against Julia's `@inbounds @fastmath` update sequence.
///
/// CSR layout (same as stdp_step): rowptr[j]..rowptr[j+1] are the
/// non-zero positions for row j; colptr[s] = i (post-neuron).
pub fn ca_plasticity_step(
  w : Array[Float],
  pre_fire : Array[Bool],
  post_fire : Array[Bool],
  colptr : Array[Int],
  rowptr : Array[Int],
  vars : CaPlasticityVariables,
  param : CaPlasticityParameter,
  t_now : Float,
  dt : Float,
) -> Unit {
  // Active gate (skip if set_ltp_active(false)).
  if vars.active.length() > 0 && !vars.active[0] { return }
  let _ = t_now
  let n_pre = pre_fire.length()
  let n_post = post_fire.length()
  let inv_tau_pre : Float = 1.0F / param.tau_pre
  let inv_tau_post : Float = 1.0F / param.tau_post
  // 1. Pre-fire weight update: W[s] += tpost[colptr[s]].
  let mut j = 0
  while j < n_pre {
    if pre_fire[j] {
      let start = rowptr[j]
      let end_ = rowptr[j + 1]
      let mut s = start
      while s < end_ {
        let i = colptr[s]
        w[s] = w[s] + vars.tpost[i]
        s = s + 1
      }
    }
    j = j + 1
  }
  // 2. Post-fire weight update: W[s] += tpre[j].
  let mut k = 0
  while k < n_post {
    if post_fire[k] {
      // Walk all rows; for each row, check if any of its connections
      // targets post-neuron k. (Cheaper alternative: pre-build
      // row->col reverse index. We do it linearly for simplicity.)
      let mut j2 = 0
      while j2 < n_pre {
        let start = rowptr[j2]
        let end_ = rowptr[j2 + 1]
        let mut s = start
        while s < end_ {
          if colptr[s] == k {
            w[s] = w[s] + vars.tpre[j2]
          }
          s = s + 1
        }
        j2 = j2 + 1
      }
    }
    k = k + 1
  }
  // 3. Trace decay.
  let mut jj = 0
  while jj < n_pre {
    vars.tpre[jj] = vars.tpre[jj] + dt * (-vars.tpre[jj]) * inv_tau_pre
    jj = jj + 1
  }
  let mut ii = 0
  while ii < n_post {
    vars.tpost[ii] = vars.tpost[ii] + dt * (-vars.tpost[ii]) * inv_tau_post
    ii = ii + 1
  }
  // 4. Spike bumps.
  let mut jj2 = 0
  while jj2 < n_pre {
    if pre_fire[jj2] {
      vars.tpre[jj2] = vars.tpre[jj2] + param.a_pre
    }
    jj2 = jj2 + 1
  }
  let mut ii2 = 0
  while ii2 < n_post {
    if post_fire[ii2] {
      vars.tpost[ii2] = vars.tpost[ii2] + param.a_post
    }
    ii2 = ii2 + 1
  }
  // 5. Clamp weights.
  let mut s2 = 0
  while s2 < w.length() {
    if w[s2] < param.w_min { w[s2] = param.w_min }
    if w[s2] > param.w_max { w[s2] = param.w_max }
    s2 = s2 + 1
  }
}
///| TripletRule (Pfister 2006) — three-factor STDP with A2/A3/A3̂/A3̄
/// amplitudes. Port of `refs/SNNModels.jl/src/connections/sparse_plasticity/dump.jl::TripletRule`.
/// Field names mapped to ASCII (Julia uses superscripts/subscripts).
///   a2_ltp = A⁺₂  — post-after-pre pair LTP rate
///   a3_ltp = A⁺₃  — triplet LTP rate
///   a2_ltd = A⁻₂  — pre-after-post pair LTD rate
///   a3_ltd = A⁻₃  — triplet LTD rate
///   tau_x  = τˣ   — pre trace time constant
///   tau_y  = τʸ   — post trace time constant
///   tau_p  = τ⁺   — triplet potentiation time constant
///   tau_m  = τ⁻   — triplet depression time constant
pub(all) struct TripletRule {
  a2_ltp : Float
  a3_ltp : Float
  a2_ltd : Float
  a3_ltd : Float
  tau_x : Float
  tau_y : Float
  tau_p : Float
  tau_m : Float
}

///| Defaults from `pfister_visualcortex(true, true)` (all-to-all, full
/// triplet pair and triplet terms).
pub fn TripletRule::pfister_alltoall_full() -> TripletRule {
  { a2_ltp: 5.0e-10F, a3_ltp: 6.2e-3F, a2_ltd: 7.0e-3F, a3_ltd: 2.3e-4F,
    tau_x: 101.0F, tau_y: 125.0F, tau_p: 16.8F, tau_m: 33.7F }
}

///| NLTAH (non-linear triplet additive Hebbian) — simple three-factor
/// STDP variant. Port of `dump.jl::NLTAH`.
pub(all) struct NLTAH {
  tau : Float   // ms
  lambda_ : Float  // λ (reserved; ASCII fallback)
  mu : Float
}

///| Clopath 2010 voltage-based STDP — port of `dump.jl::STDP`.
/// Field names mapped to ASCII. Defaults from `lkd_stdp = STDP(...)`
/// literal at the bottom of dump.jl.
pub(all) struct ClopathSTDP {
  a_ltd : Float      // a⁻ — LTD strength (pF/mV)
  a_ltp : Float      // a⁺ — LTP strength (pF/mV)
  theta_ltd : Float  // θ⁻ — LTD voltage threshold
  theta_ltp : Float  // θ⁺ — LTP voltage threshold
  tau_s : Float     // homeostatic scaling timescale
  tau_u : Float     // timescale for u
  tau_v : Float     // timescale for v
  tau_x : Float     // timescale for x
  tau_1 : Float
  eps : Float       // ϵ — filter for delayed membrane potential
  w_min : Float     // j⁻
  w_max : Float     // j⁺
  inv_tau_u : Float  // τu⁻
  inv_tau_v : Float  // τv⁻
  inv_tau_x : Float  // τx⁻
  inv_tau_1 : Float  // τ1⁻
}

///| Defaults matching `lkd_stdp = STDP(a⁻=8e-5, a⁺=14e-5, θ⁻=-70, θ⁺=-49,
/// τu=10, τv=7, τx=15, τ1=5, j⁻=1.78, j⁺=21.0)` literal.
pub fn ClopathSTDP::lkd() -> ClopathSTDP {
  let a_ltd = 8.0e-5F
  let a_ltp = 14.0e-5F
  let tau_u = 10.0F
  let tau_v = 7.0F
  let tau_x = 15.0F
  let tau_1 = 5.0F
  { a_ltd: a_ltd, a_ltp: a_ltp, theta_ltd: -70.0F, theta_ltp: -49.0F,
    tau_s: 20.0F, tau_u: tau_u, tau_v: tau_v, tau_x: tau_x, tau_1: tau_1,
    eps: 1.0F, w_min: 1.78F, w_max: 21.0F,
    inv_tau_u: 1.0F / tau_u, inv_tau_v: 1.0F / tau_v,
    inv_tau_x: 1.0F / tau_x, inv_tau_1: 1.0F / tau_1 }
}

///| ISTDP (Vogels 2011 inhibitory STDP) — port of `dump.jl::ISTDP`.
pub(all) struct ISTDP {
  eta : Float       // η — learning rate
  r0 : Float        // r0 — target rate
  vd : Float        // vd — dendritic voltage threshold
  tau_d : Double    // τd — dendritic potential decay (Double in Julia)
  tau_y : Float     // τy — inhibitory rate trace decay
  alpha : Float     // α — rate trace threshold
  w_min : Float     // j⁻
  w_max : Float     // j⁺
}

///| Defaults from `ISTDP` struct (η=0.2, r0=0.01, vd=-70, τd=5, τy=20,
/// α=2*r0*τy=0.4, j⁻=2.78, j⁺=243).
pub fn ISTDP::new() -> ISTDP {
  let r0 = 0.01F
  let tau_y = 20.0F
  { eta: 0.2F, r0: r0, vd: -70.0F, tau_d: 5.0, tau_y: tau_y,
    alpha: 2.0F * r0 * tau_y, w_min: 2.78F, w_max: 243.0F }
}

///| Convenience: `vogels_istdp()` factory — returns ISTDP configured
/// for inhibitory synapse homeostatic plasticity.
pub fn ISTDP::vogels() -> ISTDP {
  // Original Julia vogels_istdp builds an ISTDP-like struct with
  // tauy=20, eta=1.0, r0=0.005, alpha=2*r0*tauy, jeimin=48.7, jeimax=243.
  // We follow that pattern by overriding the defaults.
  let r0 = 0.005F
  let tau_y = 20.0F
  { eta: 1.0F, r0: r0, vd: -70.0F, tau_d: 5.0, tau_y: tau_y,
    alpha: 2.0F * r0 * tau_y, w_min: 48.7F, w_max: 243.0F }
}