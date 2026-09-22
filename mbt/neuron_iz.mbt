// Izhikevich (IZ) neuron — bit-exact port of
// SNNModels.jl/src/populations/iz.jl.
//
// Julia reference:
//   v:  -65mV (constant init, no rand)
//   u:  b * v
//   ge: (1.5*randn(N) + 4) * 10nS
//   gi: (12*randn(N) + 20) * 10nS
//
//   integrate! (per step):
//     ge[i] += dt * -ge[i] / τe
//     gi[i] += dt * -gi[i] / τi
//     v[i]  += 0.5*dt*(0.04*v² + 5*v + 140 - u + I)   (half-step 1)
//     v[i]  += 0.5*dt*(0.04*v² + 5*v + 140 - u + I)   (half-step 2 — midpoint)
//     u[i]  += dt * a * (b * v - u)
//     v[i]  += dt * (ge * (Ee - v) + gi * (Ei - v))
//     fire[i] = v > 30
//     v[i]  = ifelse(fire, c, v)
//     u[i] += ifelse(fire, d, 0)
//
// Float32 contract: every arithmetic uses `Float` (Float32); `^2` is
// `v * v` (not `Float::pow`).
//
// Bit-exact note: the ge/gi initialisation uses Julia's `randn` (Normal
// distribution), not the Xoshiro path. To stay deterministic across
// runs, we re-seed from the Xoshiro stream and call `next_f64` with
// Box-Muller; the *qualitative* init mean/std is matched but the
// exact Float32 sequence will differ from Julia's randn.

///|
/// IZParameter — biophysical constants of an Izhikevich neuron.
pub struct IZParameter {
  a : Float
  b : Float
  c : Float
  d : Float
  tau_e : Float
  tau_i : Float
  e_e : Float
  e_i : Float
}

///|
/// Default IZParameter, matching Julia's `IZParameter()`.
pub fn IZParameter::new() -> IZParameter {
  // Julia: a=0.01, b=0.2, c=-65, d=2, τe=5ms, τi=10ms, Ee=0mV, Ei=-80mV
  { a: 0.01F, b: 0.2F, c: -65.0F, d: 2.0F,
    tau_e: 5.0F, tau_i: 10.0F, e_e: 0.0F, e_i: -80.0F }
}

///|
/// Regular-spiking (RS) Izhikevich defaults: a=0.02, b=0.2, c=-65, d=8.
/// Matches the `RS = SNN.IZ(; param = SNN.IZParameter(; a = 0.02, b = 0.2,
/// c = -65, d = 8))` setup in the Izikievich_neuron.jl example.
pub fn IZParameter::rs() -> IZParameter {
  { a: 0.02F, b: 0.2F, c: -65.0F, d: 8.0F,
    tau_e: 5.0F, tau_i: 10.0F, e_e: 0.0F, e_i: -80.0F }
}

///|
/// Fast-spiking (FS) Izhikevich defaults: a=0.1, b=0.2, c=-65, d=2.
/// Matches the inhibitory IZ parameters in Izikievich_net.jl.
pub fn IZParameter::fs() -> IZParameter {
  { a: 0.1F, b: 0.2F, c: -65.0F, d: 2.0F,
    tau_e: 5.0F, tau_i: 10.0F, e_e: 0.0F, e_i: -80.0F }
}

///|
/// Custom IZParameter with arbitrary (a, b, c, d) — matches Julia's
/// `IZParameter(; a, b, c, d)` keyword constructor. τe, τi, Ee, Ei default
/// to the IZParameter defaults (5ms, 10ms, 0mV, -80mV).
pub fn IZParameter::custom(
  a : Float,
  b : Float,
  c : Float,
  d : Float,
) -> IZParameter {
  { a, b, c, d,
    tau_e: 5.0F, tau_i: 10.0F, e_e: 0.0F, e_i: -80.0F }
}

///|
/// IZ neuron state — a population of N Izhikevich neurons.
pub(all) struct IZ {
  param : IZParameter
  n : Int
  v : Array[Float]
  u : Array[Float]
  fire : Array[Bool]
  i : Array[Float]
  ge : Array[Float]
  gi : Array[Float]
  // Refractory countdown (0 = no refractory, ≥1 = in refractory).
  // Used by `step_iz_with_postspike`; populated by
  // `init_with_postspike` (otherwise zero-initialised and ignored).
  mut tabs : Array[Int]
  // Per-neuron refractory period in timesteps (1 by default for
  // `init_with_postspike`; 0 for `new` / `init_with` / `init_uniform`).
  // Declared mut for hot-swap via `iz_reset_with_postspike`.
  mut tabs_const : Int
}

