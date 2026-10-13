// Xoshiro256++ RNG — Julia-compatible.
//
// Julia's default RNG since 1.7 is `Xoshiro` (xoshiro256++). The state is
// four 64-bit unsigned integers s0, s1, s2, s3. Each `rand` call returns
// a Float64 by `Float64(rand(r, UInt64) >>> 11) * 0x1.0p-53`. For Float32
// it is `Float32(rand(r, UInt32) >>> 8) * Float32(0x1.0p-24)`.
//
// Reference: https://github.com/JuliaLang/julia/blob/v1.10.0/stdlib/Random/src/Xoshiro.jl
//
// Bit-exact contract: given the same seed (UInt64 or Vector{UInt32}),
// `rand(Float32, n)` and `rand(Float64, n)` produce the same sequence as
// Julia's `Xoshiro(seed)`.
//
// KNOWN GAP (TODO v0.2.0): for integer seeds Julia goes through
// `make_seed(n) -> Vector{UInt32}` followed by SHA-256 hashing of the
// bytes. Until SHA-256 is implemented in MoonBit, integer seeds use a
// deterministic splitmix-based scheme; sequences will differ from Julia
// when the input is an integer. UInt64 seeds bypass this and produce
// bit-exact state.

///|
/// Xoshiro256++ RNG state. Four UInt64 words; mutable so we can advance.
pub struct Xoshiro {
  mut s0 : UInt64
  mut s1 : UInt64
  mut s2 : UInt64
  mut s3 : UInt64
}

///|
/// Construct a Xoshiro state from four raw UInt64 words. Matches Julia's
/// `Xoshiro(s0, s1, s2, s3)` (which also sets s4 = 1s0+3s1+5s2+7s3, but
/// s4 is metadata and is not used in `rand`).
pub fn Xoshiro::from_state(
  s0 : UInt64,
  s1 : UInt64,
  s2 : UInt64,
  s3 : UInt64,
) -> Xoshiro {
  { s0, s1, s2, s3 }
}

///|
/// Seed Xoshiro from a single UInt64 via splitmix64. Matches Julia's
/// `TaskLocalRNG`/`Xoshiro` bulk-generation seeding without the SHA-256
/// step (only used for integer seeds; pass raw state via `from_state`
/// for full bit-exact reproduction).
pub fn Xoshiro::new(seed : UInt64) -> Xoshiro {
  let s = splitmix64(seed)
  let t = splitmix64(s.3)
  { s0: s.0, s1: s.1, s2: s.2, s3: t.0 }
}

///|
/// Default seed (0).
pub fn Xoshiro::default() -> Xoshiro {
  Xoshiro::new(0UL)
}

///|
/// splitmix64: takes a UInt64, returns 4 UInt64 used to seed xoshiro.
fn splitmix64(seed : UInt64) -> (UInt64, UInt64, UInt64, UInt64) {
  let s1 = seed + 0x9E3779B97F4A7C15UL
  let z1 = mix64(s1)
  let s2 = s1 + 0x9E3779B97F4A7C15UL
  let z2 = mix64(s2)
  let s3 = s2 + 0x9E3779B97F4A7C15UL
  let z3 = mix64(s3)
  let s4 = s3 + 0x9E3779B97F4A7C15UL
  let z4 = mix64(s4)
  (z1, z2, z3, z4)
}

///|
fn mix64(z : UInt64) -> UInt64 {
  let z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL
  let z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL
  z ^ (z >> 31)
}

///|
/// rotl — rotate left.
fn rotl(x : UInt64, k : Int) -> UInt64 {
  (x << k) | (x >> (64 - k))
}

///|
/// xoshiro256++ step: advance state and return a UInt64 in [0, 2^64).
/// Matches Julia's `rand(rng, UInt64)`.
pub fn next_u64(r : Xoshiro) -> UInt64 {
  let s0 = r.s0
  let s1 = r.s1
  let s2 = r.s2
  let s3 = r.s3
  let tmp = s0 + s3
  let res = rotl(tmp, 23) + s0
  let t = s1 << 17
  r.s2 = s2 ^ s0
  r.s3 = s3 ^ s1
  r.s1 = s1 ^ r.s2
  r.s0 = s0 ^ r.s3
  r.s2 = r.s2 ^ t
  r.s3 = rotl(r.s3, 45)
  res
}

