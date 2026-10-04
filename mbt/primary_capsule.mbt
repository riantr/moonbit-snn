// primary_capsule.mbt -- PrimaryCapsule: convolutional capsules (v0.134.0).
//
// In the original CapsNet (Sabour et al. 2017) the first layer is a
// stack of *primary* capsules. Each primary capsule looks at a
// receptive field in the image (a KxK patch), runs a small convolution
// shared across all spatial positions, and squashes the result into a
// pose vector.
//
//   x [c_in, h, w]
//   -> conv (c_in -> c_out * capsule_dim), KxK, stride K
//   -> reshape to [n_patches, capsule_dim]
//   -> squash each -> [n_patches, capsule_dim]
//   -> add a per-capsule bias (so an empty patch gives p ~ 0)
//
// Sharing the convolution across spatial positions is what keeps the
// parameter count small: one (c_out * capsule_dim x c_in * K * K)
// matrix for every patch.
//
// Scope of v0.134.0:
//   - PrimaryCapsule struct.
//   - PrimaryCapsule::new.
//   - primary_capsule_forward: x -> flat [n_patches x capsule_dim].
//
// Reference: Sabour et al. 2017 (CapsNet, section 3.1).

///|
/// PrimaryCapsule: a bank of convolutional primary capsules.
pub struct PrimaryCapsule {
  c_in : Int
  c_out : Int
  capsule_dim : Int
  kernel : Int
  stride : Int
  in_h : Int
  in_w : Int
  n_patches : Int
  // Shared convolution: (c_out * capsule_dim) rows of
  // (c_in * kernel * kernel) entries.
  w : Array[Array[Float]]
  b : Array[Float]
}

///|
/// Build a fresh PrimaryCapsule. `kernel` is the receptive-field size
/// and `stride` the step; kernel == stride is the standard choice
/// (non-overlapping patches).
pub fn PrimaryCapsule::new(
  c_in : Int,
  c_out : Int,
  capsule_dim : Int,
  kernel : Int,
  stride : Int,
  in_h : Int,
  in_w : Int,
  seed : UInt64,
) -> PrimaryCapsule {
  let in_dim = c_in * kernel * kernel
  let out_dim = c_out * capsule_dim
  let rows : Array[Array[Float]] = Array::make(out_dim, Array::make(in_dim, 0.0F))
  let std = sqrtf(2.0F / Float::from_int(in_dim))
  let rng = Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  for o in 0..<out_dim {
    for i in 0..<in_dim {
      rows[o][i] = std * (next_f32(rng) * 2.0F - 1.0F)
    }
  }
  let b : Array[Float] = Array::make(out_dim, 0.0F)
  let n_patches = ((in_h - kernel) / stride + 1) * ((in_w - kernel) / stride + 1)
  { c_in, c_out, capsule_dim, kernel, stride, in_h, in_w, n_patches, w: rows, b }
}

///|
/// Forward pass: input feature map [c_in, in_h, in_w] -> flat
/// row-major [n_patches x capsule_dim] of squashed capsule outputs.
///
/// Each output channel block of `capsule_dim` channels forms one
/// capsule per spatial position, so a patch produces `c_out`
/// capsules.
pub fn primary_capsule_forward(
  p : PrimaryCapsule,
  x : Array[Float],
) -> Array[Float] {
  let k = p.kernel
  let st = p.stride
  let cd = p.capsule_dim
  let patches_h = (p.in_h - k) / st + 1
  let patches_w = (p.in_w - k) / st + 1
  let n_patches = patches_h * patches_w
  let out_dim = p.c_out * cd
  // Output is [n_patches x out_dim] before squashing per capsule.
  let raw : Array[Float] = Array::make(n_patches * out_dim, 0.0F)
  // Extract each patch and run the shared convolution.
  for py in 0..<patches_h {
    for px in 0..<patches_w {
      let patch_idx = py * patches_w + px
      // Flatten the patch (c_in channels, row-major).
      let patch : Array[Float] = Array::make(
        p.c_in * k * k, 0.0F,
      )
      let mut off = 0
      for c in 0..<p.c_in {
        for dy in 0..<k {
          for dx in 0..<k {
            let iy = py * st + dy
            let ix = px * st + dx
            patch[off] = x[c * p.in_h * p.in_w + iy * p.in_w + ix]
            off = off + 1
          }
        }
      }
      // Shared conv: out_dim outputs.
      let base = patch_idx * out_dim
      for o in 0..<out_dim {
        let mut acc = p.b[o]
        for i in 0..<(p.c_in * k * k) {
          acc = acc + p.w[o][i] * patch[i]
        }
        raw[base + o] = acc
      }
    }
  }
  // Squash each capsule. Capsule c of patch i occupies
  // [base + c*cd, base + (c+1)*cd).
  let out : Array[Float] = Array::make(n_patches * out_dim, 0.0F)
  for i in 0..<n_patches {
    let base = i * out_dim
    for c in 0..<p.c_out {
      let cbase = base + c * cd
      let cap : Array[Float] = Array::make(cd, 0.0F)
      for j in 0..<cd {
        cap[j] = raw[cbase + j]
      }
      let sq = capsule_squash(cap)
      for j in 0..<cd {
        out[cbase + j] = sq.v[j]
      }
    }
  }
  out
}

///|
/// Count learnable scalars in the primary capsule bank.
pub fn primary_capsule_num_params(p : PrimaryCapsule) -> Int {
  p.w.length() * p.w[0].length() + p.b.length()
}