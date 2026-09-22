// HetRec — heterogeneous-timescale non-recurrent neuron population.
// Bit-exact port of SNNModels.jl/src/populations/hetrec.jl.
//
// A HetRec population is a set of N neurons, each with Nd dendritic
// compartments that filter input on heterogeneous timescales. Each
// soma reads from N*Nd dendritic compartments via a sparse mapping
// matrix M (soma i → dendrite (j-1)*N + k, with weight 1 if k==i
// or weight 1 with probability `overlap` otherwise). The soma then
// fires stochastically based on its baseline rate r[i] and the
// deviation of v_s[i] from a slow adaptation trace.
//
// Float32 contract: every arithmetic uses `Float` (Float32). The
// firing criterion uses libm `expf` via the formula
// `r * sigmoid(steepness * (v_s - trace)) * dt`.
//
// Storage layout (matches Julia):
//   v_d : Array[Float] size N*Nd   — dendritic voltages (filtered input)
//   v_s : Array[Float] size N      — somatic voltages
//   is  : Array[Float] size N*Nd   — synaptic input currents (set externally)
//   r   : Array[Float] size N      — per-neuron baseline firing rate
//   τd  : Array[Float] size N*Nd   — per-compartment dendritic time constants
//   fire   : Array[Bool]  size N   — spike indicators
//   tabs   : Array[Int]   size N   — refractory counters
//   trace  : Array[Float] size N   — slow adaptation trace
//   randcache : Array[Float] size N — random cache for stochastic firing
//   colptr : Array[Int]   size N+1 — column pointer for CSC of M'
//   I      : Array[Int]   size nnz — dendrite indices per synapse
//   W      : Array[Float] size nnz — synapse weights (= 1 in default M)
//
// We use a CSC-like representation directly (colptr + I + W) rather
// than CSR to match Julia's `dsparse(w)` output that the integrate!
// loop expects: `for s in colptr[i]:(colptr[i+1]-1)` iterates over
// the dendrites that soma i reads from.

///|
/// HetRecParameter — parameters for the HetRec layer.
pub(all) struct HetRecParameter {
  nd : Int              // dendritic compartments per neuron
  overlap : Float       // dendritic overlap across neurons [0, 1]
  tau_d_low : Float     // lower bound of τd distribution (ms)
  tau_d_high : Float    // upper bound of τd distribution (ms)
  rate_low : Float      // lower bound of r distribution (Hz)
  rate_high : Float     // upper bound of r distribution (Hz)
  tau_abs : Float       // absolute refractory period (ms)
  steepness : Float     // soma firing nonlinearity steepness
  tau_m : Float         // soma integration time constant (ms)
  tau_rate : Float      // adaptation trace time constant (ms)
}

///|
/// HetRecParameter with Julia's defaults.
pub fn HetRecParameter::new() -> HetRecParameter {
  // Julia: HetRecParameter() defaults:
  //   Nd=2, overlap=0.5, τd=Uniform(10, 100), rate=Uniform(0, 1),
  //   τabs=5ms, steepness=1, τm=20ms, τrate=100ms
  {
    nd: 2,
    overlap: 0.5F,
    tau_d_low: 10.0F,
    tau_d_high: 100.0F,
    rate_low: 0.0F,
    rate_high: 1.0F,
    tau_abs: 5.0F,
    steepness: 1.0F,
    tau_m: 20.0F,
    tau_rate: 100.0F,
  }
}

///|
/// Custom HetRecParameter with non-default Nd, τm, and overlap.
/// Mirrors Julia's `HetRecParameter(Nd = 4, τm = 30ms, overlap = 0.2)`.
/// Other fields keep their defaults.
pub fn HetRecParameter::custom(
  nd : Int,
  tau_m : Float,
  overlap : Float,
) -> HetRecParameter {
  {
    nd,
    overlap,
    tau_d_low: 10.0F,
    tau_d_high: 100.0F,
    rate_low: 0.0F,
    rate_high: 1.0F,
    tau_abs: 5.0F,
    steepness: 1.0F,
    tau_m,
    tau_rate: 100.0F,
  }
}

