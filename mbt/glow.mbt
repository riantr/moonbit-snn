// glow.mbt 鈥?Glow normalizing flow (v0.103.0): RealNVP + ActNorm +
// 1脳1 convolution permutation.
//
// Glow (Dinh et al. 2018) extends Real NVP with two additional
// layers:
//   1. ActNorm: a learnable per-dimension affine
//        y = scale 鈯?x + bias
//      where scale and bias are initialized from the first batch's
//      per-dim std/mean (data-dependent init).
//   2. 1脳1 convolution permutation: replaces the fixed reverse
//      permutation with a learnable permutation matrix W. The log
//      |det W| contributes to the total log det.
// A Glow "step" chains: ActNorm 鈫?1脳1 conv 鈫?AffineCoupling.
//
// Reference: Dinh et al. 2018 "Glow: Generative Flow with Invertible
// 1脳1 Convolutions".

///|
/// ActNorm layer: per-dim learnable affine y = scale 路 x + bias.
/// Initialized from data statistics (mean, std of the first batch).
pub struct ActNorm {
  dim : Int
  scale : Array[Float]
  bias : Array[Float]
}

///|
/// Build ActNorm with zero scale + zero bias (init from data later).
pub fn ActNorm::new(dim : Int) -> ActNorm {
  let scale : Array[Float] = Array::make(dim, 1.0F)
  let bias : Array[Float] = Array::make(dim, 0.0F)
  { dim, scale, bias }
}

///|
/// Initialize ActNorm's scale + bias from data (per-dim std + mean).
pub fn actnorm_init_from_data(
  layer : ActNorm,
  data : Array[Float],
  n : Int,
) -> ActNorm {
  if n <= 0 || layer.dim == 0 {
    return layer
  }
  let scale : Array[Float] = Array::make(layer.dim, 0.0F)
  let bias : Array[Float] = Array::make(layer.dim, 0.0F)
  for k in 0..<layer.dim {
    let mut sum = 0.0F
    for i in 0..<n {
      sum = sum + data[i * layer.dim + k]
    }
    let mean = sum / Float::from_int(n)
    let mut variance = 0.0F
    for i in 0..<n {
      let d = data[i * layer.dim + k] - mean
      variance = variance + d * d
    }
    let std = sqrtf(variance / Float::from_int(n))
    let safe_std = if std < 1.0e-6F { 1.0e-6F } else { std }
    scale[k] = 1.0F / safe_std
    bias[k] = -mean / safe_std
  }
  { dim: layer.dim, scale, bias }
}

///|
/// ActNorm forward: y = scale 路 x + bias.
pub fn actnorm_forward(
  layer : ActNorm,
  x : Array[Float],
) -> Array[Float] {
  let y : Array[Float] = Array::make(layer.dim, 0.0F)
  for i in 0..<layer.dim {
    y[i] = layer.scale[i] * x[i] + layer.bias[i]
  }
  y
}

///|
/// ActNorm inverse: x = (y - bias) / scale.
pub fn actnorm_inverse(
  layer : ActNorm,
  y : Array[Float],
) -> Array[Float] {
  let x : Array[Float] = Array::make(layer.dim, 0.0F)
  for i in 0..<layer.dim {
    x[i] = (y[i] - layer.bias[i]) / layer.scale[i]
  }
  x
}

///|
/// ActNorm log |det J| = 危 log |scale[i]|.
pub fn actnorm_log_det(layer : ActNorm) -> Float {
  let mut ld = 0.0F
  for i in 0..<layer.dim {
    let abs_scale = if layer.scale[i] < 0.0F {
      -layer.scale[i]
    } else {
      layer.scale[i]
    }
    let safe_scale = if abs_scale < 1.0e-12F { 1.0e-12F } else { abs_scale }
    ld = ld + logf(safe_scale)
  }
  ld
}

///|
/// 1脳1 convolution (on 1D data: a permutation matrix W 鈭?R^{d 脳 d}).
/// log |det J| = log |det W|.
pub struct OneByOneConv {
  dim : Int
  // W is dim 脳 dim. log_det_W = log|det W| (cached at init).
  w : Array[Array[Float]]
  log_det_w : Float
}

