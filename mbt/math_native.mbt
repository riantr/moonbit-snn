// Native math FFI for bit-exact Float32 transcendental functions.
//
// Julia uses libm's `expf`, `cosf`, `sinf`, `tanhf` etc. for Float32
// transcendentals in neuron/synapse models. MoonBit's standard math
// library operates on Double and would round differently when cast
// to Float. We bind directly to libm for the bit-exact single-precision
// versions.

///|
/// Float32 exponential: matches Julia's `exp(Float32, x)` and the C
/// `expf` function. Used by AdEx (exponential spike-initiation term)
/// and other biophysical models.
pub extern "C" fn expf(x : Float) -> Float = "expf"

///|
/// Float32 hyperbolic tangent: matches Julia's `tanh(Float32, x)` and
/// the C `tanhf` function.
pub extern "C" fn tanhf(x : Float) -> Float = "tanhf"

///|
/// Float32 sigmoid / logistic. Computed as `1.0 / (1.0 + expf(-x))`.
/// Matches Julia's `1 / (1 + expf(-x))` (single-precision exp).
pub fn sigmoid_f32(x : Float) -> Float {
  1.0F / (1.0F + expf(-x))
}

///|
/// Float32 absolute value, no FFI needed.
pub fn abs_f32(x : Float) -> Float {
  if x < 0.0F {
    -x
  } else {
    x
  }
}

///|
/// Float32 natural logarithm: matches Julia's `log(Float32, x)` and the
/// C `logf` function. Used by STDP kernels with log-ratio traces.
pub extern "C" fn logf(x : Float) -> Float = "logf"
