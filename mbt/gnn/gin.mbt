// gin.mbt -- Graph Isomorphism Network (v0.141.0).
//
// Reference: Xu et al. 2019, "How Powerful are Graph Neural
// Networks?", ICLR. GIN's headline result is a *provable
// expressivity guarantee*: a GIN with sum aggregation and a suitable
// MLP is at least as powerful as the 1-Weisfeiler-Leman (1-WL)
// graph-isomorphism test.
//
// The intuition is the opposite of GCN. GCN smooths: a node's
// representation is the (normalised) average of its neighbourhood,
// which makes nearby nodes converge and destroys the ability to
// distinguish structures that differ only in local detail. GIN keeps
// the sum, which preserves the multiset of neighbour labels:
//
//   h_v' = MLP( (1 + eps) * h_v + sum_{u in N(v)} h_u )
//
// The (1 + eps) self term is a *learnable* scalar. Setting eps = 0
// and prepending a self-loop to the edge list is exactly equivalent,
// so the knob exists to let the model adjust the self weight
// continuously during training. A learnable eps is what lets GIN
// match or beat 1-WL; eps = 0 with a self-loop is the same thing
// expressed structurally.
//
// Scope of v0.141.0:
//   - gin_aggregate: unweighted neighbour SUM (not the weighted
//     scatter_sum, because GIN's aggregation must be a plain sum).
//   - GINLayer: 2-layer MLP update with the (1 + eps) self term.
//   - GIN: an L-layer stack built from dims.
//   - gin_forward + gin_num_params.
//
// Scope NOT here: the backward pass. GIN's expressive power is a
// property of the *architecture*; the gradient through the sum is
// mechanically identical to the GCN backward and is deferred to the
// same batch as the GCN/GAT backward.

///|
/// Unweighted sum of the in-neighbour features of every node:
///   out[v] = sum_{u in N(v)} h_u
///
/// This deliberately does NOT reuse `scatter_sum` from graph.mbt.
/// That helper multiplies each message by `g.edge_weight[e]`, which
/// is correct for a normalised GCN adjacency but wrong here: GIN's
/// sum aggregation must be the plain multiset sum, so that a node
/// with two identical neighbours and a node with one neighbour are
/// distinguishable (the 1-WL guarantee depends on it).
///
/// Nodes with in-degree 0 keep a zero row.
pub fn gin_aggregate(g : Graph, h : Array[Float]) -> Array[Float] {
  gin_aggregate_w(g, h, g.feat_dim)
}

///|
/// `gin_aggregate` with an EXPLICIT message width. A GIN layer l > 0
/// receives `dims[l]`-wide activations while `g.feat_dim` is still
/// `dims[0]`, so the layer forward must pass `layer.in_dim` here.
pub fn gin_aggregate_w(
  g : Graph,
  h : Array[Float],
  width : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * width
    let dst_off = d * width
    for k in 0..<width {
      out[dst_off + k] = out[dst_off + k] + h[src_off + k]
    }
  }
  out
}

///|
/// A single GIN layer: sum aggregation, then a 2-layer MLP.
pub struct GINLayer {
  in_dim : Int
  hidden_dim : Int
  out_dim : Int
  /// The learnable self-weight. The layer computes (1 + eps) * h_v,
  /// so eps = 0 means "self counted exactly once".
  eps : Float
  mlp1 : GraphLinear
  mlp2 : GraphLinear
}

///|
/// Build a GINLayer.
pub fn GINLayer::new(
  in_dim : Int,
  hidden_dim : Int,
  out_dim : Int,
  eps : Float,
  seed : UInt64,
) -> GINLayer {
  {
    in_dim,
    hidden_dim,
    out_dim,
    eps,
    mlp1: GraphLinear::new(in_dim, hidden_dim, seed),
    mlp2: GraphLinear::new(hidden_dim, out_dim, seed + 10UL),
  }
}

///|
/// Forward one GIN layer:
///   a_v = (1 + eps) * h_v + sum_{u in N(v)} h_u
///   h_v' = act( W_2 · relu( W_1 · a_v + b_1 ) + b_2 )
pub fn gin_layer_forward(
  layer : GINLayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  // The message width is the layer's INPUT width. Using g.feat_dim
  // here is only correct for layer 0; for a hidden layer the incoming
  // activations are wider and the aggregate would be truncated.
  let dim = layer.in_dim
  let agg = gin_aggregate_w(g, h, dim)
  // Combine self (scaled by 1 + eps) with the neighbour sum.
  let comb_size = g.n_nodes * dim
  let comb : Array[Float] = Array::make(comb_size, 0.0F)
  for i in 0..<comb_size {
    comb[i] = (1.0F + layer.eps) * h[i] + agg[i]
  }
  // 2-layer MLP with ReLU in the middle.
  let hid = graph_linear_forward(layer.mlp1, comb, g.n_nodes)
  let hid_act = graph_relu(hid)
  let out = graph_linear_forward(layer.mlp2, hid_act, g.n_nodes)
  graph_relu(out)
}

///|
/// Number of parameters in one GINLayer.
pub fn gin_layer_num_params(layer : GINLayer) -> Int {
  layer.mlp1.in_dim * layer.mlp1.out_dim + layer.mlp1.out_dim +
  layer.mlp2.in_dim * layer.mlp2.out_dim + layer.mlp2.out_dim
}

///|
/// Stack of L GIN layers.
pub struct GIN {
  layers : Array[GINLayer]
  num_layers : Int
  eps : Float
  out_dim : Int
}

///|
/// Build an L-layer GIN. `dims` must have `num_layers + 1` entries;
/// dims[0] is the input feature width and dims[num_layers] the output
/// width. Each layer's MLP hidden width is set to that layer's output
/// width (the paper's minimum 2-layer MLP); pass a custom
/// `GINLayer::new` if a wider hidden layer is wanted.
pub fn GIN::new(dims : Array[Int], eps : Float, seed : UInt64) -> GIN {
  let num_layers = dims.length() - 1
  let layers : Array[GINLayer] = Array::make(
    num_layers, GINLayer::new(dims[0], dims[1], dims[1], eps, seed),
  )
  for l in 0..<num_layers {
    layers[l] = GINLayer::new(
      dims[l],
      dims[l + 1],
      dims[l + 1],
      eps,
      seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  { layers, num_layers, eps, out_dim: dims[num_layers], }
}

///|
/// Forward the full GIN stack. Every layer applies its ReLU,
/// including the last, so callers that need raw logits should read
/// the last layer's pre-activation output.
pub fn gin_forward(net : GIN, g : Graph, h : Array[Float]) -> Array[Float] {
  let mut acc = gin_layer_forward(net.layers[0], g, h)
  for l in 1..<net.num_layers {
    acc = gin_layer_forward(net.layers[l], g, acc)
  }
  acc
}

///|
/// Total parameter count of the stack.
pub fn gin_num_params(net : GIN) -> Int {
  let mut total = 0
  for l in 0..<net.num_layers {
    total = total + gin_layer_num_params(net.layers[l])
  }
  total
}