///|
/// Build a fresh 1脳1 conv with xavier-normal weights + LU-style
/// log det placeholder (for v0.100.0 we use the LU-style log det
/// from a freeform square matrix; here we approximate via an
/// explicit LU decomposition stored separately).
///
/// For simplicity in v0.100.0 we initialize W as a (slightly
/// perturbed) identity, so log|det W| = 0. This is the Glow init
/// recommended in practice (initialize W as identity or a random
/// rotation).
pub fn OneByOneConv::new(dim : Int, seed : UInt64) -> OneByOneConv {
  let w : Array[Array[Float]] = Array::make(
    dim, Array::make(dim, 0.0F),
  )
  // Initialize as identity + small noise.
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  for i in 0..<dim {
    w[i][i] = 1.0F
  }
  for i in 0..<dim {
    for j in 0..<dim {
      if i != j {
        w[i][j] = 0.01F * gaussian_noise_via_box_muller(rng)
      }
    }
  }
  // Compute log|det W| via LU decomposition (Gaussian elimination).
  let log_det_w = matrix_log_det(w, dim)
  { dim, w, log_det_w }
}

///|
/// Helper: standard normal via Box-Muller.
fn gaussian_noise_via_box_muller(r : Xoshiro) -> Float {
  let (z1, _) = box_muller(r)
  Float::from_double(z1)
}

///|
/// Compute log |det A| for a square matrix A via Gaussian elimination
/// (LU decomposition without pivoting). For v0.100.0 we just track
/// the diagonal of the upper-triangular U.
fn matrix_log_det(a : Array[Array[Float]], n : Int) -> Float {
  let lu : Array[Array[Float]] = Array::make(
    n, Array::make(n, 0.0F),
  )
  for i in 0..<n {
    for j in 0..<n {
      lu[i][j] = a[i][j]
    }
  }
  let mut log_det = 0.0F
  let mut sign = 1.0F
  for k in 0..<n {
    // Find pivot.
    let mut pivot_row = k
    let mut pivot_val = if lu[k][k] < 0.0F { -lu[k][k] } else { lu[k][k] }
    for i in (k + 1)..<n {
      let v = if lu[i][k] < 0.0F { -lu[i][k] } else { lu[i][k] }
      if v > pivot_val {
        pivot_val = v
        pivot_row = i
      }
    }
    if pivot_val < 1.0e-12F {
      return 0.0F  // singular
    }
    if pivot_row != k {
      // Swap rows.
      let tmp = lu[k]
      lu[k] = lu[pivot_row]
      lu[pivot_row] = tmp
      sign = -sign
    }
    log_det = log_det + logf(if lu[k][k] < 0.0F { -lu[k][k] } else { lu[k][k] })
    // Eliminate below.
    for i in (k + 1)..<n {
      let factor = lu[i][k] / lu[k][k]
      for j in (k + 1)..<n {
        lu[i][j] = lu[i][j] - factor * lu[k][j]
      }
      lu[i][k] = 0.0F
    }
  }
  if sign < 0.0F {
    log_det
  } else {
    log_det
  }
}

///|
/// 1脳1 conv forward: y = W 路 x.
pub fn one_by_one_conv_forward(
  conv : OneByOneConv,
  x : Array[Float],
) -> Array[Float] {
  let y : Array[Float] = Array::make(conv.dim, 0.0F)
  for i in 0..<conv.dim {
    let mut s = 0.0F
    for k in 0..<conv.dim {
      s = s + conv.w[i][k] * x[k]
    }
    y[i] = s
  }
  y
}

///|
/// 1脳1 conv inverse: x = W^(-1) 路 y. Computed via Gaussian
/// elimination at call time (no cached inverse).
pub fn one_by_one_conv_inverse(
  conv : OneByOneConv,
  y : Array[Float],
) -> Array[Float] {
  // Build augmented matrix [W | y] and solve via Gaussian elimination.
  let n = conv.dim
  let aug : Array[Array[Float]] = Array::make(
    n, Array::make(n + 1, 0.0F),
  )
  for i in 0..<n {
    for k in 0..<n {
      aug[i][k] = conv.w[i][k]
    }
    aug[i][n] = y[i]
  }
  // Forward elimination.
  for k in 0..<n {
    let mut pivot_row = k
    let mut pivot_val = if aug[k][k] < 0.0F { -aug[k][k] } else { aug[k][k] }
    for i in (k + 1)..<n {
      let v = if aug[i][k] < 0.0F { -aug[i][k] } else { aug[i][k] }
      if v > pivot_val {
        pivot_val = v
        pivot_row = i
      }
    }
    if pivot_row != k {
      let tmp = aug[k]
      aug[k] = aug[pivot_row]
      aug[pivot_row] = tmp
    }
    for i in (k + 1)..<n {
      let factor = aug[i][k] / aug[k][k]
      for j in k..<(n + 1) {
        aug[i][j] = aug[i][j] - factor * aug[k][j]
      }
    }
  }
  // Back substitution.
  let x : Array[Float] = Array::make(n, 0.0F)
  for i_rev in 0..<n {
    let i = n - 1 - i_rev
    let mut s = aug[i][n]
    for j in (i + 1)..<n {
      s = s - aug[i][j] * x[j]
    }
    x[i] = s / aug[i][i]
  }
  x
}