///|
/// rand(r, UInt32): take top 32 bits of `rand(r, UInt64)` and cast.
/// Matches Julia's `(rand(rng, UInt64) >>> (64 - 8*4)) % UInt32`.
pub fn next_u32(r : Xoshiro) -> UInt {
  let u = next_u64(r)
  (u >> 32).to_uint()
}

///|
/// rand(r, Float64): `Float64(rand(r, UInt64) >>> 11) * 0x1.0p-53`.
/// Matches Julia's `rand(r, CloseOpen01_64)`.
pub fn next_f64(r : Xoshiro) -> Double {
  let u = next_u64(r)
  let bits = u >> 11
  // 0x1.0p-53 = 2^-53
  bits.to_double() * 0x1.0p-53
}

///|
/// rand(r, Float32): `Float32(rand(r, UInt32) >>> 8) * Float32(0x1.0p-24)`.
/// Matches Julia's `rand(r, CloseOpen01{Float32})`.
pub fn next_f32(r : Xoshiro) -> Float {
  let u = next_u32(r)
  let bits = u >> 8
  // 0x1.0p-24 = 2^-24
  Float::from_double(bits.to_double() * 0x1.0p-24)
}

///|
/// Generate `n` Float32 values matching `rand(Float32, n)` in Julia.
pub fn rand_f32(r : Xoshiro, n : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = next_f32(r)
  }
  out
}

///|
/// Generate `n` Float64 values matching `rand(Float64, n)` in Julia.
pub fn rand_f64(r : Xoshiro, n : Int) -> Array[Double] {
  let out : Array[Double] = Array::make(n, 0.0)
  for i in 0..<n {
    out[i] = next_f64(r)
  }
  out
}
///|
/// Box-Muller transform: produce a pair of N(0, 1) Float64 from
/// two uniform Float64 draws. Matches Julia's `randn()` which uses
/// the same algorithm.
///
/// This was declared in `neuron_iz.mbt`. Drawing a Gaussian has nothing
/// to do with an Izhikevich neuron, and 95 call sites across the tree --
/// ARIMA, BiLSTM, C51, DCGAN, DDPM -- were reaching through
/// `@core.box_muller` for it, which made the nn package import the
/// neuron package purely to sample noise.
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
/// Fill an Array[Float] of length `n` with He-init (Kaiming) draws:
/// N(0, sqrt(2 / fan_in)) using a xoshiro RNG and Float32 Box-Muller.
///
/// This was declared in bottles.mbt, whose subject is pre-made architectures --
/// LeNet5, SimpleCNN, MLP. Initialising a weight array is not an architecture,
/// and 38 files across nine packages reached through `@snn_mbt.he_init` to call
/// it: residual_block, gin, message_passing, pna, set2set, the DDPM and EBM
/// trainers, the GTrXL and LSTM agents, task_sampler, ode_func.
///
/// It belongs here rather than in nn/ because its only inputs are a Xoshiro
/// and two integers -- no tensor, no parameter block, no layer. That made it
/// reachable from every package; keeping it in a leaf file meant a
/// sub-package could not call it at all, since none of them can import the
/// root.
pub fn he_init(rng : Xoshiro, n : Int, fan_in : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  let std = sqrtf(2.0F / Float::from_int(fan_in))
  let mut i = 0
  while i + 1 < n {
    // Box-Muller on Float32 (consume 2 uniforms -> 2 normals).
    let u1 = {
      let mut v = next_f32(rng)
      if v < 0.000001F { v = 0.000001F }
      v
    }
    let u2 = next_f32(rng)
    let z1 = sqrtf(-2.0F * logf(u1)) * cosf(2.0F * 3.14159265F * u2)
    let z2 = sqrtf(-2.0F * logf(u1)) * sin_f32(2.0F * 3.14159265F * u2)
    out[i] = z1 * std
    out[i + 1] = z2 * std
    i = i + 2
  }
  if i < n {
    // Last one if odd length.
    let u1 = {
      let mut v = next_f32(rng)
      if v < 0.000001F { v = 0.000001F }
      v
    }
    let u2 = next_f32(rng)
    let z1 = sqrtf(-2.0F * logf(u1)) * cosf(2.0F * 3.14159265F * u2)
    out[i] = z1 * std
  }
  out
}

///|
fn sin_f32(x : Float) -> Float { sinf(x) }

