// pna_backward.mbt -- PNA backward pass and the shared GNN training
// loop (v0.148.0).
//
// PNA is the fourth and last of the Batch X architectures to get a
// backward. It is the interesting one, because it is the only
// aggregation here whose derivative is neither a scatter (sum, mean)
// nor a routing (max, min): the standard deviation's derivative is
//
//   d sigma_i / d x_j = (x_j - mean_i) / (deg_i * sigma_i)
//
// for every neighbour j of node i. Every incoming edge therefore
// contributes a share of the destination's std gradient, but the
// share depends on that edge's own message, not just on a degree
// count. Where sigma_i == 0 the derivative does not exist (a constant
// neighbourhood is a flat function); the backward returns 0 there,
// which is the conventional choice and keeps the training step from
// producing a NaN.
//
// PNA's aggregators are UNWEIGHTED (they reduce a neighbour multiset,
// not a normalised adjacency), so this file ships its own mean /
// max / min backwards rather than the `scatter_*_backward` family in
// graph_backward.mbt, which multiplies by `edge_weight[e]`. Using the
// weighted versions here would be a silent per-edge rescale by the
// adjacency normalisation -- invisible on an unweighted graph, wrong
// on a normalised one.
//
// The second half of the file is the payoff: a tagged `TrainableGraphNet`
// and a `graph_net_train_step` that runs loss -> backward -> SGD for
// GCN / GIN / PNA / MPNN behind one signature. Node-level
// cross-entropy is the objective, which is the standard node
// classification task (Cora / Citeseer) that GCN was invented for.

///|
/// Backward of `pna_mean`: the unweighted analogue of
/// `scatter_mean_backward`, with no `edge_weight` factor.
pub fn pna_mean_backward(g : Graph, d_out : Array[Float]) -> Array[Float] {
  pna_mean_backward_w(g, d_out, g.feat_dim)
}

///|
/// `pna_mean_backward` at an EXPLICIT message width.
pub fn pna_mean_backward_w(
  g : Graph,
  d_out : Array[Float],
  width : Int,
) -> Array[Float] {
  let deg = graph_in_degree(g)
  let out : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    if deg[d] == 0 {
      continue
    }
    let scale = 1.0F / Float::from_int(deg[d])
    let src_off = s * width
    let dst_off = d * width
    for k in 0..<width {
      out[src_off + k] = out[src_off + k] + scale * d_out[dst_off + k]
    }
  }
  out
}

///|
/// Backward of `pna_max`: route the gradient to the winning edge only,
/// with no `edge_weight` factor. `argmax` must come from
/// `pna_max_forward_with_idx` on the SAME features.
pub fn pna_max_backward(
  g : Graph,
  d_out : Array[Float],
  argmax : Array[Int],
) -> Array[Float] {
  pna_max_backward_w(g, d_out, argmax, g.feat_dim)
}

///|
/// `pna_max_backward` at an EXPLICIT message width.
pub fn pna_max_backward_w(
  g : Graph,
  d_out : Array[Float],
  argmax : Array[Int],
  width : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for d in 0..<g.n_nodes {
    let dst_off = d * width
    for k in 0..<width {
      let e = argmax[dst_off + k]
      if e < 0 || e >= g.n_edges {
        continue
      }
      let s = g.edge_src[e]
      if s < 0 || s >= g.n_nodes {
        continue
      }
      let src_off = s * width
      out[src_off + k] = out[src_off + k] + d_out[dst_off + k]
    }
  }
  out
}

///|
/// Backward of `pna_min`. Structurally identical to
/// `pna_max_backward` -- only the recorded argmin differs.
pub fn pna_min_backward(
  g : Graph,
  d_out : Array[Float],
  argmin : Array[Int],
) -> Array[Float] {
  pna_max_backward_w(g, d_out, argmin, g.feat_dim)
}

///|
/// `pna_min_backward` at an EXPLICIT message width.
pub fn pna_min_backward_w(
  g : Graph,
  d_out : Array[Float],
  argmin : Array[Int],
  width : Int,
) -> Array[Float] {
  pna_max_backward_w(g, d_out, argmin, width)
}

