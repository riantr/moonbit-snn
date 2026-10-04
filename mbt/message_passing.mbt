// message_passing.mbt -- Message passing layers (v0.138.0).
//
// Message Passing Neural Networks (Gilmer et al. 2017) generalise
// every GNN as three steps applied at every node:
//
//   m_v   = AGGREGATE({ MESSAGE(h_u, h_v, e_uv) : u in N(v) })
//   h_v'  = UPDATE(h_v, m_v)
//
// The MPNN formulation factors a GNN into a message function, an
// aggregation function, and an update function, which is why it
// covers GCN, GAT, GraphSAGE and most others as special cases.
//
// Two concrete layers ship here:
//
//   GCNLayer  (Kipf & Welling 2017): h' = act(W · D^-1/2(A+I)D^-1/2 · h)
//   MPnnLayer (Gilmer et al. 2017):  h' = act(W_self · h
//                                               + W_neigh · mean_{u in N(v)} h_u)
//
// Scope of v0.138.0:
//   - GCNLayer struct + gcn_layer_forward (stacked L layers).
//   - MPnnLayer struct + mpnn_layer_forward (stacked L layers).
//   - Linear + ReLU helpers used by both.
//
// Reference: Gilmer et al. 2017; Kipf & Welling 2017.

///|
/// A single Linear layer stored as (out_dim x in_dim) weights.
pub struct GraphLinear {
  in_dim : Int
  out_dim : Int
  w : Array[Array[Float]]
  b : Array[Float]
}

///|
/// Build a Linear layer with xavier-normal initialisation.
pub fn GraphLinear::new(
  in_dim : Int,
  out_dim : Int,
  seed : UInt64,
) -> GraphLinear {
  let std = sqrtf(2.0F / Float::from_int(in_dim))
  let rng = Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  let w = xavier_normal(out_dim, in_dim, std, rng)
  let b : Array[Float] = Array::make(out_dim, 0.0F)
  { in_dim, out_dim, w, b }
}

///|
/// Forward: h [n_nodes x in_dim] -> [n_nodes x out_dim].
pub fn graph_linear_forward(
  lin : GraphLinear,
  h : Array[Float],
  n_nodes : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(n_nodes * lin.out_dim, 0.0F)
  for n in 0..<n_nodes {
    let src = n * lin.in_dim
    let dst = n * lin.out_dim
    for o in 0..<lin.out_dim {
      let mut acc = lin.b[o]
      for k in 0..<lin.in_dim {
        acc = acc + lin.w[o][k] * h[src + k]
      }
      out[dst + o] = acc
    }
  }
  out
}

///|
/// Element-wise ReLU.
pub fn graph_relu(x : Array[Float]) -> Array[Float] {
  let out : Array[Float] = Array::make(x.length(), 0.0F)
  for i in 0..<x.length() {
    out[i] = if x[i] > 0.0F { x[i] } else { 0.0F }
  }
  out
}

///|
/// GCNLayer: one graph convolution with the symmetric normalised
/// adjacency. `alpha` is the GCN self-loop weight (1.0 in the paper).
pub struct GCNLayer {
  in_dim : Int
  out_dim : Int
  alpha : Float
  w : GraphLinear
}

///|
/// Build a GCNLayer.
pub fn GCNLayer::new(
  in_dim : Int,
  out_dim : Int,
  alpha : Float,
  seed : UInt64,
) -> GCNLayer {
  { in_dim, out_dim, alpha, w: GraphLinear::new(in_dim, out_dim, seed), }
}

///|
/// Forward one GCN layer:
///   h' = act( W · ( alpha * h + D^-1/2 A D^-1/2 h ) )
/// The adjacency is normalised once by the caller via
/// `normalise_adjacency`, so this function reads the pre-baked weights
/// from `g.edge_weight`.
pub fn gcn_layer_forward(
  layer : GCNLayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  // Self contribution (alpha * h) plus the weighted neighbour sum.
  // `gcn_layer_support` (gcn_backward.mbt, v0.147.0) owns that
  // computation so the forward and the backward cannot drift apart.
  let support = gcn_layer_support(layer, g, h)
  let projected = graph_linear_forward(layer.w, support, g.n_nodes)
  graph_relu(projected)
}

