// util.mbt — small utility helpers from `SNNModels.jl/src/utils/util.jl`.
//
// Provides:
//   - `rand_value(n, p1, p2)` — uniform random values in [min, max].
//   - `exp32(x)` / `exp64(x)` / `exp256(x)` — fast exp approximation
//     (Julia-side performance optimisation; we just provide them for
//     API parity even though we use libm expf directly).
//   - `name(pre, post, k?)` — Symbol name generator for connection labels.
//   - `str_name(pre, post, k?)` — String name generator.
//   - `f2l(s, l?)` — pad/truncate a string to exactly `l` chars.
//
// Float32 contract: every arithmetic uses Float32 (matching Julia's
// default `FT = Float32`).

// =========================================================================
// rand_value
// =========================================================================

///|
/// Generate `n` random Float32 values uniformly distributed between
/// `min(p1, p2)` and `max(p1, p2)`. Mirrors Julia's
/// `rand_value(N, p1, p2) = min + rand(N) * abs(p1-p2)`.
///
/// If `p1 == p2`, all values are exactly `p1` (degenerate range).
pub fn rand_value(n : Int, p1 : Float, p2 : Float, rng : Xoshiro) -> Array[Float] {
  let lo = if p1 < p2 { p1 } else { p2 }
  let hi = if p1 < p2 { p2 } else { p1 }
  let span = hi - lo
  let out : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    // next_f32 returns a uniform Float32 in [0, 1).
    out[i] = lo + next_f32(rng) * span
    i = i + 1
  }
  out
}

// =========================================================================
// exp32 / exp64 / exp256 — fast exp approximations
// =========================================================================
//
// Julia's exp32 / exp64 / exp256 are approximations of exp(x) using
// repeated squaring of (1 + x/N)^N. They clamp input to avoid
// underflow. We port them verbatim — even though we already use libm
// expf for our neuron updates, we expose these for API parity.

///|
/// Fast approximation of exp(x) using 32 squaring iterations.
/// Clamps x to [-10, +∞).
pub fn exp32(x : Float) -> Float {
  let mut v = if x < -10.0F { -10.0F } else { x }
  v = 1.0F + v / 32.0F
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v
}

///|
/// Fast approximation of exp(x) using 64 squaring iterations.
/// Clamps x to [-10, +∞).
pub fn exp64(x : Float) -> Float {
  let mut v = if x < -10.0F { -10.0F } else { x }
  v = 1.0F + v / 64.0F
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v
}

///|
/// Fast approximation of exp(x) using 256 squaring iterations.
/// Clamps x to [-10, +∞).
pub fn exp256(x : Float) -> Float {
  let mut v = if x < -10.0F { -10.0F } else { x }
  v = 1.0F + v / 256.0F
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v = v * v
  v
}

// =========================================================================
// name / str_name
// =========================================================================

///|
/// Generate a Symbol name for a connection between populations:
/// `:pre_to_post` or `:pre_to_post_k` if `k` is provided.
/// Mirrors Julia's `name(pre, post, k=nothing)`.
pub fn name(pre : String, post : String, k : String) -> String {
  if k.length() == 0 {
    pre + "_to_" + post
  } else {
    pre + "_to_" + post + "_" + k
  }
}

///|
/// Two-argument form: name(pre, post) → "pre_to_post".
pub fn name2(pre : String, post : String) -> String {
  pre + "_to_" + post
}

///|
/// Generate a String name for a connection between populations:
/// "pre_to_post" or "pre_to_post_k" if `k` is provided.
/// Mirrors Julia's `str_name(pre, post, k=nothing)`.
pub fn str_name(pre : String, post : String, k : String) -> String {
  if k.length() == 0 {
    pre + "_to_" + post
  } else {
    pre + "_to_" + post + "_" + k
  }
}

///|
/// One-argument form: str_name(pre, k=nothing) → "pre" or "pre_k".
/// Mirrors Julia's `str_name(pre::String, k=nothing)`.
pub fn str_name_single(pre : String, k : String) -> String {
  if k.length() == 0 {
    pre
  } else {
    pre + "_" + k
  }
}

// =========================================================================
// f2l — fixed-length string formatter
// =========================================================================

///|
/// Format `s` to exactly `l` characters by right-padding with spaces
/// (if `s.length() < l`) or truncating (if `s.length() > l`). Mirrors
/// Julia's `f2l(s, l=10)`.
pub fn f2l(s : String, l : Int) -> String {
  if s.length() < l {
    // Right-pad with spaces to reach `l` characters.
    let pad = l - s.length()
    let sb = StringBuilder::new(size_hint=s.length() + pad)
    sb.write_string(s)
    let mut k = 0
    while k < pad {
      sb.write_char(' ')
      k = k + 1
    }
    sb.to_string()
  } else if s.length() > l {
    // Truncate.
    s.substring(start=0, end=l)
  } else {
    s
  }
}

///|
/// Convenience overload: f2l(s) with default length 10.
pub fn f2l_default(s : String) -> String {
  f2l(s, 10)
}