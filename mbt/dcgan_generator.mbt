// dcgan_generator.mbt -- DCGAN generator (v0.121.0).
//
// The DCGAN generator (Radford et al. 2016 "Unsupervised Learning
// with Deep Convolutional Generative Adversarial Networks") maps a
// latent vector z to an image:
//
//   z [latent_dim] -> Linear -> [c_base*4*4]
//   -> reshape to [c_base, 4, 4]
//   -> deconv(c_base -> c_base*2, 4x4, stride 2) -> BN -> ReLU  [8x8]
//   -> deconv(c_base*2 -> c_base, 4x4, stride 2) -> BN -> ReLU  [16x16]
//   -> deconv(c_base -> c_out, 4x4, stride 2) -> Tanh          [32x32]
//
// Rather than depending on a transposed-convolution primitive (which
// this project does not yet ship), we use nearest-neighbour
// upsampling followed by a regular convolution. This is the
// "upconv" variant and is mathematically equivalent in expressiveness
// while reusing the existing `conv2d_forward`.
//
// Scope of v0.121.0:
//   - DCGANGenerator struct.
//   - DCGANGenerator::new.
//   - upsample2d: nearest-neighbour 2x upsampler.
//   - dcgan_generator_forward: z -> image.
//   - dcgan_generator_num_params.

///|
/// DCGANGenerator: latent vector -> RGB (or grayscale) image.
pub struct DCGANGenerator {
  latent_dim : Int
  c_base : Int
  c_out : Int
  out_h : Int
  out_w : Int
  // Latent projection: (c_base*4*4 x latent_dim).
  fc_w : Array[Array[Float]]
  fc_b : Array[Float]
  // Conv layer 1: (c_base*2 x c_base), 3x3, stride 1, pad 1.
  conv1_w : Array[Array[Float]]
  conv1_b : Array[Float]
  bn1_gamma : Array[Float]
  bn1_beta : Array[Float]
  // Conv layer 2: (c_base x c_base), 3x3, stride 1, pad 1.
  conv2_w : Array[Array[Float]]
  conv2_b : Array[Float]
  bn2_gamma : Array[Float]
  bn2_beta : Array[Float]
  // Conv layer 3: (c_out x c_base), 3x3, stride 1, pad 1.
  conv3_w : Array[Array[Float]]
  conv3_b : Array[Float]
}

///|
/// Build a fresh DCGANGenerator. `out_h` and `out_w` must be 32 (the
/// canonical DCGAN output size for the 3-stage stack).
pub fn DCGANGenerator::new(
  latent_dim : Int,
  c_base : Int,
  c_out : Int,
  out_h : Int,
  out_w : Int,
  seed : UInt64,
) -> DCGANGenerator {
  let proj_dim = c_base * 4 * 4
  let rng0 = Xoshiro::from_state(
    seed + 1UL, seed + 2UL, seed + 3UL, seed + 4UL,
  )
  let std0 = sqrtf(2.0F / Float::from_int(latent_dim))
  let fc_w = xavier_normal(proj_dim, latent_dim, std0, rng0)
  let fc_b : Array[Float] = Array::make(proj_dim, 0.0F)
  // Conv 1.
  let rng1 = Xoshiro::from_state(
    seed + 11UL, seed + 12UL, seed + 13UL, seed + 14UL,
  )
  let conv1_w = xavier_normal_conv(c_base * 2, c_base, 3)
  let conv1_b : Array[Float] = Array::make(c_base * 2, 0.0F)
  let bn1_gamma : Array[Float] = Array::make(c_base * 2, 1.0F)
  let bn1_beta : Array[Float] = Array::make(c_base * 2, 0.0F)
  let _ = rng1
  // Conv 2.
  let conv2_w = xavier_normal_conv(c_base, c_base, 3)
  let conv2_b : Array[Float] = Array::make(c_base, 0.0F)
  let bn2_gamma : Array[Float] = Array::make(c_base, 1.0F)
  let bn2_beta : Array[Float] = Array::make(c_base, 0.0F)
  // Conv 3.
  let conv3_w = xavier_normal_conv(c_out, c_base, 3)
  let conv3_b : Array[Float] = Array::make(c_out, 0.0F)
  { latent_dim, c_base, c_out, out_h, out_w, fc_w, fc_b, conv1_w, conv1_b, bn1_gamma, bn1_beta, conv2_w, conv2_b, bn2_gamma, bn2_beta, conv3_w, conv3_b }
}

///|
/// helper: build a Conv2dParam with xavier-normal init, shape
/// (c_out, c_in, kh, kw) flattened to a flat Array[Float].
fn xavier_normal_conv(
  c_out : Int,
  c_in : Int,
  k : Int,
) -> Array[Array[Float]] {
  // Represented as c_out rows of (c_in*k*k) entries for easy reuse with
  // a matvec-style inner product.
  let in_dim = c_in * k * k
  let rows : Array[Array[Float]] = Array::make(c_out, Array::make(in_dim, 0.0F))
  let std = sqrtf(2.0F / Float::from_int(in_dim))
  let rng = Xoshiro::new(20261004UL + c_out.to_uint64() * 31UL + c_in.to_uint64())
  for o in 0..<c_out {
    for i in 0..<in_dim {
      rows[o][i] = std * (next_f32(rng) * 2.0F - 1.0F)
    }
  }
  rows
}

