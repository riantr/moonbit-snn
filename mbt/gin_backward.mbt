// gin_backward.mbt -- GIN and MPNN backward passes (v0.146.0).
//
// The first two consumers of v0.145.0's scatter primitives. Both
// architectures have the same shape: a self term, a neighbour term,
// a Linear, and a ReLU. The backward therefore has three distinct
// stages, and the middle one is the one that was previously missing
// from the whole repository -- routing an upstream gradient back
// through the edge list into the *input* embedding.
//
// GIN's forward (v0.141.0):
//   a   = (1 + eps) * h + gin_aggregate(g, h)        <- unweighted sum
//   z1  = W1 . a + b1
//   h'  = ReLU(W2 . ReLU(z1) + b2)
//
// Backward, with G' the ReLU indicator and d_* the upstream grads:
//   d_z2  = G'_out . d_h'
//   (d_a, g2) = linear_backward(W2, d_z2, h1act)
//   d_z1  = G'_mid . d_a
//   (d_w1, g1) = linear_backward(W1, d_z1, comb)
//   d_h   = (1 + eps) * d_a + scatter_sum_backward(g, d_a)
//
// The last line is the whole point: `d_a` is scattered back along the
// edge list, so a node's input gradient is its own share PLUS the
// share of every node that reads it. On a hub that is the dominant
// term, which is exactly why a forward-only GNN cannot be trained
// without it.
//
// MPNN (v0.138.0) is the same with mean instead of sum and two
// Linears summed instead of a 2-layer MLP:
//   h' = ReLU( W_self . h + W_neigh . scatter_mean(g, h) )
// so its neighbour gradient carries the 1/deg factor from
// `scatter_mean_backward`.
//
// No forward cache: both backwards recompute what they need from `h`
// and `g`. The recompute is one extra aggregation pass, which is
// cheaper than the bookkeeping a cache would need across 5+ tensor
// shapes, and it keeps the forward signatures from v0.138.0 / v0.141.0
// untouched (no cache means the forward is still usable for inference
// with no extra allocations).

///|
/// Parameter gradients for one GINLayer.
pub struct GINLayerGrad {
  mlp1 : GraphLinearGrad
  mlp2 : GraphLinearGrad
}

///|
/// Parameter gradients for a whole GIN stack.
pub struct GINGrad {
  layers : Array[GINLayerGrad]
  num_layers : Int
}

///|
/// Zero-initialised GIN gradients for a stack.
pub fn GINGrad::zero(net : GIN) -> GINGrad {
  let layers : Array[GINLayerGrad] = Array::make(
    net.num_layers,
    GINLayerGrad::{
      mlp1: GraphLinearGrad::zero(net.layers[0].mlp1),
      mlp2: GraphLinearGrad::zero(net.layers[0].mlp2),
    },
  )
  for l in 0..<net.num_layers {
    layers[l] = GINLayerGrad::{
      mlp1: GraphLinearGrad::zero(net.layers[l].mlp1),
      mlp2: GraphLinearGrad::zero(net.layers[l].mlp2),
    }
  }
  { layers, num_layers: net.num_layers, }
}

///|
/// helper: recompute a GIN layer's pre-activation intermediates.
/// Returns (comb, mid) where `comb` is the aggregate fed to the first
/// Linear and `mid` is the post-ReLU hidden (the input to the second).
fn gin_layer_intermediates(
  layer : GINLayer,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Float]) {
  let dim = g.feat_dim
  let agg = gin_aggregate(g, h)
  let comb_size = g.n_nodes * dim
  let comb : Array[Float] = Array::make(comb_size, 0.0F)
  for i in 0..<comb_size {
    comb[i] = (1.0F + layer.eps) * h[i] + agg[i]
  }
  let z1 = graph_linear_forward(layer.mlp1, comb, g.n_nodes)
  let mid : Array[Float] = Array::make(z1.length(), 0.0F)
  for i in 0..<z1.length() {
    mid[i] = graph_relu_one(z1[i])
  }
  (comb, mid)
}

