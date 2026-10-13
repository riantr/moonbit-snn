// siamese_net.mbt -- SiameseNet with shared weights (v0.131.0).
//
// A Siamese network (Bromley et al. 1993) runs the *same* embedding
// function over two or three inputs and compares the resulting
// embeddings. Weight sharing is what makes the learned metric
// symmetric and reduces the parameter count by a factor of the number
// of inputs.
//
// This file provides the twin-architecture wrapper around
// `EmbeddingNet` (v0.129.0):
//
//   anchor   \
//             >-- SiameseNet (shared EmbeddingNet) --> distances
//   positive /
//   negative /
//
// Scope of v0.131.0:
//   - SiameseNet struct: one shared EmbeddingNet + margin + type.
//   - SiameseNet::new.
//   - siamese_embed: embed one input.
//   - siamese_embed_pair: embed two inputs through the same weights.
//   - siamese_embed_triplet: embed three inputs through the same weights.
//   - siamese_distances: compute the pair/triplet distance matrix.
//   - siamese_num_params (shared weights counted ONCE).

///|
/// Loss type selected for a SiameseNet. Contrastive is pair-based;
/// Triplet is anchor/positive/negative based.
pub(all) enum SiameseLoss {
  Contrastive
  Triplet
} derive(Eq, Debug)

///|
/// SiameseNet: a shared EmbeddingNet plus the inference margin.
pub struct SiameseNet {
  net : EmbeddingNet
  margin : Float
  loss_type : SiameseLoss
}

///|
/// Build a fresh SiameseNet.
pub fn SiameseNet::new(
  net : EmbeddingNet,
  margin : Float,
  loss_type : SiameseLoss,
) -> SiameseNet {
  { net, margin, loss_type }
}

///|
/// Embed a single input through the shared weights.
pub fn siamese_embed(s : SiameseNet, x : Array[Float]) -> Array[Float] {
  embedding_net_forward(s.net, x)
}

///|
/// Embed two inputs through the same weights and return both
/// embeddings. This is the "twin" in siamese.
pub fn siamese_embed_pair(
  s : SiameseNet,
  x1 : Array[Float],
  x2 : Array[Float],
) -> (Array[Float], Array[Float]) {
  let e1 = embedding_net_forward(s.net, x1)
  let e2 = embedding_net_forward(s.net, x2)
  (e1, e2)
}

///|
/// Embed three inputs through the same weights and return all three
/// embeddings. This is the "triplet" variant.
pub fn siamese_embed_triplet(
  s : SiameseNet,
  a : Array[Float],
  p : Array[Float],
  n : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let ea = embedding_net_forward(s.net, a)
  let ep = embedding_net_forward(s.net, p)
  let en = embedding_net_forward(s.net, n)
  (ea, ep, en)
}

///|
/// Compute the pairwise distance matrix (in embedding space) for a
/// batch of `n` already-embedded vectors. Returns a flat row-major
/// [n x n] array of squared distances. This is used to evaluate
/// retrieval quality (e.g. Recall@K) and to build N-pair batches.
pub fn siamese_distances(
  embeddings : Array[Array[Float]],
) -> Array[Float] {
  let n = embeddings.length()
  let out : Array[Float] = Array::make(n * n, 0.0F)
  for i in 0..<n {
    for j in 0..<n {
      out[i * n + j] = pairwise_squared_distance(embeddings[i], embeddings[j])
    }
  }
  out
}

///|
/// Count trainable scalars. The shared weights are counted ONCE, not
/// once per twin — this is the main parameter saving of the siamese
/// architecture over two independent networks.
pub fn siamese_num_params(s : SiameseNet) -> Int {
  embedding_net_num_params(s.net)
}