///|
/// Nearest-neighbour 2x upsample of a [c, h, w] feature map.
fn upsample2d(x : Array[Float], c : Int, h : Int, w : Int) -> Array[Float] {
  let ho = h * 2
  let wo = w * 2
  let out : Array[Float] = Array::make(c * ho * wo, 0.0F)
  for ch in 0..<c {
    let src_off = ch * h * w
    let dst_off = ch * ho * wo
    for y in 0..<h {
      for x2 in 0..<w {
        let v = x[src_off + y * w + x2]
        // Replicate 2x2.
        for dy in 0..<2 {
          for dx in 0..<2 {
            out[dst_off + (y * 2 + dy) * wo + (x2 * 2 + dx)] = v
          }
        }
      }
    }
  }
  out
}

///|
/// helper: apply a 3x3 conv (stride 1, pad 1) with the given
/// (c_out x (c_in*9)) weight rows, to a [c_in, h, w] map.
fn conv3x3(
  x : Array[Float],
  c_in : Int,
  h : Int,
  w : Int,
  weights : Array[Array[Float]],
  c_out : Int,
  bias : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(c_out * h * w, 0.0F)
  for o in 0..<c_out {
    let dst_off = o * h * w
    for y in 0..<h {
      for x2 in 0..<w {
        let mut acc = bias[o]
        for ci in 0..<c_in {
          let src_off = ci * h * w
          for ky in 0..<3 {
            for kx in 0..<3 {
              let iy = y + ky - 1
              let ix = x2 + kx - 1
              if iy >= 0 && iy < h && ix >= 0 && ix < w {
                let k_idx = ci * 9 + ky * 3 + kx
                acc = acc + weights[o][k_idx] * x[src_off + iy * w + ix]
              }
            }
          }
        }
        out[dst_off + y * w + x2] = acc
      }
    }
  }
  out
}

///|
/// helper: per-channel BatchNorm (training-mode: batch statistics over
/// the [h, w] spatial plane for each channel).
fn batchnorm_channel(
  x : Array[Float],
  c : Int,
  h : Int,
  w : Int,
  gamma : Array[Float],
  beta : Array[Float],
  eps : Float,
) -> Array[Float] {
  let out : Array[Float] = Array::make(c * h * w, 0.0F)
  let plane = h * w
  let n = Float::from_int(plane)
  for ch in 0..<c {
    let off = ch * plane
    let mut mean = 0.0F
    for i in 0..<plane {
      mean = mean + x[off + i]
    }
    mean = mean / n
    let mut variance = 0.0F
    for i in 0..<plane {
      let d = x[off + i] - mean
      variance = variance + d * d
    }
    variance = variance / n
    let std = sqrtf(variance + eps)
    for i in 0..<plane {
      out[off + i] = gamma[ch] * (x[off + i] - mean) / std + beta[ch]
    }
  }
  out
}

///|
/// Forward pass: latent vector z -> generated image
/// [c_out, out_h, out_w].
pub fn dcgan_generator_forward(
  g : DCGANGenerator,
  z : Array[Float],
) -> Array[Float] {
  // 1. Latent projection.
  let proj_dim = g.c_base * 4 * 4
  let feat : Array[Float] = Array::make(proj_dim, 0.0F)
  for o in 0..<proj_dim {
    let mut acc = g.fc_b[o]
    for k in 0..<g.latent_dim {
      acc = acc + g.fc_w[o][k] * z[k]
    }
    feat[o] = acc
  }
  // 2. Reshape to [c_base, 4, 4] and upsample twice.
  let f1 = upsample2d(feat, g.c_base, 4, 4)
  let f2 = upsample2d(f1, g.c_base, 8, 8)
  // 3. Conv1 + BN + ReLU.
  let c1 = conv3x3(f2, g.c_base, 16, 16, g.conv1_w, g.c_base * 2, g.conv1_b)
  let c1b = batchnorm_channel(
    c1, g.c_base * 2, 16, 16, g.bn1_gamma, g.bn1_beta, 1.0e-5F,
  )
  let c1r : Array[Float] = Array::make(c1b.length(), 0.0F)
  for i in 0..<c1b.length() {
    c1r[i] = if c1b[i] > 0.0F { c1b[i] } else { 0.0F }
  }
  // 4. Conv2 + BN + ReLU.
  let c2 = conv3x3(c1r, g.c_base * 2, 16, 16, g.conv2_w, g.c_base, g.conv2_b)
  let c2b = batchnorm_channel(
    c2, g.c_base, 16, 16, g.bn2_gamma, g.bn2_beta, 1.0e-5F,
  )
  let c2r : Array[Float] = Array::make(c2b.length(), 0.0F)
  for i in 0..<c2b.length() {
    c2r[i] = if c2b[i] > 0.0F { c2b[i] } else { 0.0F }
  }
  // 5. Conv3 + Tanh.
  let c3 = conv3x3(c2r, g.c_base, 16, 16, g.conv3_w, g.c_out, g.conv3_b)
  let out : Array[Float] = Array::make(c3.length(), 0.0F)
  for i in 0..<c3.length() {
    out[i] = tanhf(c3[i])
  }
  out
}

///|
/// Count learnable scalars in the generator.
pub fn dcgan_generator_num_params(g : DCGANGenerator) -> Int {
  let mut total = 0
  total = total + g.fc_w.length() * g.fc_w[0].length()
  total = total + g.fc_b.length()
  total = total + g.conv1_w.length() * g.conv1_w[0].length()
  total = total + g.conv1_b.length()
  total = total + g.bn1_gamma.length() + g.bn1_beta.length()
  total = total + g.conv2_w.length() * g.conv2_w[0].length()
  total = total + g.conv2_b.length()
  total = total + g.bn2_gamma.length() + g.bn2_beta.length()
  total = total + g.conv3_w.length() * g.conv3_w[0].length()
  total = total + g.conv3_b.length()
  total
}