///|
/// HetRec — heterogeneous-timescale non-recurrent population.
pub struct HetRec {
  param : HetRecParameter
  n : Int                          // number of neurons
  v_d : Array[Float]               // dendritic voltages (size N*Nd)
  v_s : Array[Float]               // somatic voltages (size N)
  is_ : Array[Float]               // synaptic input currents (size N*Nd)
  r : Array[Float]                 // baseline firing rates (size N)
  tau_d : Array[Float]             // dendritic time constants (size N*Nd)
  fire : Array[Bool]               // spike indicators (size N)
  tabs : Array[Int]                // refractory counters (size N)
  trace : Array[Float]             // adaptation trace (size N)
  randcache : Array[Float]         // random cache (size N)
  // Sparse mapping M' (CSC layout: somas = columns, dendrites = rows).
  colptr : Array[Int]              // column pointer (size N+1)
  i_syn : Array[Int]               // dendrite index per synapse (size nnz)
  w_syn : Array[Float]             // weight per synapse (size nnz; binary 0/1)
}

///|
/// Construct a HetRec population. Initialises:
///   - v_d, v_s = 0
///   - is_ = 0
///   - r = Uniform(rate_low, rate_high) per neuron (in Hz; matches
///     Julia's `Uniform(0, 1)` so r values are in (0, 1)).
///   - τd = Uniform(tau_d_low, tau_d_high) per compartment (in ms).
///   - fire = false, tabs = 0, trace = 0
///   - randcache = uniform (will be re-filled each step)
///   - colptr / i_syn / w_syn: sparse M where
///       M[soma i, dendrite (j-1)*N + k] = 1 if k==i (own dendrite)
///                                  or 1 with probability `overlap`
///       M is then transposed to M' (dendrites × somas) and stored
///       as CSC with colptr = soma indices, i_syn = dendrite indices.
pub fn HetRec::new(n : Int, param : HetRecParameter, rng : Xoshiro) -> HetRec {
  let nd = param.nd
  let total_d = n * nd
  let v_d : Array[Float] = Array::make(total_d, 0.0F)
  let v_s : Array[Float] = Array::make(n, 0.0F)
  let is_ : Array[Float] = Array::make(total_d, 0.0F)
  // Sample baseline firing rates from Uniform(rate_low, rate_high).
  let r : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let u = next_f32(rng)
    r[i] = param.rate_low + u * (param.rate_high - param.rate_low)
  }
  // Sample dendritic time constants from Uniform(tau_d_low, tau_d_high).
  let tau_d : Array[Float] = Array::make(total_d, 0.0F)
  for i in 0..<total_d {
    let u = next_f32(rng)
    tau_d[i] = param.tau_d_low + u * (param.tau_d_high - param.tau_d_low)
  }
  let fire : Array[Bool] = Array::make(n, false)
  let tabs : Array[Int] = Array::make(n, 0)
  let trace : Array[Float] = Array::make(n, 0.0F)
  let randcache : Array[Float] = Array::make(n, 0.0F)

  // Build sparse M' (CSC: columns = somas, rows = dendrites).
  // For each soma i, collect dendrite indices (j-1)*N + k where M[i, (j-1)*N + k] = 1.
  let colptr : Array[Int] = Array::make(n + 1, 0)
  let i_syn : Array[Int] = []
  let w_syn : Array[Float] = []
  for i in 0..<n {
    colptr[i] = i_syn.length()
    for j in 0..<nd {
      // Own dendrite: always connected.
      let d_idx = j * n + i
      i_syn.push(d_idx)
      w_syn.push(1.0F)
      // Cross-neuron overlap: each other soma k gets connection
      // with probability `overlap`.
      for k in 0..<n {
        if k == i {
          continue
        }
        let u = next_f32(rng)
        if u < param.overlap {
          let d_idx2 = j * n + k
          i_syn.push(d_idx2)
          w_syn.push(1.0F)
        }
      }
    }
  }
  colptr[n] = i_syn.length()

  {
    param, n, v_d, v_s, is_, r, tau_d,
    fire, tabs, trace, randcache,
    colptr, i_syn, w_syn,
  }
}

