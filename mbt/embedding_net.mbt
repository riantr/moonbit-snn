// embedding_net.mbt -- EmbeddingNet for metric learning (v0.129.0).
//
// Metric learning (Bromley et al. 1993; FaceNet 2015) learns a
// function f(x) that maps raw inputs into an embedding space where
// Euclidean (or cosine) distance reflects semantic similarity. All
// loss functions (contrastive, triplet, N-pair) then operate on the
// embedding rather than the raw input.
//
// Scope of v0.129.0:
//   - EmbeddingNet struct: MLP with optional L2 normalisation.
//   - EmbeddingNet::new (xavier-normal init).
//   - embedding_net_forward: x -> embedding.
//   - l2_normalize: project an embedding onto the unit sphere.
//   - pairwise_squared_distance: squared L2 between two embeddings.
//   - cosine_similarity: cosine between two embeddings.
//   - embedding_net_num_params.
//
// Reference: Bromley et al. 1993; FaceNet (Schroff et al. 2015).

///|
/// EmbeddingNet: MLP that maps an input vector of length `input_dim`
/// to an `embed_dim` embedding. When `normalize` is true, the output is
/// L2-normalised so that only the direction matters (the FaceNet
/// convention for cosine-similarity losses).
pub struct EmbeddingNet {
  input_dim : Int
  hidden_dim : Int
  embed_dim : Int
  num_layers : Int
  normalize : Bool
  // Layer 0: input -> hidden.
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // Hidden layers.
  hidden_w : Array[Array[Array[Float]]]
  hidden_b : Array[Array[Float]]
  // Final hidden -> embed_dim.
  w_out : Array[Array[Float]]
  b_out : Array[Float]
}

///|
/// Build a fresh EmbeddingNet. `num_layers` is the number of hidden
/// layers; the total depth is num_layers + 1 (one more for the output
/// projection).
pub fn EmbeddingNet::new(
  input_dim : Int,
  hidden_dim : Int,
  embed_dim : Int,
  num_layers : Int,
  normalize : Bool,
  seed : UInt64,
) -> EmbeddingNet {
  let rng1 = Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  let std1 = sqrtf(2.0F / Float::from_int(input_dim))
  let w1 = xavier_normal(hidden_dim, input_dim, std1, rng1)
  let b1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let hidden_w : Array[Array[Array[Float]]] = Array::make(
    num_layers - 1, Array::make(0, Array::make(0, 0.0F)),
  )
  let hidden_b : Array[Array[Float]] = Array::make(
    num_layers - 1, Array::make(0, 0.0F),
  )
  for l in 0..<(num_layers - 1) {
    let rng_l = Xoshiro::from_state(
      seed + (l + 100).to_uint64() * 7UL,
      seed + (l + 100).to_uint64() * 11UL,
      seed + (l + 100).to_uint64() * 13UL,
      seed + (l + 100).to_uint64() * 17UL,
    )
    let std_l = sqrtf(2.0F / Float::from_int(hidden_dim))
    hidden_w[l] = xavier_normal(hidden_dim, hidden_dim, std_l, rng_l)
    hidden_b[l] = Array::make(hidden_dim, 0.0F)
  }
  let rng_out = Xoshiro::from_state(
    seed + 20UL, seed + 21UL, seed + 22UL, seed + 23UL,
  )
  let std_out = sqrtf(2.0F / Float::from_int(hidden_dim))
  let w_out = xavier_normal(embed_dim, hidden_dim, std_out, rng_out)
  let b_out : Array[Float] = Array::make(embed_dim, 0.0F)
  { input_dim, hidden_dim, embed_dim, num_layers, normalize, w1, b1, hidden_w, hidden_b, w_out, b_out }
}

///|
/// L2-normalise an embedding onto the unit sphere. Returns a fresh
/// array; if the input norm is ~0 the input is returned unchanged (it
/// has no well-defined direction).
pub fn l2_normalize(v : Array[Float]) -> Array[Float] {
  let n = v.length()
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + v[i] * v[i]
  }
  let norm = sqrtf(sum)
  let out : Array[Float] = Array::make(n, 0.0F)
  if norm < 1.0e-12F {
    for i in 0..<n {
      out[i] = v[i]
    }
  } else {
    for i in 0..<n {
      out[i] = v[i] / norm
    }
  }
  out
}

///|
/// Squared Euclidean distance between two embeddings of equal length.
/// Using the squared form (rather than the norm) matches the
/// contrastive-loss convention, where the square is what appears in
/// the gradient.
pub fn pairwise_squared_distance(
  a : Array[Float],
  b : Array[Float],
) -> Float {
  let n = a.length()
  let mut sum = 0.0F
  for i in 0..<n {
    let d = a[i] - b[i]
    sum = sum + d * d
  }
  sum
}

///|
/// Cosine similarity between two embeddings. Returns 0.0 if either
/// vector has ~0 norm.
pub fn cosine_similarity(a : Array[Float], b : Array[Float]) -> Float {
  let n = a.length()
  let mut dot = 0.0F
  let mut na = 0.0F
  let mut nb = 0.0F
  for i in 0..<n {
    dot = dot + a[i] * b[i]
    na = na + a[i] * a[i]
    nb = nb + b[i] * b[i]
  }
  let denom = sqrtf(na) * sqrtf(nb)
  if denom < 1.0e-12F {
    0.0F
  } else {
    dot / denom
  }
}

///|
/// Forward pass: input -> embedding. When the net is configured to
/// normalise, the output is L2-normalised.
pub fn embedding_net_forward(
  net : EmbeddingNet,
  x : Array[Float],
) -> Array[Float] {
  let hidden_dim = net.hidden_dim
  // First Linear + GELU.
  let mut h : Array[Float] = Array::make(hidden_dim, 0.0F)
  for i in 0..<hidden_dim {
    let mut acc = net.b1[i]
    for k in 0..<net.input_dim {
      acc = acc + net.w1[i][k] * x[k]
    }
    h[i] = gelu(acc)
  }
  // Hidden layers with GELU.
  for l in 0..<(net.num_layers - 1) {
    let new_h : Array[Float] = Array::make(hidden_dim, 0.0F)
    for i in 0..<hidden_dim {
      let mut acc = net.hidden_b[l][i]
      for k in 0..<hidden_dim {
        acc = acc + net.hidden_w[l][i][k] * h[k]
      }
      new_h[i] = gelu(acc)
    }
    h = new_h
  }
  // Output projection (no activation).
  let out : Array[Float] = Array::make(net.embed_dim, 0.0F)
  for i in 0..<net.embed_dim {
    let mut acc = net.b_out[i]
    for k in 0..<hidden_dim {
      acc = acc + net.w_out[i][k] * h[k]
    }
    out[i] = acc
  }
  if net.normalize {
    l2_normalize(out)
  } else {
    out
  }
}

///|
/// Count learnable scalars in the embedding network.
pub fn embedding_net_num_params(net : EmbeddingNet) -> Int {
  let mut total = 0
  total = total + net.w1.length() * net.w1[0].length()
  total = total + net.b1.length()
  for l in 0..<net.hidden_w.length() {
    total = total + net.hidden_w[l].length() * net.hidden_w[l][0].length()
    total = total + net.hidden_b[l].length()
  }
  total = total + net.w_out.length() * net.w_out[0].length()
  total = total + net.b_out.length()
  total
}