///|
/// Glow "step": ActNorm + 1脳1 conv + AffineCoupling. A Glow model
/// is a stack of N such steps.
pub struct GlowStep {
  actnorm : ActNorm
  conv : OneByOneConv
  coupling : AffineCouplingLayer
}

///|
/// Build a fresh GlowStep. `dim` is the input dim, `half_dim = dim/2`.
pub fn GlowStep::new(dim : Int, seed : UInt64) -> GlowStep {
  let half = dim / 2
  let actnorm = ActNorm::new(dim)
  let conv = OneByOneConv::new(dim, seed)
  let coupling = AffineCouplingLayer::new(half, seed + 100UL)
  { actnorm, conv, coupling }
}

///|
/// Initialize ActNorm scale + bias from data (caller must call this
/// before the first forward pass for proper init).
pub fn glow_step_init_from_data(
  step : GlowStep,
  data : Array[Float],
  n : Int,
) -> GlowStep {
  let new_actnorm = actnorm_init_from_data(step.actnorm, data, n)
  { ..step, actnorm: new_actnorm }
}

///|
/// Glow step forward: y = coupling(conv(actnorm(x))).
/// Returns `(y, log_det)` where log_det = log_det_actnorm +
/// log_det_W + log_det_coupling.
pub fn glow_step_forward(
  step : GlowStep,
  x : Array[Float],
) -> (Array[Float], Float) {
  let y_actnorm = actnorm_forward(step.actnorm, x)
  let ld_actnorm = actnorm_log_det(step.actnorm)
  let y_conv = one_by_one_conv_forward(step.conv, y_actnorm)
  let ld_conv = step.conv.log_det_w
  let (y_coupling, ld_coupling) = coupling_forward(step.coupling, y_conv)
  let total_ld = ld_actnorm + ld_conv + ld_coupling
  (y_coupling, total_ld)
}

///|
/// Glow step inverse: x = actnorm_inverse(conv_inverse(coupling_inverse(y))).
pub fn glow_step_inverse(
  step : GlowStep,
  y : Array[Float],
) -> Array[Float] {
  let x_coupling = coupling_inverse(step.coupling, y)
  let x_conv = one_by_one_conv_inverse(step.conv, x_coupling)
  let x = actnorm_inverse(step.actnorm, x_conv)
  x
}

///|
/// Glow flow: N stacked GlowSteps.
pub struct Glow {
  n_steps : Int
  steps : Array[GlowStep]
  dim : Int
}

///|
/// Build a fresh Glow flow.
pub fn Glow::new(dim : Int, n_steps : Int, seed : UInt64) -> Glow {
  let steps : Array[GlowStep] = Array::make(
    n_steps, GlowStep::new(dim, seed),
  )
  for i in 0..<n_steps {
    steps[i] = GlowStep::new(
      dim, seed + (i + 1).to_uint64() * 1000UL,
    )
  }
  { n_steps, steps, dim }
}

///|
/// Initialize ActNorms from data. Should be called once before the
/// first forward pass.
pub fn glow_init_from_data(
  flow : Glow,
  data : Array[Float],
  n : Int,
) -> Glow {
  for i in 0..<flow.n_steps {
    flow.steps[i] = glow_step_init_from_data(flow.steps[i], data, n)
  }
  flow
}

///|
/// Glow forward: chain GlowStep forward across all steps. Returns
/// `(y, total_log_det)`.
pub fn glow_forward(
  flow : Glow,
  x : Array[Float],
) -> (Array[Float], Float) {
  let mut cur = x.copy()
  let mut total_log_det = 0.0F
  for i in 0..<flow.n_steps {
    let (y, ld) = glow_step_forward(flow.steps[i], cur)
    cur = y
    total_log_det = total_log_det + ld
  }
  (cur, total_log_det)
}

///|
/// Glow inverse: chain GlowStep inverse across all steps in reverse.
pub fn glow_inverse(
  flow : Glow,
  y : Array[Float],
) -> Array[Float] {
  let mut cur = y.copy()
  let mut i = flow.n_steps - 1
  while i >= 0 {
    cur = glow_step_inverse(flow.steps[i], cur)
    i = i - 1
  }
  cur
}