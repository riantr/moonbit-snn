// analysis_protocols.mbt — small protocol helpers for SNN analysis.
//
// Port of `refs/SNNUtils.jl/src/analysis/protocols.jl`:
//   - `get_poisson_spikes(rate, dt, rng)` — Bernoulli sample: 1.0F if
//     `rate * dt > rand()`, else 0.0F. Mirrors Julia's Bernoulli
//     sampling used in spike generators.
//   - `logrange(x1, x2, n)` — n-element log10-spaced array from x1 to x2.

///|
/// Bernoulli spike sample: returns 1.0F with probability `rate * dt`,
/// otherwise 0.0F. Mirrors Julia `get_poisson_spikes(; rate, dt)`.
/// `rate` is in kHz; `dt` is in ms (so rate*dt is dimensionless).
/// For a kHz rate × ms dt, rate*dt ∈ [0, 1].
pub fn get_poisson_spikes(rate : Float, dt : Float, rng : Xoshiro) -> Float {
  let lambda = rate * dt
  // next_f64(rng) is uniform in [0, 1). If < lambda, fire.
  if next_f64(rng).to_float() < lambda {
    1.0F
  } else {
    0.0F
  }
}

///|
/// Log-spaced array from `x1` to `x2` with `n` entries (inclusive
/// endpoints). `logrange(x1, x2, n)[k] = 10^(log10(x1) + k * (log10(x2) - log10(x1)) / (n-1))`.
///
/// Uses Float64 for log/exp to match Julia's `logspace`-style precision,
/// then casts back to Float32 for the result.
pub fn logrange(x1 : Float, x2 : Float, n : Int) -> Array[Float] {
  let result : Array[Float] = Array::make(n, 0.0F)
  if n <= 0 {
    return result
  }
  if n == 1 {
    result[0] = x1
    return result
  }
  let x1_d = Float::to_double(x1)
  let x2_d = Float::to_double(x2)
  let log_x1 = @math.ln(x1_d) / @math.ln(10.0)  // log10(x1)
  let log_x2 = @math.ln(x2_d) / @math.ln(10.0)  // log10(x2)
  let n_d = Float::from_int(n - 1).to_double()
  let n_d_inv = 1.0 / n_d
  for i in 0..<n {
    let t = Float::from_int(i).to_double() * n_d_inv
    let log_val = log_x1 + t * (log_x2 - log_x1)
    let val = exp_pow10(log_val)
    result[i] = Float::from_double(val)
  }
  result
}

///|
/// Helper: 10^x using Double path (since Float::pow doesn't exist).
fn exp_pow10(x : Double) -> Double {
  @math.exp(x * @math.ln(10.0))
}