///|
/// Stack of L GCN layers. The last layer is applied without a ReLU so
/// the output is a usable logit (Kipf & Welling apply the activation
/// to every *hidden* layer only).
pub struct GCN {
  layers : Array[GCNLayer]
  num_layers : Int
  hidden_dim : Int
  out_dim : Int
}

///|
/// Build an L-layer GCN. `dims` must have `num_layers + 1` entries.
pub fn GCN::new(
  dims : Array[Int],
  alpha : Float,
  seed : UInt64,
) -> GCN {
  let num_layers = dims.length() - 1
  let layers : Array[GCNLayer] = Array::make(
    num_layers, GCNLayer::new(dims[0], dims[1], alpha, seed),
  )
  for l in 0..<num_layers {
    layers[l] = GCNLayer::new(
      dims[l], dims[l + 1], alpha, seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  {
    layers,
    num_layers,
    hidden_dim: dims[1],
    out_dim: dims[num_layers],
  }
}

///|
/// Forward the full GCN stack. `g` must already carry the normalised
/// adjacency weights (see `normalise_adjacency`). Every layer applies
/// its ReLU, including the last — callers that need raw logits should
/// read the pre-activation support or strip the final ReLU.
pub fn gcn_forward(
  net : GCN,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let out = gcn_layer_forward(net.layers[0], g, h)
  let mut acc = out
  for l in 1..<net.num_layers {
    acc = gcn_layer_forward(net.layers[l], g, acc)
  }
  acc
}

///|
/// MPnnLayer: the Gilmer message-passing formulation with mean
/// aggregation and a separate self/neighbour weight pair.
pub struct MPnnLayer {
  in_dim : Int
  out_dim : Int
  w_self : GraphLinear
  w_neigh : GraphLinear
}

///|
/// Build an MPnnLayer.
pub fn MPnnLayer::new(
  in_dim : Int,
  out_dim : Int,
  seed : UInt64,
) -> MPnnLayer {
  {
    in_dim,
    out_dim,
    w_self: GraphLinear::new(in_dim, out_dim, seed),
    w_neigh: GraphLinear::new(in_dim, out_dim, seed + 50UL),
  }
}

///|
/// Forward one MPNN layer:
///   h' = act( W_self · h + W_neigh · mean_{u in N(v)} h_u )
/// The UPDATE function is an element-wise sum of two linear maps,
/// which is the GraphSAGE-style update (Hamilton et al. 2017).
pub fn mpnn_layer_forward(
  layer : MPnnLayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let self_out = graph_linear_forward(layer.w_self, h, g.n_nodes)
  // The mean is over the layer's INPUT width, not g.feat_dim -- the
  // two differ for any hidden layer.
  let neigh_mean = scatter_mean_w(g, h, layer.in_dim)
  let neigh_out = graph_linear_forward(layer.w_neigh, neigh_mean, g.n_nodes)
  let out : Array[Float] = Array::make(g.n_nodes * layer.out_dim, 0.0F)
  for i in 0..<out.length() {
    out[i] = graph_relu_one(self_out[i] + neigh_out[i])
  }
  out
}

///|
/// helper: scalar ReLU. Public because the GNN families that add
/// their own terms to a pre-activation array (GIN's MLP, H2GCN's
/// three separated spaces) need the elementwise rule without
/// allocating a whole output array per call.
pub fn graph_relu_one(x : Float) -> Float {
  if x > 0.0F { x } else { 0.0F }
}

///|
/// Stack of L MPNN layers.
pub struct MPnn {
  layers : Array[MPnnLayer]
  num_layers : Int
  out_dim : Int
}

///|
/// Build an L-layer MPNN stack. `dims` must have `num_layers + 1`
/// entries.
pub fn MPnn::new(dims : Array[Int], seed : UInt64) -> MPnn {
  let num_layers = dims.length() - 1
  let layers : Array[MPnnLayer] = Array::make(
    num_layers, MPnnLayer::new(dims[0], dims[1], seed),
  )
  for l in 0..<num_layers {
    layers[l] = MPnnLayer::new(
      dims[l], dims[l + 1], seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  { layers, num_layers, out_dim: dims[num_layers], }
}

///|
/// Forward the full MPNN stack.
pub fn mpnn_forward(
  net : MPnn,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let mut acc = mpnn_layer_forward(net.layers[0], g, h)
  for l in 1..<net.num_layers {
    acc = mpnn_layer_forward(net.layers[l], g, acc)
  }
  acc
}