///|
/// helper: the ReLU derivative mask for an array of pre-activations.
/// 1 where the pre-activation was positive, 0 elsewhere. ReLU is
/// non-differentiable at exactly 0; the forward's `x > 0` test makes
/// the subgradient 0 there, which is what this reproduces.
fn relu_mask(pre : Array[Float]) -> Array[Float] {
  let m : Array[Float] = Array::make(pre.length(), 0.0F)
  for i in 0..<pre.length() {
    m[i] = if pre[i] > 0.0F { 1.0F } else { 0.0F }
  }
  m
}

///|
/// Backward of one GIN layer. Returns `(d_input, grad)`.
pub fn gin_layer_backward(
  layer : GINLayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> (Array[Float], GINLayerGrad) {
  let (comb, mid) = gin_layer_intermediates(layer, g, h)
  // Recompute the pre-activations for the derivative masks.
  let z2_pre = graph_linear_forward(layer.mlp2, mid, g.n_nodes)
  let z1_pre = graph_linear_forward(layer.mlp1, comb, g.n_nodes)
  // Stage 3: second Linear.
  let d_z2 = elem_mul(d_out, relu_mask(z2_pre))
  let (d_mid, g2) = graph_linear_backward(layer.mlp2, d_z2, g.n_nodes, mid)
  // Stage 2: first Linear, through its ReLU.
  let d_z1 = elem_mul(d_mid, relu_mask(z1_pre))
  let (d_comb, g1) = graph_linear_backward(layer.mlp1, d_z1, g.n_nodes, comb)
  // Stage 1: the self/neighbour split, and the edge-list scatter.
  let d_from_agg = scatter_sum_backward(g, d_comb)
  let dim = g.feat_dim
  let d_input : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for i in 0..<d_input.length() {
    d_input[i] = (1.0F + layer.eps) * d_comb[i] + d_from_agg[i]
  }
  (d_input, GINLayerGrad::{ mlp1: g1, mlp2: g2, })
}

///|
/// Forward the stack, caching each layer's input so the backward can
/// walk the stack without re-deriving layer boundaries.
///
/// Returns (output, inputs) where `inputs[l]` is the [n_nodes x width]
/// tensor fed to layer `l`. This is the memory cost a real training
/// loop pays; the convenience wrappers below avoid it.
pub fn gin_forward_with_inputs(
  net : GIN,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Array[Float]]) {
  let inputs : Array[Array[Float]] = Array::make(net.num_layers, [])
  let mut acc = h
  for l in 0..<net.num_layers {
    inputs[l] = acc
    acc = gin_layer_forward(net.layers[l], g, acc)
  }
  (acc, inputs)
}

///|
/// Backward of a whole GIN stack, given the per-layer inputs from
/// `gin_forward_with_inputs`. Returns `(d_input, grads)`.
pub fn gin_backward(
  net : GIN,
  g : Graph,
  inputs : Array[Array[Float]],
  d_out : Array[Float],
) -> (Array[Float], GINGrad) {
  let grads = GINGrad::zero(net)
  let mut d = d_out
  for l in 0..<net.num_layers {
    let (d_prev, gl) = gin_layer_backward(
      net.layers[l], g, inputs[l], d,
    )
    grads.layers[l] = gl
    d = d_prev
  }
  (d, grads)
}

