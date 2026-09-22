// analysis_poisson_input.mbt — Poisson spike train generators.
//
// Port of `refs/SNNUtils.jl/src/analysis/protocols.jl`:
//   - `_PoissonInput(Hz_rate, interval, dt)` — generate one spike train
//     using exponential interspike intervals.
//   - `PoissonInput(Hz_rate, interval, dt, neurons)` — `neurons` × T
//     matrix of spike trains.
//
// Math (Float32 to match Julia's @snn_kw Float32 defaults):
//   λ = 1000 / Hz_rate  // mean ISI in ms
//   spike_count = round(interval / dt)
//   sample δ ∈ Exponential(λ / dt), accumulate; emit spike at each
//   integer floor of the cumulative sum until past spike_count.
//
// Bit-exact matching Julia: sample draws use the same RNG sequence.
// Float64 path for exp() (Float32 loses precision); cast back to Float
// for interval sampling and emission.

///|
/// Sample one spike train (1D Bool array) using exponential ISI.
/// `interval` is in ms, `dt` is in ms. Spike count = round(interval / dt).
pub fn poisson_input_single(
  hz_rate : Float,
  interval : Float,
  dt : Float,
  rng : Xoshiro,
) -> Array[Bool] {
  let n_spikes = (interval / dt).to_int()  // floor-style int cast
  let spikes : Array[Bool] = Array::make(n_spikes, false)
  if hz_rate <= 0.0F || n_spikes == 0 {
    return spikes
  }
  // λ = 1000 / Hz_rate  (mean ISI in ms); normalise by dt to get per-step count.
  let lambda_ms : Float = 1000.0F / hz_rate
  let lambda_dt : Float = lambda_ms / dt
  let mut t : Float = 0.0F
  while t < Float::from_int(n_spikes) {
    // Sample δ ~ Exponential(lambda_dt) using inverse-CDF:
    //   δ = -lambda_dt * ln(1 - u)  for u uniform in (0, 1).
    // Use next_f64() for accuracy.
    let u = next_f64(rng)
    let safe_u = if u <= 0.0 { 0.0001 } else { u }
    let delta_d = -lambda_dt.to_double() * @math.ln(1.0 - safe_u)
    let delta : Float = Float::from_double(delta_d)
    t = t + delta
    if t < Float::from_int(n_spikes) {
      let idx = t.to_int()  // floor-style int cast
      if idx >= 0 && idx < n_spikes {
        spikes[idx] = true
      }
    }
  }
  spikes
}

///|
/// Generate `neurons` × `n_spikes` matrix of independent Poisson spike
/// trains. Returns Array[Array[Bool]] where outer index = neuron.
/// Total time = interval ms.
pub fn poisson_input(
  hz_rate : Float,
  interval : Float,
  dt : Float,
  neurons : Int,
  rng : Xoshiro,
) -> Array[Array[Bool]] {
  let out : Array[Array[Bool]] = []
  for _ in 0..<neurons {
    out.push(poisson_input_single(hz_rate, interval, dt, rng))
  }
  out
}