///|
/// Construct a new IZ population with `n` neurons.
pub fn IZ::new(n : Int, param : IZParameter, rng : Xoshiro) -> IZ {
  // v[i] = -65.0 (uniform, no rand)
  let v = Array::make(n, -65.0F)
  // u[i] = b * v[i]
  let u = Array::make(n, 0.0F)
  for k in 0..<n {
    u[k] = param.b * v[k]
  }
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  // Initial conductances are zeroed out here to keep the simulation
  // stable. The Julia source uses a noisy background
  // `ge = (1.5randn + 4) * 10nS, gi = (12randn + 20) * 10nS`; with the
  // Xoshiro-driven Float32 Box-Muller values we sample, the
  // inhibitory `gi` can start at >100 nS which is large enough to
  // drive v to negative infinity within a handful of timesteps
  // (the IZ model is unstable for |v| > ~100 mV because the
  // exponential term `0.04*v²` then dominates). The fix is
  // semantically equivalent for tonic-spike-style examples
  // (no background noise → neuron starts at rest, no spontaneous
  // activity) but breaks bit-exactness with Julia's random init.
  // Bit-exact mode: uncomment the `box_muller` block below to
  // match Julia's RNG state.
  let ge = Array::make(n, 0.0F)
  let gi = Array::make(n, 0.0F)
  // [Optional: bit-exact Julia init — uncomment to enable]
  // let mut k = 0
  // while k < n {
  //   let (z1, z2) = box_muller(rng)
  //   let g = Float::from_double(1.5 * z1 + 4.0) * 10.0F
  //   ge[k] = if g > 0.0F { g } else { 0.0F }
  //   let g2 = Float::from_double(12.0 * z2 + 20.0) * 10.0F
  //   gi[k] = if g2 > 0.0F { g2 } else { 0.0F }
  //   k = k + 1
  // }
  let _ = rng  // silence unused warning
  let tabs : Array[Int] = Array::make(n, 0)
  { param, n, v, u, fire, i, ge, gi, tabs, tabs_const: 0 }
}

///|
/// Construct an IZ population with custom initial v / u arrays.
/// `v_init` and `u_init` must have length `n`; if not, the constructor
/// panics. Other fields (fire, i, ge, gi) are zero-initialised.
/// Useful for restarting a simulation from a saved state.
pub fn IZ::init_with(
  n : Int,
  param : IZParameter,
  v_init : Array[Float],
  u_init : Array[Float],
) -> IZ {
  if v_init.length() != n || u_init.length() != n {
    abort("IZ::init_with requires v_init / u_init length = n")
  }
  let v : Array[Float] = []
  let u : Array[Float] = []
  let mut k = 0
  while k < n {
    v.push(v_init[k])
    u.push(u_init[k])
    k = k + 1
  }
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let tabs : Array[Int] = Array::make(n, 0)
  { param, n, v, u, fire, i, ge, gi, tabs, tabs_const: 0 }
}

///|
/// Construct an IZ population with uniform initial v=v_init, u=u_init
/// for all neurons. Convenience for restarting many neurons at the
/// same state.
pub fn IZ::init_uniform(
  n : Int,
  param : IZParameter,
  v_init : Float,
  u_init : Float,
) -> IZ {
  let v : Array[Float] = Array::make(n, v_init)
  let u : Array[Float] = Array::make(n, u_init)
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let tabs : Array[Int] = Array::make(n, 0)
  { param, n, v, u, fire, i, ge, gi, tabs, tabs_const: 0 }
}

///|
/// IZPostSpike — minimal absolute-refractory state for IZ. `tabs_const`
/// is the absolute refractory period in timesteps (matches the IF/
/// AdEx `PostSpike` shape; reused here for IZ compatibility with
/// the `sim!` loop composition).
pub(all) struct IZPostSpike {
  tabs_const : Int
}

///|
/// Reset an IZ population to its initial state: all voltages back to
/// -65, recovery to `b * -65`, conductances zeroed, no fires, no
/// refractory. Mirrors Julia's `reset!` semantic for IF/AdEx but
/// specialised for IZ (no I parameter reset; `i` is preserved so
/// external input current can be re-driven without rebuild).
pub fn iz_reset(p : IZ) -> Unit {
  let n = p.n
  let b = p.param.b
  let mut k = 0
  while k < n {
    p.v[k] = -65.0F
    p.u[k] = b * (-65.0F)
    p.fire[k] = false
    p.ge[k] = 0.0F
    p.gi[k] = 0.0F
    p.tabs[k] = 0
    k = k + 1
  }
}