///|
/// Backward of `pna_std`.
///
///   d sigma_i / d x_j = (x_j - mean_i) / (deg_i * sigma_i)
///
/// `mean` and `sigma` are the per-node values from the forward pass;
/// they can be recomputed with `pna_mean` / `pna_std` on the same `h`.
/// Nodes with deg == 0 or sigma == 0 get a zero contribution, because
/// the derivative is undefined there (a constant neighbourhood is a
/// flat function) and dividing by it would emit a NaN.
pub fn pna_std_backward(
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  mean : Array[Float],
  sigma : Array[Float],
) -> Array[Float] {
  let width = if g.n_nodes == 0 {
    0
  } else {
    mean.length() / g.n_nodes
  }
  pna_std_backward_w(g, h, d_out, mean, sigma, width)
}

///|
/// `pna_std_backward` at an EXPLICIT message width.
pub fn pna_std_backward_w(
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  mean : Array[Float],
  sigma : Array[Float],
  width : Int,
) -> Array[Float] {
  let deg = graph_in_degree(g)
  let out : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    if deg[d] == 0 {
      continue
    }
    let src_off = s * width
    let dst_off = d * width
    let inv = 1.0F / Float::from_int(deg[d])
    for k in 0..<width {
      let sdst = sigma[dst_off + k]
      if sdst <= 0.0F {
        continue
      }
      let x_j = h[src_off + k]
      let mu = mean[dst_off + k]
      out[src_off + k] = out[src_off + k] +
        d_out[dst_off + k] * (x_j - mu) * inv / sdst
    }
  }
  out
}

///|
/// Parameter gradients for one PNALayer.
pub struct PNALayerGrad {
  pre_lin : GraphLinearGrad
  post_lin : GraphLinearGrad
}

///|
/// Parameter gradients for a whole PNA stack.
pub struct PNAGrad {
  layers : Array[PNALayerGrad]
  num_layers : Int
}

///|
/// Zero-initialised PNA gradients for a stack.
pub fn PNAGrad::zero(net : PNA) -> PNAGrad {
  let layers : Array[PNALayerGrad] = Array::make(
    net.num_layers,
    PNALayerGrad::{
      pre_lin: GraphLinearGrad::zero(net.layers[0].pre_lin),
      post_lin: GraphLinearGrad::zero(net.layers[0].post_lin),
    },
  )
  for l in 0..<net.num_layers {
    layers[l] = PNALayerGrad::{
      pre_lin: GraphLinearGrad::zero(net.layers[l].pre_lin),
      post_lin: GraphLinearGrad::zero(net.layers[l].post_lin),
    }
  }
  { layers, num_layers: net.num_layers, }
}

///|
/// helper: recompute the PNA layer's concat tensor and the forward
/// statistics the backward needs (per-feature degree scales, the
/// per-aggregator argmax, and the mean / sigma for the std block).
fn pna_layer_backward_state(
  layer : PNALayer,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Float], Array[Int], Array[Int], Array[Float], Array[Float]) {
  let dim = layer.in_dim
  let n_agg = pna_num_aggregators()
  let mean = pna_mean_w(g, h, dim)
  let (mx, argmax) = pna_max_forward_with_idx_w(g, h, dim)
  let (mn, argmin) = pna_min_forward_with_idx_w(g, h, dim)
  let sigma = pna_std_w(g, h, dim)
  let aggs = [mean, mx, mn, sigma]
  let in_deg = graph_in_degree(g)
  let mean_deg = pna_mean_in_degree(g, layer.delta)
  let scale_size = g.n_nodes
  let scales : Array[Float] = Array::make(scale_size, 1.0F)
  for v in 0..<g.n_nodes {
    scales[v] = pna_degree_scale(layer.scaler, in_deg[v], mean_deg)
  }
  let concat_dim = dim * (n_agg + 1)
  let z : Array[Float] = Array::make(g.n_nodes * concat_dim, 0.0F)
  for v in 0..<g.n_nodes {
    let sc = scales[v]
    let src_off = v * dim
    let dst_off = v * concat_dim
    for k in 0..<dim {
      z[dst_off + k] = sc * h[src_off + k]
    }
    for a in 0..<n_agg {
      let base = dst_off + dim * (a + 1)
      for k in 0..<dim {
        z[base + k] = sc * aggs[a][src_off + k]
      }
    }
  }
  (z, scales, argmax, argmin, mean, sigma)
}

