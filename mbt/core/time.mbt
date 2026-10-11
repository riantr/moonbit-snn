// Time struct — bit-exact port of SNNModels.jl/src/utils/structs.jl
//
// Julia:
//   mutable struct Time
//     t::Vector{Float32}   = [0.0f0]      # current time (ms), mutable via [1]
//     tt::Vector{Int32}    = Int32[0]      # current timestep
//     dt::Float32          = 0.125f0
//   end
//
//   function Time(time::Number)
//     tts = time / 0.125f0
//     return Time([Float32(time)], Int32[Int32(tts)], 0.125f0)
//   end
//
// MoonBit notes:
// - Float32 is `Float` in MoonBit.
// - We need mutable inner-cell access; we use `Array[Float]` (length 1)
//   for `t` and `Array[Int]` (length 1) for `tt` to allow in-place updates.
// - We expose `update_time!`, `get_time`, `set_time!`, `get_dt`, `set_dt!`,
//   `get_tt`, `set_tt!` matching the Julia API.

///|
/// Time tracker for the SNN simulator. Holds the current simulation time
/// `t` (Float32, ms), the integer step counter `tt` (Int32), and the
/// integration step size `dt` (Float32, default 0.125).
pub struct Time {
  t : Array[Float]
  tt : Array[Int]
  mut dt : Float
}

///|
/// Construct a Time struct at t=0 with dt=0.125f0.
pub fn Time::new() -> Time {
  { t: [0.0F], tt: [0], dt: 0.125F }
}

///|
/// Construct a Time struct from a numeric time value (ms). The initial
/// step counter is `time / 0.125` rounded to Int32, matching Julia's
/// `Int32(tts)` truncation (Float-to-Int casts truncate toward zero).
pub fn Time::from_ms(time : Float) -> Time {
  let tts : Float = time / 0.125F
  { t: [time], tt: [tts.to_int()], dt: 0.125F }
}

///|
/// Get the current simulation time in ms. Matches Julia's `get_time(T)`.
pub fn get_time(t : Time) -> Float {
  t.t[0]
}

///|
/// Set the current simulation time in ms. Matches Julia's
/// `set_time!(T, time)` (not used in the inner loop, but available).
pub fn set_time(t : Time, v : Float) -> Unit {
  t.t[0] = v
}

///|
/// Get the current integration timestep counter. Matches `get_tt`.
pub fn get_tt(t : Time) -> Int {
  t.tt[0]
}

///|
/// Set the current integration timestep counter. Matches `set_tt!`.
pub fn set_tt(t : Time, v : Int) -> Unit {
  t.tt[0] = v
}

///|
/// Get the integration step size dt in ms. Matches `get_dt`.
pub fn get_dt(t : Time) -> Float {
  t.dt
}

///|
/// Set the integration step size dt in ms. Matches `set_dt!`.
pub fn set_dt(t : Time, v : Float) -> Unit {
  t.dt = v
}

///|
/// Advance the simulation by one timestep. Matches Julia's
/// `update_time!(T, dt)`:
///   T.t[1] += dt
///   T.tt[1] += 1
pub fn update_time(t : Time, dt : Float) -> Unit {
  t.t[0] = t.t[0] + dt
  t.tt[0] = t.tt[0] + 1
}

///|
/// Reset the time to zero. Matches `reset_time!(T)`.
pub fn reset_time(t : Time) -> Unit {
  t.t[0] = 0.0F
  t.tt[0] = 0
}