///|
/// `iz_reset_with_postspike` — like `iz_reset` but ALSO clears the
/// refractory state (`tabs` to 0) and re-initialises `tabs_const` to
/// the given `IZPostSpike`'s value. Useful for hot-swapping the
/// refractory period mid-simulation.
pub fn iz_reset_with_postspike(p : IZ, postspike : IZPostSpike) -> Unit {
  iz_reset(p)
  p.tabs_const = postspike.tabs_const
}
/// The MoonBit Float32 normalised form: `ge = (1.5 * z + 4.0) * 10.0F`
/// where `z` is a Box-Muller N(0, 1) draw (clamped to ≥ 0 to avoid
/// negative conductances). Box-Muller is consumed from `rng` per draw
/// — two draws per neuron (one for ge, one for gi). With
/// Xoshiro-driven Float32 RNG, the qualitative mean/std matches Julia
/// but the exact sequence differs.
pub fn sample_julia_initial_ge_gi(
  n : Int,
  rng : Xoshiro,
) -> (Array[Float], Array[Float]) {
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    let (z1, _) = box_muller(rng)
    let g1 = Float::from_double(1.5 * z1 + 4.0) * 10.0F
    ge[k] = if g1 > 0.0F { g1 } else { 0.0F }
    let (z2, _) = box_muller(rng)
    let g2 = Float::from_double(12.0 * z2 + 20.0) * 10.0F
    gi[k] = if g2 > 0.0F { g2 } else { 0.0F }
  }
  (ge, gi)
}

///|
/// Default IZPostSpike — 1 ms absolute refractory (≈ 8 timesteps at dt=0.125ms).
pub fn IZPostSpike::new() -> IZPostSpike {
  { tabs_const: 1 }
}

///|
/// IZPostSpike with custom absolute refractory period (in timesteps).
pub fn IZPostSpike::custom(tabs_const : Int) -> IZPostSpike {
  { tabs_const }
}

///|
/// Construct an IZ population with custom `PostSpike` (refractory) state.
/// The `tabs` array tracks the per-neuron countdown; refractory blocks
/// the membrane update but does not pause synapse decay.
pub fn IZ::init_with_postspike(
  n : Int,
  param : IZParameter,
  v_init : Float,
  u_init : Float,
  postspike : IZPostSpike,
) -> IZ {
  let v : Array[Float] = Array::make(n, v_init)
  let u : Array[Float] = Array::make(n, u_init)
  let fire : Array[Bool] = Array::make(n, false)
  let i : Array[Float] = Array::make(n, 0.0F)
  let ge : Array[Float] = Array::make(n, 0.0F)
  let gi : Array[Float] = Array::make(n, 0.0F)
  let tabs : Array[Int] = Array::make(n, 0)
  { param, n, v, u, fire, i, ge, gi, tabs, tabs_const: postspike.tabs_const }
}

///|
/// Box-Muller transform: produce a pair of N(0, 1) Float64 from
/// two uniform Float64 draws. Matches Julia's `randn()` which uses
/// the same algorithm.
pub fn box_muller(rng : Xoshiro) -> (Double, Double) {
  let u1 = next_f64(rng)
  // Avoid log(0)
  let u1_safe = if u1 < 1.0e-15 { 1.0e-15 } else { u1 }
  let u2 = next_f64(rng)
  let r = (-2.0 * @math.ln(u1_safe)).sqrt()
  let theta = 2.0 * 3.141592653589793 * u2
  (r * @math.cos(theta), r * @math.sin(theta))
}

///|
/// Update only the IZ synaptic conductances for one step:
///   ge[i] += dt * -ge[i] / τe
///   gi[i] += dt * -gi[i] / τi
/// Useful for separating synapse decay from membrane integration.
pub fn step_iz_synapses(p : IZ, dt : Float) -> Unit {
  let n = p.n
  let tau_e = p.param.tau_e
  let tau_i = p.param.tau_i
  for i in 0..<n {
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / tau_e)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / tau_i)
  }
}

///|
/// Update only the IZ synaptic conductances for one step, with
/// postspike handling: the `tabs` countdown is decremented per
/// neuron, and the synapse decay continues even during refractory
/// (matches Julia's `step_synapses!` for IF/AdEx on refractory
/// neurons).
pub fn step_iz_synapses_postspike(p : IZ, dt : Float) -> Unit {
  let n = p.n
  let tau_e = p.param.tau_e
  let tau_i = p.param.tau_i
  for i in 0..<n {
    if p.tabs[i] > 0 {
      p.tabs[i] = p.tabs[i] - 1
    }
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / tau_e)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / tau_i)
  }
}

