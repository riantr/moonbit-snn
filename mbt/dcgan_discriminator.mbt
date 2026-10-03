// dcgan_discriminator.mbt -- DCGAN discriminator (v0.122.0).
//
// The DCGAN discriminator (Radford et al. 2016) classifies an image as
// real (1.0) or fake (0.0):
//
//   image [c_out, 32, 32]
//   -> stride-2 conv (c_out -> c_base, 4x4) -> LeakyReLU(0.2)     [16x16]
//   -> stride-2 conv (c_base -> c_base*2, 4x4) -> LeakyReLU(0.2)  [8x8]
//   -> stride-2 conv (c_base*2 -> c_base*4, 4x4) -> LeakyReLU(0.2)[4x4]
//   -> flatten -> Linear (c_base*4*4 -> 1) -> sigmoid
//
// The original DCGAN uses spectral normalisation on the discriminator
// weights for training stability. We ship the forward pass plus the
// spectral-norm power-iteration helper; the actual weight rescaling in
// the training step is deferred.
//
// Scope of v0.122.0:
//   - DCGANDiscriminator struct.
//   - DCGANDiscriminator::new.
//   - spectral_norm_power_iteration: one power-iteration step to
//     estimate the spectral norm of a weight matrix.
//   - dcgan_discriminator_forward: image -> logit.
//   - dcgan_discriminator_prob: image -> probability in [0, 1].
//   - dcgan_discriminator_num_params.

///|
/// DCGANDiscriminator: image -> real/fake logit.
pub struct DCGANDiscriminator {
  c_in : Int
  c_base : Int
  h : Int
  w : Int
  // Conv layers, weights as (c_out rows x (c_in*4*4) cols) for 4x4 kernels.
  conv1_w : Array[Array[Float]]
  conv1_b : Array[Float]
  conv2_w : Array[Array[Float]]
  conv2_b : Array[Float]
  conv3_w : Array[Array[Float]]
  conv3_b : Array[Float]
  // Classifier: (1 x (c_base*4*4*4*4)).
  fc_w : Array[Array[Float]]
  fc_b : Float
}

///|
/// Build a fresh DCGANDiscriminator for 32x32 inputs.
pub fn DCGANDiscriminator::new(
  c_in : Int,
  c_base : Int,
  h : Int,
  w : Int,
  seed : UInt64,
) -> DCGANDiscriminator {
  let conv1_w = conv4x4_weights(c_base, c_in, seed + 11UL)
  let conv1_b : Array[Float] = Array::make(c_base, 0.0F)
  let conv2_w = conv4x4_weights(c_base * 2, c_base, seed + 22UL)
  let conv2_b : Array[Float] = Array::make(c_base * 2, 0.0F)
  let conv3_w = conv4x4_weights(c_base * 4, c_base * 2, seed + 33UL)
  let conv3_b : Array[Float] = Array::make(c_base * 4, 0.0F)
  // After three stride-2 convs from 32x32 we get 4x4 with c_base*4 chans.
  let feat_dim = c_base * 4 * 4 * 4 * 4
  let rng_fc = Xoshiro::from_state(
    seed + 44UL, seed + 45UL, seed + 46UL, seed + 47UL,
  )
  let std_fc = sqrtf(2.0F / Float::from_int(feat_dim))
  let fc_w = xavier_normal(1, feat_dim, std_fc, rng_fc)
  { c_in, c_base, h, w, conv1_w, conv1_b, conv2_w, conv2_b, conv3_w, conv3_b, fc_w, fc_b: 0.0F }
}

///|
/// helper: build a (c_out x (c_in*16)) weight matrix for 4x4 kernels
/// with xavier-normal init.
fn conv4x4_weights(
  c_out : Int,
  c_in : Int,
  seed : UInt64,
) -> Array[Array[Float]] {
  let in_dim = c_in * 16
  let rows : Array[Array[Float]] = Array::make(c_out, Array::make(in_dim, 0.0F))
  let std = sqrtf(2.0F / Float::from_int(in_dim))
  let rng = Xoshiro::new(seed)
  for o in 0..<c_out {
    for i in 0..<in_dim {
      rows[o][i] = std * (next_f32(rng) * 2.0F - 1.0F)
    }
  }
  rows
}

