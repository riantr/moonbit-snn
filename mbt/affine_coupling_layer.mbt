// affine_coupling_layer.mbt — AffineCouplingLayer (v0.101.0), the core
// building block of normalizing flows.
//
// An affine coupling layer (Dinh et al. 2016 "Real NVP") splits the
// input x ∈ R^d in two halves x = (x₁, x₂) and applies:
//   y₂ = x₂ ⊙ exp(s(x₁)) + t(x₁)
//   y = (y₁, y₂) where y₁ = x₁
// where s, t : R^{d/2} → R^{d/2} are learnable scale and translation
// functions.
//
// The Jacobian of this transformation is triangular:
//   J = [ I    0  ]
//       [ *  diag(exp(s(x₁))) ]
// so log |det J| = Σ_i s(x₁)[i].
//
// The inverse is straightforward:
//   x₂ = (y₂ - t(x₁)) ⊙ exp(-s(x₁))
//   x = (x₁, x₂)
//
// For v0.101.0 we use simple LINEAR projections for s and t (no NN):
//   s(x₁) = scale_w · x₁ + scale_b
//   t(x₁) = trans_w · x₁ + trans_b
// This keeps the layer cheap and exposes the canonical coupling
// structure. A neural-network s/t is straightforward to add later.
//
// Reference: Dinh et al. 2016 "Density Estimation with Real NVP";
// Dinh et al. 2018 "Glow" Section 3.

///|
/// Affine coupling layer. Holds linear projection weights for scale and
/// translation (both d/2 × d/2). The input is split into two halves of
/// length `half_dim` (must be even input dim).
pub struct AffineCouplingLayer {
  half_dim : Int
  // scale_w: (half_dim × half_dim), scale_b: (half_dim)
  scale_w : Array[Array[Float]]
  scale_b : Array[Float]
  // trans_w: (half_dim × half_dim), trans_b: (half_dim)
  trans_w : Array[Array[Float]]
  trans_b : Array[Float]
}

///|
/// Build a fresh AffineCouplingLayer with xavier-normal weights.
pub fn AffineCouplingLayer::new(
  half_dim : Int,
  seed : UInt64,
) -> AffineCouplingLayer {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std = sqrtf(2.0F / Float::from_int(half_dim))
  let scale_w = xavier_normal(half_dim, half_dim, std, rng1)
  let scale_b : Array[Float] = Array::make(half_dim, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let trans_w = xavier_normal(half_dim, half_dim, std, rng2)
  let trans_b : Array[Float] = Array::make(half_dim, 0.0F)
  { half_dim, scale_w, scale_b, trans_w, trans_b }
}

///|
/// Apply the linear projection: out = w · x + b.
fn linear_project(
  w : Array[Array[Float]],
  b : Array[Float],
  x : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(b.length(), 0.0F)
  for i in 0..<b.length() {
    let mut s = b[i]
    for k in 0..<x.length() {
      s = s + w[i][k] * x[k]
    }
    out[i] = s
  }
  out
}

///|
/// Forward: x → y. Splits x into x₁ (length half_dim) and x₂
/// (length half_dim). Output: y = (x₁, x₂ ⊙ exp(s(x₁)) + t(x₁)).
/// Returns `(y, log_det_jacobian)`.
pub fn coupling_forward(
  layer : AffineCouplingLayer,
  x : Array[Float],
) -> (Array[Float], Float) {
  let half = layer.half_dim
  let x1 : Array[Float] = Array::make(half, 0.0F)
  let x2 : Array[Float] = Array::make(half, 0.0F)
  for i in 0..<half {
    x1[i] = x[i]
    x2[i] = x[half + i]
  }
  let s_x1 = linear_project(layer.scale_w, layer.scale_b, x1)
  let t_x1 = linear_project(layer.trans_w, layer.trans_b, x1)
  let y2 : Array[Float] = Array::make(half, 0.0F)
  let mut log_det = 0.0F
  for i in 0..<half {
    let exp_s = expf(s_x1[i])
    y2[i] = x2[i] * exp_s + t_x1[i]
    log_det = log_det + s_x1[i]
  }
  let y : Array[Float] = Array::make(2 * half, 0.0F)
  for i in 0..<half {
    y[i] = x1[i]
    y[half + i] = y2[i]
  }
  (y, log_det)
}

///|
/// Inverse: y → x. Splits y into (y₁, y₂) and recovers
//   x₂ = (y₂ - t(x₁)) ⊙ exp(-s(x₁))
/// Returns `x` (no log_det — caller can compute via -log_det from
/// forward if needed).
pub fn coupling_inverse(
  layer : AffineCouplingLayer,
  y : Array[Float],
) -> Array[Float] {
  let half = layer.half_dim
  let y1 : Array[Float] = Array::make(half, 0.0F)
  let y2 : Array[Float] = Array::make(half, 0.0F)
  for i in 0..<half {
    y1[i] = y[i]
    y2[i] = y[half + i]
  }
  let s_y1 = linear_project(layer.scale_w, layer.scale_b, y1)
  let t_y1 = linear_project(layer.trans_w, layer.trans_b, y1)
  let x2 : Array[Float] = Array::make(half, 0.0F)
  for i in 0..<half {
    x2[i] = (y2[i] - t_y1[i]) * expf(-s_y1[i])
  }
  let x : Array[Float] = Array::make(2 * half, 0.0F)
  for i in 0..<half {
    x[i] = y1[i]
    x[half + i] = x2[i]
  }
  x
}

///|
/// Return the log |det J| of the forward transformation at `x`.
/// Equivalent to summing `s(x₁)[i]` over all i.
pub fn coupling_log_det(
  layer : AffineCouplingLayer,
  x : Array[Float],
) -> Float {
  let half = layer.half_dim
  let x1 : Array[Float] = Array::make(half, 0.0F)
  for i in 0..<half {
    x1[i] = x[i]
  }
  let s_x1 = linear_project(layer.scale_w, layer.scale_b, x1)
  let mut log_det = 0.0F
  for i in 0..<half {
    log_det = log_det + s_x1[i]
  }
  log_det
}