// patch_embedding.mbt 鈥?PatchEmbedding for Vision Transformer (v0.109.0).
//
// The Vision Transformer (Dosovitskiy et al. 2020) divides an image
// into a grid of fixed-size patches (e.g. 16脳16), linearly embeds
// each patch into a d_model-dimensional token, adds positional
// embeddings, and processes the resulting sequence with a Transformer.
//
// This file ships the `PatchEmbedding` primitive:
//   patch_embed_image(image, embedder)
//     -> tokens flat [n_patches 脳 d_model]
// where image is flat row-major [C 脳 H 脳 W] and patches are extracted
// in raster order. The patch linear projection is the learnable
// parameter (d_model 脳 C 脳 P 脳 P).
//
// Reference: Dosovitskiy et al. 2020 "An Image is Worth 16x16 Words:
// Transformers for Image Recognition at Scale".

///|
/// PatchEmbedding: linear projection from (C 脳 P 脳 P) 鈫?d_model.
/// Input image is divided into (H/P) 脳 (W/P) patches.
pub struct PatchEmbedding {
  c : Int
  h : Int
  w : Int
  p : Int
  d_model : Int
  // Linear projection: (d_model 脳 C 脳 P 脳 P)
  proj : Array[Array[Float]]
  // Per-dim bias.
  bias : Array[Float]
  // Number of patches (n_patches = (h/p) 脳 (w/p)).
  n_patches : Int
}

///|
/// Build a fresh PatchEmbedding with xavier-normal init for the
/// projection weights and zero bias.
pub fn PatchEmbedding::new(
  c : Int,
  h : Int,
  w : Int,
  p : Int,
  d_model : Int,
  seed : UInt64,
) -> PatchEmbedding {
  let patch_dim = c * p * p
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std = sqrtf(2.0F / Float::from_int(patch_dim))
  let proj = xavier_normal(d_model, patch_dim, std, rng)
  let bias : Array[Float] = Array::make(d_model, 0.0F)
  let n_patches = (h / p) * (w / p)
  { c, h, w, p, d_model, proj, bias, n_patches }
}

///|
/// Embed a single patch (length C 脳 P 脳 P) into a d_model token.
/// Returns a fresh Array[Float] of length d_model.
pub fn patch_embed_patch(
  embedder : PatchEmbedding,
  patch : Array[Float],
) -> Array[Float] {
  let token : Array[Float] = Array::make(embedder.d_model, 0.0F)
  for i in 0..<embedder.d_model {
    let mut s = embedder.bias[i]
    for k in 0..<patch.length() {
      s = s + embedder.proj[i][k] * patch[k]
    }
    token[i] = s
  }
  token
}

///|
/// Embed a full image (flat row-major [C 脳 H 脳 W]) into a sequence of
/// patch tokens. Returns a flat row-major
/// `[n_patches 脳 d_model]` array.
pub fn patch_embed_image(
  embedder : PatchEmbedding,
  image : Array[Float],
) -> Array[Float] {
  let tokens : Array[Float] = Array::make(
    embedder.n_patches * embedder.d_model, 0.0F,
  )
  let patch_dim = embedder.c * embedder.p * embedder.p
  let p = embedder.p
  let patches_per_row = embedder.w / p
  for py in 0..<(embedder.h / p) {
    for px in 0..<patches_per_row {
      let patch_idx = py * patches_per_row + px
      // Extract the (px, py) patch from the image.
      // Image layout: [C, H, W] row-major; channel innermost varies
      // fastest. For each pixel (x, y) within the patch, accumulate
      // (C 脳 P 脳 P) elements.
      let patch : Array[Float] = Array::make(patch_dim, 0.0F)
      let mut patch_off = 0
      for c in 0..<embedder.c {
        for dy in 0..<p {
          for dx in 0..<p {
            let x = px * p + dx
            let y = py * p + dy
            let img_off = c * embedder.h * embedder.w + y * embedder.w + x
            patch[patch_off] = image[img_off]
            patch_off = patch_off + 1
          }
        }
      }
      let token = patch_embed_patch(embedder, patch)
      let tok_off = patch_idx * embedder.d_model
      for k in 0..<embedder.d_model {
        tokens[tok_off + k] = token[k]
      }
    }
  }
  tokens
}