///|
/// Full training step for a GIN: forward, backward, SGD on every
/// layer. Returns (new_net, d_input) so the caller can chain a graph
/// through several models.
pub fn gin_sgd_step(
  net : GIN,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (GIN, Array[Float]) {
  let (out, inputs) = gin_forward_with_inputs(net, g, h)
  let (d_input, grads) = gin_backward(net, g, inputs, d_out)
  ignore(out)
  let layers : Array[GINLayer] = Array::make(net.num_layers, net.layers[0])
  for l in 0..<net.num_layers {
    let gl = grads.layers[l]
    layers[l] = {
      ..net.layers[l],
      mlp1: graph_linear_sgd_step(net.layers[l].mlp1, gl.mlp1, lr),
      mlp2: graph_linear_sgd_step(net.layers[l].mlp2, gl.mlp2, lr),
    }
  }
  let new_net : GIN = { ..net, layers, }
  (new_net, d_input)
}

///|
/// Parameter gradients for one MPnnLayer.
pub struct MPnnLayerGrad {
  w_self : GraphLinearGrad
  w_neigh : GraphLinearGrad
}

///|
/// Parameter gradients for a whole MPNN stack.
pub struct MPnnGrad {
  layers : Array[MPnnLayerGrad]
  num_layers : Int
}

///|
/// Zero-initialised MPNN gradients for a stack.
pub fn MPnnGrad::zero(net : MPnn) -> MPnnGrad {
  let layers : Array[MPnnLayerGrad] = Array::make(
    net.num_layers,
    MPnnLayerGrad::{
      w_self: GraphLinearGrad::zero(net.layers[0].w_self),
      w_neigh: GraphLinearGrad::zero(net.layers[0].w_neigh),
    },
  )
  for l in 0..<net.num_layers {
    layers[l] = MPnnLayerGrad::{
      w_self: GraphLinearGrad::zero(net.layers[l].w_self),
      w_neigh: GraphLinearGrad::zero(net.layers[l].w_neigh),
    }
  }
  { layers, num_layers: net.num_layers, }
}

///|
/// Backward of one MPNN layer. Returns `(d_input, grad)`.
pub fn mpnn_layer_backward(
  layer : MPnnLayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> (Array[Float], MPnnLayerGrad) {
  // Recompute the two pre-ReLU branches.
  let self_pre = graph_linear_forward(layer.w_self, h, g.n_nodes)
  let neigh_mean = scatter_mean(g, h)
  let neigh_pre = graph_linear_forward(layer.w_neigh, neigh_mean, g.n_nodes)
  let dim = g.feat_dim
  let d_input : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  // The two branches sum before the activation, so both receive the
  // same masked upstream gradient.
  let mask = relu_mask(self_pre)
  let d_self_branch = elem_mul(d_out, mask)
  let d_neigh_branch = elem_mul(d_out, mask)
  // d_h from the self branch.
  let (d_from_self, g_self) = graph_linear_backward(
    layer.w_self, d_self_branch, g.n_nodes, h,
  )
  add_into(d_input, d_from_self)
  // d_h from the neighbour branch: two hops -- through the Linear,
  // then back through the mean aggregation.
  let (d_from_neigh_pre, g_neigh) = graph_linear_backward(
    layer.w_neigh, d_neigh_branch, g.n_nodes, neigh_mean,
  )
  let d_from_agg = scatter_mean_backward(g, d_from_neigh_pre)
  add_into(d_input, d_from_agg)
  (d_input, MPnnLayerGrad::{ w_self: g_self, w_neigh: g_neigh, })
}

///|
/// Forward the MPNN stack, caching each layer's input.
pub fn mpnn_forward_with_inputs(
  net : MPnn,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Array[Float]]) {
  let inputs : Array[Array[Float]] = Array::make(net.num_layers, [])
  let mut acc = h
  for l in 0..<net.num_layers {
    inputs[l] = acc
    acc = mpnn_layer_forward(net.layers[l], g, acc)
  }
  (acc, inputs)
}

///|
/// Backward of a whole MPNN stack. Returns `(d_input, grads)`.
pub fn mpnn_backward(
  net : MPnn,
  g : Graph,
  inputs : Array[Array[Float]],
  d_out : Array[Float],
) -> (Array[Float], MPnnGrad) {
  let grads = MPnnGrad::zero(net)
  let mut d = d_out
  for l in 0..<net.num_layers {
    let (d_prev, gl) = mpnn_layer_backward(
      net.layers[l], g, inputs[l], d,
    )
    grads.layers[l] = gl
    d = d_prev
  }
  (d, grads)
}

///|
/// Full training step for an MPNN: forward, backward, SGD on every
/// layer.
pub fn mpnn_sgd_step(
  net : MPnn,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (MPnn, Array[Float]) {
  let (out, inputs) = mpnn_forward_with_inputs(net, g, h)
  let (d_input, grads) = mpnn_backward(net, g, inputs, d_out)
  ignore(out)
  let layers : Array[MPnnLayer] = Array::make(net.num_layers, net.layers[0])
  for l in 0..<net.num_layers {
    let gl = grads.layers[l]
    layers[l] = {
      ..net.layers[l],
      w_self: graph_linear_sgd_step(net.layers[l].w_self, gl.w_self, lr),
      w_neigh: graph_linear_sgd_step(net.layers[l].w_neigh, gl.w_neigh, lr),
    }
  }
  let new_net : MPnn = { ..net, layers, }
  (new_net, d_input)
}