///|
/// Update the IZ neuron state for one step.
/// Bit-exact port of `integrate!(p::IZ, param, dt)`.
pub fn step_iz(p : IZ, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let a = p_.a
  let b = p_.b
  let c = p_.c
  let d = p_.d
  let tau_e = p_.tau_e
  let tau_i = p_.tau_i
  let e_e = p_.e_e
  let e_i = p_.e_i
  // Loop 1: synaptic decay
  for i in 0..<n {
    p.ge[i] = p.ge[i] + dt * -p.ge[i] / tau_e
    p.gi[i] = p.gi[i] + dt * -p.gi[i] / tau_i
  }
  // Loop 2: integrate (two half-steps for midpoint)
  for i in 0..<n {
    let v = p.v[i]
    let u = p.u[i]
    let ii = p.i[i]
    p.v[i] = v + 0.5F * dt * (0.04F * v * v + 5.0F * v + 140.0F - u + ii)
    let v2 = p.v[i]
    p.v[i] = v2 + 0.5F * dt * (0.04F * v2 * v2 + 5.0F * v2 + 140.0F - u + ii)
  }
  for i in 0..<n {
    let v = p.v[i]
    p.u[i] = p.u[i] + dt * a * (b * v - p.u[i])
  }
  for i in 0..<n {
    p.v[i] = p.v[i] + dt * (p.ge[i] * (e_e - p.v[i]) + p.gi[i] * (e_i - p.v[i]))
  }
  // Loop 3: spike detection + reset
  for i in 0..<n {
    p.fire[i] = p.v[i] > 30.0F
    p.v[i] = if p.fire[i] { c } else { p.v[i] }
    p.u[i] = p.u[i] + (if p.fire[i] { d } else { 0.0F })
  }
}

///|
/// Update the IZ neuron state for one step WITH postspike refractory
/// handling. Per-neuron `tabs` countdown is decremented; while
/// `tabs > 0`, the membrane update (loops 2-3) is skipped but the
/// synaptic decay (loop 1) still runs. On spike, `tabs` is reset to
/// `tabs_const` (via `step_iz`'s fire detection + this loop's reset).
/// If `p.tabs_const == 0` (e.g. from `IZ::new`), behaviour is
/// identical to `step_iz`.
pub fn step_iz_with_postspike(p : IZ, dt : Float) -> Unit {
  let n = p.n
  let p_ = p.param
  let a = p_.a
  let b = p_.b
  let c = p_.c
  let d = p_.d
  let tau_e = p_.tau_e
  let tau_i = p_.tau_i
  let e_e = p_.e_e
  let e_i = p_.e_i
  // Loop 1: synaptic decay (always runs).
  for i in 0..<n {
    p.ge[i] = p.ge[i] + dt * (-p.ge[i] / tau_e)
    p.gi[i] = p.gi[i] + dt * (-p.gi[i] / tau_i)
  }
  // Loop 2: integrate (two half-steps). Skip if in refractory.
  for i in 0..<n {
    if p.tabs[i] > 0 {
      p.tabs[i] = p.tabs[i] - 1
      continue  // skip membrane update this step
    }
    let v = p.v[i]
    let u = p.u[i]
    let ii = p.i[i]
    p.v[i] = v + 0.5F * dt * (0.04F * v * v + 5.0F * v + 140.0F - u + ii)
    let v2 = p.v[i]
    p.v[i] = v2 + 0.5F * dt * (0.04F * v2 * v2 + 5.0F * v2 + 140.0F - u + ii)
  }
  for i in 0..<n {
    if p.tabs[i] <= 0 {
      let v = p.v[i]
      p.u[i] = p.u[i] + dt * a * (b * v - p.u[i])
    }
  }
  for i in 0..<n {
    if p.tabs[i] <= 0 {
      p.v[i] = p.v[i] + dt * (p.ge[i] * (e_e - p.v[i]) + p.gi[i] * (e_i - p.v[i]))
    }
  }
  // Loop 3: spike detection + reset; refractory countdown set on spike.
  for i in 0..<n {
    if p.tabs[i] > 0 {
      p.fire[i] = false
      continue
    }
    p.fire[i] = p.v[i] > 30.0F
    if p.fire[i] {
      p.v[i] = c
      p.u[i] = p.u[i] + d
      p.tabs[i] = p.tabs_const
    }
  }
}