///|
/// Backward of one PNA layer. Returns `(d_input, grad)`.
pub fn pna_layer_backward(
  layer : PNALayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> (Array[Float], PNALayerGrad) {
  // MUST be the layer's input width, matching
  // `pna_layer_backward_state`. Leaving this as `g.feat_dim` sizes the
  // d_input buffer and the reducer-gradient stride to the NODE feature
  // width, which for a hidden layer under-allocates it; the previous
  // layer then receives a gradient that is too narrow and reads out of
  // bounds one layer up. That is exactly the PanicError this fixed.
  let dim = layer.in_dim
  let n_agg = pna_num_aggregators()
  let (z, scales, argmax, argmin, mean, sigma) =
    pna_layer_backward_state(layer, g, h)
  // Recompute both pre-activations for the ReLU masks.
  let z1_pre = graph_linear_forward(layer.pre_lin, z, g.n_nodes)
  let mid : Array[Float] = Array::make(z1_pre.length(), 0.0F)
  for i in 0..<z1_pre.length() {
    mid[i] = graph_relu_one(z1_pre[i])
  }
  let z2_pre = graph_linear_forward(layer.post_lin, mid, g.n_nodes)
  let d_pre1 : Array[Float] = Array::make(z2_pre.length(), 0.0F)
  for i in 0..<z2_pre.length() {
    d_pre1[i] = if z2_pre[i] > 0.0F { d_out[i] } else { 0.0F }
  }
  // Backward through the post Linear.
  let (d_mid, g_post) = graph_linear_backward(
    layer.post_lin, d_pre1, g.n_nodes, mid,
  )
  // Backward through the pre Linear + its ReLU.
  let d_z1 : Array[Float] = Array::make(d_mid.length(), 0.0F)
  for i in 0..<d_mid.length() {
    d_z1[i] = if z1_pre[i] > 0.0F { d_mid[i] } else { 0.0F }
  }
  let (d_z, g_pre) = graph_linear_backward(
    layer.pre_lin, d_z1, g.n_nodes, z,
  )
  // Split d_z into its five blocks, undo the degree scaling, and
  // route each reducer's share back to the input embeddings.
  let concat_dim = dim * (n_agg + 1)
  // Each reducer's gradient is a FULL [n_nodes x dim] tensor, because
  // the forward aggregate `aggs[a]` is per-node. Writing a single
  // node's row into a `dim`-wide buffer and overwriting it per v
  // silently keeps only the last node's share.
  let per_node = g.n_nodes * dim
  let d_mean : Array[Float] = Array::make(per_node, 0.0F)
  let d_max : Array[Float] = Array::make(per_node, 0.0F)
  let d_min : Array[Float] = Array::make(per_node, 0.0F)
  let d_std : Array[Float] = Array::make(per_node, 0.0F)
  for v in 0..<g.n_nodes {
    let sc = scales[v]
    let src_off = v * dim
    let dst_off = v * concat_dim
    for k in 0..<dim {
      d_mean[src_off + k] = sc * d_z[dst_off + dim + k]
      d_max[src_off + k] = sc * d_z[dst_off + 2 * dim + k]
      d_min[src_off + k] = sc * d_z[dst_off + 3 * dim + k]
      d_std[src_off + k] = sc * d_z[dst_off + 4 * dim + k]
    }
  }
  // The self block is a direct (scaled) copy to the node's own row.
  let d_input : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for v in 0..<g.n_nodes {
    let sc = scales[v]
    let off = v * dim
    let zoff = v * concat_dim
    for k in 0..<dim {
      d_input[off + k] = sc * d_z[zoff + k]
    }
  }
  add_into(d_input, pna_mean_backward_w(g, d_mean, dim))
  add_into(d_input, pna_max_backward_w(g, d_max, argmax, dim))
  add_into(d_input, pna_min_backward_w(g, d_min, argmin, dim))
  add_into(d_input, pna_std_backward_w(g, h, d_std, mean, sigma, dim))
  (d_input, PNALayerGrad::{ pre_lin: g_pre, post_lin: g_post, })
}

///|
/// Forward the PNA stack, caching each layer's input.
pub fn pna_forward_with_inputs(
  net : PNA,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Array[Float]]) {
  let inputs : Array[Array[Float]] = Array::make(net.num_layers, [])
  let mut acc = h
  for l in 0..<net.num_layers {
    inputs[l] = acc
    acc = pna_layer_forward(net.layers[l], g, acc)
  }
  (acc, inputs)
}