///|
/// Fill the random cache from the Xoshiro stream (Float32 path).
/// Mirrors Julia's `rand!(randcache)` which draws Float32 values.
pub fn hetrec_refresh_random(p : HetRec, rng : Xoshiro) -> Unit {
  let n = p.n
  for i in 0..<n {
    p.randcache[i] = next_f32(rng)
  }
}

///|
/// Integrate one Euler step for HetRec. Bit-exact port of Julia's
/// `integrate!(p::HetRec, param, dt)`:
///
///   1. dendritic Euler step: v_d += dt * (-v_d - is) / τd
///   2. soma input from dendrites: for each synapse s of soma i:
///        v_s[i] += (W[s] * v_d[I[s]] - v_s[i]) * dt / τm
///   3. decrement tabs; if tabs > 0, skip firing logic
///   4. update trace: trace += dt * (-trace / τrate)
///      if not refractory: trace += (v_s[i] - trace) / τrate
///   5. stochastic fire: if randcache[i] < r[i] * sigmoid(steepness * (v_s - trace)) * dt
///      then fire[i] = true; tabs[i] = τabs/dt; trace[i] += 1
///
/// The caller is expected to:
///   - inject synaptic currents into `p.is_` before calling step
///     (e.g., via SpikingSynapse forward to the `:is` target)
///   - call hetrec_refresh_random(p, rng) once per step to refresh
///     `p.randcache` (matches Julia's `rand!(randcache)`).
pub fn step_hetrec(p : HetRec, dt : Float) -> Unit {
  let n = p.n
  let nd = p.param.nd
  let total_d = n * nd
  let steepness = p.param.steepness
  let tau_m = p.param.tau_m
  let tau_rate = p.param.tau_rate
  let tau_abs = p.param.tau_abs
  let tabs_steps : Int = (tau_abs / dt).to_int()

  // 1. Dendritic Euler step.
  for i in 0..<total_d {
    let tau_di = p.tau_d[i]
    p.v_d[i] = p.v_d[i] + dt * (-p.v_d[i] - p.is_[i]) / tau_di
  }

  // 2. Soma input from dendrites (Euler integration).
  for i in 0..<n {
    let start = p.colptr[i]
    let end = p.colptr[i + 1]
    let dt_over_tau_m = dt / tau_m
    let mut s = start
    while s < end {
      let dend_idx = p.i_syn[s]
      let w = p.w_syn[s]
      p.v_s[i] = p.v_s[i] + (w * p.v_d[dend_idx] - p.v_s[i]) * dt_over_tau_m
      s = s + 1
    }
  }

  // 3-5. Refractory + trace + stochastic fire.
  for i in 0..<n {
    p.tabs[i] = p.tabs[i] - 1
    p.fire[i] = false
    // trace leak
    p.trace[i] = p.trace[i] + dt * (-p.trace[i] / tau_rate)
    if p.tabs[i] > 0 {
      continue
    }
    // trace catch-up to v_s
    p.trace[i] = p.trace[i] + (p.v_s[i] - p.trace[i]) / tau_rate
    // Stochastic fire: sigmoid(steepness * (v_s - trace)) = 1 / (1 + exp(-steepness * (v_s - trace)))
    let sigmoid_arg = -steepness * (p.v_s[i] - p.trace[i])
    let rate = if sigmoid_arg > 88.0F {
      // Saturating: exp(-large) ≈ 0 → sigmoid ≈ 1
      p.r[i] * dt
    } else if sigmoid_arg < -88.0F {
      // Saturating: exp(large) → Inf → sigmoid ≈ 0
      0.0F
    } else {
      p.r[i] * dt / (1.0F + expf(sigmoid_arg))
    }
    if p.randcache[i] < rate {
      p.fire[i] = true
      p.tabs[i] = tabs_steps
      p.trace[i] = p.trace[i] + 1.0F
    }
  }
}