///|
/// helper: 4x4 stride-2 convolution (pad=1) over a [c_in, h, w] map.
/// Output spatial dims are h/2 x w/2.
fn conv4x4_stride2(
  x : Array[Float],
  c_in : Int,
  h : Int,
  w : Int,
  weights : Array[Array[Float]],
  c_out : Int,
  bias : Array[Float],
) -> Array[Float] {
  let ho = h / 2
  let wo = w / 2
  let out : Array[Float] = Array::make(c_out * ho * wo, 0.0F)
  for o in 0..<c_out {
    let dst_off = o * ho * wo
    for y in 0..<ho {
      for x2 in 0..<wo {
        let mut acc = bias[o]
        for ci in 0..<c_in {
          let src_off = ci * h * w
          for ky in 0..<4 {
            for kx in 0..<4 {
              // Stride-2 output position (y, x2) maps to input top-left
              // (2y, 2x2); the 4x4 kernel then covers [2y, 2y+4).
              let iy = y * 2 + ky - 1
              let ix = x2 * 2 + kx - 1
              if iy >= 0 && iy < h && ix >= 0 && ix < w {
                let k_idx = ci * 16 + ky * 4 + kx
                acc = acc + weights[o][k_idx] * x[src_off + iy * w + ix]
              }
            }
          }
        }
        out[dst_off + y * wo + x2] = acc
      }
    }
  }
  out
}

///|
/// helper: LeakyReLU with slope 0.2.
fn leaky_relu(x : Array[Float]) -> Array[Float] {
  let out : Array[Float] = Array::make(x.length(), 0.0F)
  for i in 0..<x.length() {
    out[i] = if x[i] > 0.0F { x[i] } else { 0.2F * x[i] }
  }
  out
}

///|
/// One power-iteration step to estimate the spectral norm (largest
/// singular value) of the weight matrix `wmat` (rows x cols). Returns
/// the Rayleigh quotient ||W u|| / ||u|| for the current iterate `u`.
pub fn spectral_norm_power_iteration(
  wmat : Array[Array[Float]],
  u : Array[Float],
) -> Float {
  let rows = wmat.length()
  let cols = wmat[0].length()
  // v = W^T u
  let v : Array[Float] = Array::make(cols, 0.0F)
  for c in 0..<cols {
    let mut acc = 0.0F
    for r in 0..<rows {
      acc = acc + wmat[r][c] * u[r]
    }
    v[c] = acc
  }
  // ||v||
  let mut vnorm = 0.0F
  for c in 0..<cols {
    vnorm = vnorm + v[c] * v[c]
  }
  vnorm = sqrtf(vnorm)
  if vnorm < 1.0e-12F {
    return 0.0F
  }
  // u' = W v / ||v||
  let unorm : Array[Float] = Array::make(rows, 0.0F)
  for r in 0..<rows {
    let mut acc = 0.0F
    for c in 0..<cols {
      acc = acc + wmat[r][c] * v[c]
    }
    unorm[r] = acc
  }
  let mut un = 0.0F
  for r in 0..<rows {
    un = un + unorm[r] * unorm[r]
  }
  un = sqrtf(un)
  if un < 1.0e-12F {
    return 0.0F
  }
  // Rayleigh quotient: ||W v|| / ||v||
  un / vnorm
}

///|
/// Forward pass: image [c_in, h, w] -> real/fake logit (scalar).
pub fn dcgan_discriminator_forward(
  d : DCGANDiscriminator,
  image : Array[Float],
) -> Float {
  let c1 = conv4x4_stride2(
    image, d.c_in, d.h, d.w, d.conv1_w, d.c_base, d.conv1_b,
  )
  let h1 = leaky_relu(c1)
  let h1h = d.h / 2
  let h1w = d.w / 2
  let c2 = conv4x4_stride2(
    h1, d.c_base, h1h, h1w, d.conv2_w, d.c_base * 2, d.conv2_b,
  )
  let h2 = leaky_relu(c2)
  let h2h = h1h / 2
  let h2w = h1w / 2
  let c3 = conv4x4_stride2(
    h2, d.c_base * 2, h2h, h2w, d.conv3_w, d.c_base * 4, d.conv3_b,
  )
  let h3 = leaky_relu(c3)
  let feat_dim = d.c_base * 4 * 4 * 4 * 4
  let mut acc = d.fc_b
  for k in 0..<feat_dim {
    acc = acc + d.fc_w[0][k] * h3[k]
  }
  acc
}

///|
/// Forward pass returning a probability in [0, 1] via sigmoid.
pub fn dcgan_discriminator_prob(
  d : DCGANDiscriminator,
  image : Array[Float],
) -> Float {
  let logit = dcgan_discriminator_forward(d, image)
  // sigmoid
  if logit >= 0.0F {
    1.0F / (1.0F + expf(-logit))
  } else {
    let e = expf(logit)
    e / (1.0F + e)
  }
}

///|
/// Count learnable scalars in the discriminator.
pub fn dcgan_discriminator_num_params(d : DCGANDiscriminator) -> Int {
  let mut total = 0
  total = total + d.conv1_w.length() * d.conv1_w[0].length()
  total = total + d.conv1_b.length()
  total = total + d.conv2_w.length() * d.conv2_w[0].length()
  total = total + d.conv2_b.length()
  total = total + d.conv3_w.length() * d.conv3_w[0].length()
  total = total + d.conv3_b.length()
  total = total + d.fc_w.length() * d.fc_w[0].length()
  total = total + 1
  total
}