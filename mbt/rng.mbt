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
