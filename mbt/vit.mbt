// vit.mbt -- ViT: Vision Transformer full model (v0.111.0).
//
// A standard ViT (Dosovitskiy et al. 2020):
//   1. Split image into patches and linearly embed each patch (via
//      PatchEmbedding).
//   2. Prepend a learnable [CLS] token.
//   3. Add learnable positional embeddings (one per patch + one for CLS).
//   4. Pass the resulting sequence through N ViTBlocks.
//   5. Read out the CLS token's representation and feed it through a
//      classification head (Linear d_model -> num_classes).
//
// Scope of v0.111.0:
//   - ViT struct (PatchEmbedding + CLS + positional + N blocks + head).
//   - ViT::new (constructor).
//   - vit_forward: image [C*H*W] -> logits [num_classes].
//   - vit_classify: argmax of logits -> Int.
//   - vit_num_params: count of learnable scalars.
//
// Reference: Dosovitskiy et al. 2020 "An Image is Worth 16x16 Words:
// Transformers for Image Recognition at Scale".

///|
/// ViT: full Vision Transformer. The CLS token is prepended to the
/// patch tokens, and positional embeddings are added in-place before
/// the stack of ViTBlocks.
pub struct ViT {
  patch_embed : PatchEmbedding
  cls_token : Array[Float]
  pos_embed : Array[Float]
  blocks : Array[ViTBlock]
  head_w : Array[Array[Float]]
  head_b : Array[Float]
  num_classes : Int
  d_model : Int
  n_patches : Int
}

///|
/// Build a fresh ViT with random init for all learnable parameters.
/// `patch_size`, `d_model`, `num_heads`, `mlp_hidden`, `num_blocks`,
/// `num_classes` are the canonical ViT hyperparameters.
pub fn ViT::new(
  c : Int,
  h : Int,
  w : Int,
  p : Int,
  d_model : Int,
  num_heads : Int,
  mlp_hidden : Int,
  num_blocks : Int,
  num_classes : Int,
  seed : UInt64,
) -> ViT {
  let patch_embed = PatchEmbedding::new(c, h, w, p, d_model, seed)
  let cls_token : Array[Float] = Array::make(d_model, 0.0F)
  let rng_cls = Xoshiro::from_state(
    seed + 100UL, seed + 101UL, seed + 102UL, seed + 103UL,
  )
  let std_cls = sqrtf(2.0F / Float::from_int(d_model))
  for i in 0..<d_model {
    cls_token[i] = std_cls * (next_f32(rng_cls) * 2.0F - 1.0F)
  }
  let n_patches = patch_embed.n_patches
  // Positional embeddings: (n_patches + 1) tokens, each of dim d_model.
  let seq_len = n_patches + 1
  let pos_embed : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  let rng_pos = Xoshiro::from_state(
    seed + 200UL, seed + 201UL, seed + 202UL, seed + 203UL,
  )
  let std_pos = sqrtf(2.0F / Float::from_int(d_model))
  for i in 0..<(seq_len * d_model) {
    pos_embed[i] = std_pos * (next_f32(rng_pos) * 2.0F - 1.0F)
  }
  // Stack of ViTBlocks.
  let blocks : Array[ViTBlock] = Array::make(num_blocks, ViTBlock::new(
    d_model, num_heads, mlp_hidden, seed,
  ))
  for i in 0..<num_blocks {
    blocks[i] = ViTBlock::new(d_model, num_heads, mlp_hidden, seed + (i + 1).to_uint64() * 1000UL)
  }
  // Classification head.
  let rng_head = Xoshiro::from_state(
    seed + 300UL, seed + 301UL, seed + 302UL, seed + 303UL,
  )
  let std_head = sqrtf(2.0F / Float::from_int(d_model))
  let head_w = xavier_normal(num_classes, d_model, std_head, rng_head)
  let head_b : Array[Float] = Array::make(num_classes, 0.0F)
  { patch_embed, cls_token, pos_embed, blocks, head_w, head_b, num_classes, d_model, n_patches }
}

///|
/// ViT forward pass. Input image is flat row-major [C x H x W].
/// Returns the logits array of length `num_classes`.
pub fn vit_forward(vit : ViT, image : Array[Float]) -> Array[Float] {
  let d_model = vit.d_model
  let n_patches = vit.n_patches
  let seq_len = n_patches + 1
  // 1. Patch embedding.
  let patch_tokens = patch_embed_image(vit.patch_embed, image)
  // 2. Prepend CLS token.
  let tokens : Array[Float] = Array::make(seq_len * d_model, 0.0F)
  for i in 0..<d_model {
    tokens[i] = vit.cls_token[i]
  }
  for n in 0..<n_patches {
    let src = n * d_model
    let dst = (n + 1) * d_model
    for i in 0..<d_model {
      tokens[dst + i] = patch_tokens[src + i]
    }
  }
  // 3. Add positional embeddings.
  for i in 0..<(seq_len * d_model) {
    tokens[i] = tokens[i] + vit.pos_embed[i]
  }
  // 4. Stack of ViTBlocks.
  let mut h = tokens
  for b in 0..<vit.blocks.length() {
    h = vit_block_forward(vit.blocks[b], h, seq_len)
  }
  // 5. Read out CLS token (index 0) and apply classification head.
  let cls_repr : Array[Float] = Array::make(d_model, 0.0F)
  for i in 0..<d_model {
    cls_repr[i] = h[i]
  }
  let logits : Array[Float] = Array::make(vit.num_classes, 0.0F)
  for k in 0..<vit.num_classes {
    let mut a = vit.head_b[k]
    for i in 0..<d_model {
      a = a + vit.head_w[k][i] * cls_repr[i]
    }
    logits[k] = a
  }
  logits
}

///|
/// Argmax over logits. Returns the index of the highest-scoring class.
pub fn vit_classify(vit : ViT, image : Array[Float]) -> Int {
  let logits = vit_forward(vit, image)
  let mut best_idx = 0
  let mut best_val = logits[0]
  for k in 1..<vit.num_classes {
    if logits[k] > best_val {
      best_val = logits[k]
      best_idx = k
    }
  }
  best_idx
}

///|
/// Count of learnable scalars in the model. Useful for capacity
/// reporting and as a sanity check on training-step magnitude.
pub fn vit_num_params(vit : ViT) -> Int {
  let mut total = 0
  // PatchEmbedding: d_model * C * P * P + d_model (bias).
  let pe = vit.patch_embed
  total = total + pe.proj.length() * pe.proj[0].length()
  total = total + pe.bias.length()
  // CLS + positional.
  total = total + vit.cls_token.length()
  total = total + vit.pos_embed.length()
  // Per ViTBlock: 4*d_model (LN) + MHA internals + 2*mlp_hidden*d_model (W1) +
  // mlp_hidden (b1) + 2*d_model*mlp_hidden (W2) + d_model (b2).
  for b in 0..<vit.blocks.length() {
    let block = vit.blocks[b]
    total = total + 4 * block.d_model
    // MHA params (4 * d_model * d_model for Wq/Wk/Wv/Wo).
    total = total + 4 * block.d_model * block.d_model
    total = total + block.mlp_w1.length() * block.mlp_w1[0].length()
    total = total + block.mlp_b1.length()
    total = total + block.mlp_w2.length() * block.mlp_w2[0].length()
    total = total + block.mlp_b2.length()
  }
  // Classification head.
  total = total + vit.head_w.length() * vit.head_w[0].length()
  total = total + vit.head_b.length()
  total
}