///|
/// Backward of a whole PNA stack. Returns `(d_input, grads)`.
/// REVERSE layer order, for the reason given on `gin_backward`.
pub fn pna_backward(
  net : PNA,
  g : Graph,
  inputs : Array[Array[Float]],
  d_out : Array[Float],
) -> (Array[Float], PNAGrad) {
  let grads = PNAGrad::zero(net)
  let mut d = d_out
  for i in 0..<net.num_layers {
    let l = net.num_layers - 1 - i
    let (d_prev, gl) = pna_layer_backward(
      net.layers[l], g, inputs[l], d,
    )
    grads.layers[l] = gl
    d = d_prev
  }
  (d, grads)
}

///|
/// Full training step for a PNA: forward, backward, SGD on every
/// layer.
pub fn pna_sgd_step(
  net : PNA,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (PNA, Array[Float]) {
  let (out, inputs) = pna_forward_with_inputs(net, g, h)
  let (d_input, grads) = pna_backward(net, g, inputs, d_out)
  ignore(out)
  let layers : Array[PNALayer] = Array::make(net.num_layers, net.layers[0])
  for l in 0..<net.num_layers {
    let gl = grads.layers[l]
    layers[l] = {
      ..net.layers[l],
      pre_lin: graph_linear_sgd_step(
        net.layers[l].pre_lin, gl.pre_lin, lr,
      ),
      post_lin: graph_linear_sgd_step(
        net.layers[l].post_lin, gl.post_lin, lr,
      ),
    }
  }
  let new_net : PNA = { ..net, layers, }
  (new_net, d_input)
}

///|
/// A trainable message-passing network, dispatched by tag. One
/// training-loop signature therefore covers all four backward-enabled
/// architectures.
pub(all) enum TrainableGraphNet {
  Gcn(GCN)
  Gin(GIN)
  Pna(PNA)
  Mpnn(MPnn)
}

///|
/// Forward a tagged trainable network to node embeddings.
pub fn trainable_net_forward(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  match net {
    Gcn(m) => gcn_forward(m, g, h)
    Gin(m) => gin_forward(m, g, h)
    Pna(m) => pna_forward(m, g, h)
    Mpnn(m) => mpnn_forward(m, g, h)
  }
}

///|
/// Node-embedding width of a tagged network.
pub fn trainable_net_node_dim(net : TrainableGraphNet) -> Int {
  match net {
    Gcn(m) => m.out_dim
    Gin(m) => m.out_dim
    Pna(m) => m.out_dim
    Mpnn(m) => m.out_dim
  }
}

///|
/// Parameter count of a tagged network.
pub fn trainable_net_num_params(net : TrainableGraphNet) -> Int {
  match net {
    Gcn(m) => {
      let mut t = 0
      for l in 0..<m.num_layers {
        t = t + graph_linear_num_params(m.layers[l].w)
      }
      t
    }
    Gin(m) => gin_num_params(m)
    Pna(m) => pna_num_params(m)
    Mpnn(m) => {
      let mut t = 0
      for l in 0..<m.num_layers {
        t = t + graph_linear_num_params(m.layers[l].w_self) +
        graph_linear_num_params(m.layers[l].w_neigh)
      }
      t
    }
  }
}

///|
/// Forward + backward + SGD for a tagged network. Returns
/// `(new_net, d_input)`.
pub fn trainable_net_sgd_step(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (TrainableGraphNet, Array[Float]) {
  match net {
    Gcn(m) => {
      let (m2, d) = gcn_sgd_step(m, g, h, d_out, lr)
      (Gcn(m2), d)
    }
    Gin(m) => {
      let (m2, d) = gin_sgd_step(m, g, h, d_out, lr)
      (Gin(m2), d)
    }
    Pna(m) => {
      let (m2, d) = pna_sgd_step(m, g, h, d_out, lr)
      (Pna(m2), d)
    }
    Mpnn(m) => {
      let (m2, d) = mpnn_sgd_step(m, g, h, d_out, lr)
      (Mpnn(m2), d)
    }
  }
}

///|
/// Mean cross-entropy over nodes, where every node carries its own
/// per-node logit row. `logits` is [n_nodes x num_classes].
pub fn node_ce_loss(
  logits : Array[Float],
  labels : Array[Int],
  n_nodes : Int,
  num_classes : Int,
) -> Float {
  if n_nodes == 0 {
    return 0.0F
  }
  let mut total = 0.0F
  for v in 0..<n_nodes {
    let off = v * num_classes
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    for k in 0..<num_classes {
      row[k] = logits[off + k]
    }
    let y = if v < labels.length() { labels[v] } else { 0 }
    total = total + graph_cross_entropy(row, y)
  }
  total / Float::from_int(n_nodes)
}

///|
/// Gradient of `node_ce_loss` with respect to the node logits:
/// d_logits[v][k] = (softmax(logits_v)[k] - onehot(labels_v)[k]) / n_nodes.
///
/// The 1/n_nodes is the mean's contribution and must be present or the
/// effective learning rate scales with the node count.
pub fn node_ce_grad(
  logits : Array[Float],
  labels : Array[Int],
  n_nodes : Int,
  num_classes : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(n_nodes * num_classes, 0.0F)
  if n_nodes == 0 {
    return out
  }
  let inv = 1.0F / Float::from_int(n_nodes)
  for v in 0..<n_nodes {
    let off = v * num_classes
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    for k in 0..<num_classes {
      row[k] = logits[off + k]
    }
    let y = if v < labels.length() { labels[v] } else { 0 }
    let grow = graph_cross_entropy_grad(row, y)
    for k in 0..<num_classes {
      out[off + k] = grow[k] * inv
    }
  }
  out
}

///|
/// One node-classification training step: forward, mean
/// cross-entropy, backward, SGD. Returns `(new_net, loss, d_input)`.
///
/// The node logits are the backbone's own output, so the backbone's
/// `out_dim` must equal `num_classes`. For a wider backbone, chain a
/// `GraphLinear` on top of `d_input`.
pub fn graph_net_train_step(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
  lr : Float,
) -> (TrainableGraphNet, Float, Array[Float]) {
  let logits = trainable_net_forward(net, g, h)
  let loss = node_ce_loss(logits, labels, g.n_nodes, num_classes)
  let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
  let (new_net, d_input) = trainable_net_sgd_step(net, g, h, d_logits, lr)
  (new_net, loss, d_input)
}

///|
/// One graph-classification training step: forward, mean readout, mean
/// cross-entropy on the pooled graph embedding, backward, SGD.
///
/// The readout is a mean pool over node embeddings, so the gradient
/// reaching the backbone is a scatter of the graph-level gradient back
/// to every node -- which is why the loss over one pooled vector
/// trains every node in the graph.
pub fn graph_net_graph_train_step(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  target : Int,
  num_classes : Int,
  lr : Float,
) -> (TrainableGraphNet, Float, Array[Float]) {
  let node_emb = trainable_net_forward(net, g, h)
  let node_dim = trainable_net_node_dim(net)
  let graph_emb = mean_pool(node_emb, g.n_nodes, node_dim)
  let loss = graph_cross_entropy(graph_emb, target)
  let d_graph = graph_cross_entropy_grad(graph_emb, target)
  // Mean-pool adjoint: every node gets the same share, scaled by 1/n.
  let d_node : Array[Float] = Array::make(g.n_nodes * node_dim, 0.0F)
  if g.n_nodes > 0 {
    let inv = 1.0F / Float::from_int(g.n_nodes)
    for v in 0..<g.n_nodes {
      let off = v * node_dim
      for k in 0..<node_dim {
        d_node[off + k] = d_graph[k] * inv
      }
    }
  }
  let (new_net, d_input) = trainable_net_sgd_step(net, g, h, d_node, lr)
  (new_net, loss, d_input)
}
