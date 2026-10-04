// gcn_backward.mbt -- GCN backward pass (v0.147.0).
//
// GCN (v0.138.0) is the third consumer of the v0.145.0 scatter
// primitives, and it is the one where the WEIGHTS matter. Its forward
// is
//
//   support[v] = alpha * h_v + sum_{e: dst(e)=v} w_e * h_{src(e)}
//   h'         = ReLU( W . support )
//
// so the backward is
//
//   d_support[v] = W^T . (G' . d_h')[v]                    (Linear)
//   d_h[s] += alpha * d_support[s]                          (self)
//   d_h[s] += sum_{e: src(e)=s} w_e * d_support[dst(e)]     (edge)
//
// That last line is `scatter_sum_backward` reading the same
// `edge_weight` array the forward used, which is why the adjacency
// normalisation is a *parameter-free* transformation: baking
// D^-1/2(A+I)D^-1/2 into `edge_weight` means the backward is correct
// with no extra adjoint for the normalisation itself. There is no
// gradient flowing to the degrees, because the degrees are not
// parameters -- they are a function of the (fixed) topology.
//
// The self term is the one place to be careful. `gcn_layer_forward`
// (v0.138.0) computes `alpha * h` IN ADDITION to whatever self-loops
// `normalise_adjacency` already inserted. The backward mirrors the
// forward exactly: `alpha * d_support` to the node's own row, and
// `w_e * d_support[dst]` along every edge. If the caller built the
// graph with `normalise_adjacency` (which adds self-loops) AND left
// alpha at 1.0, the self contribution is counted twice -- in the
// forward and therefore in the backward too. Set alpha = 0.0 when the
// adjacency already carries the self term.

///|
/// Parameter gradients for one GCNLayer.
pub struct GCNLayerGrad {
  w : GraphLinearGrad
}

///|
/// Parameter gradients for a whole GCN stack.
pub struct GCNGrad {
  layers : Array[GCNLayerGrad]
  num_layers : Int
}

///|
/// Zero-initialised GCN gradients for a stack.
pub fn GCNGrad::zero(net : GCN) -> GCNGrad {
  let layers : Array[GCNLayerGrad] = Array::make(
    net.num_layers,
    GCNLayerGrad::{ w: GraphLinearGrad::zero(net.layers[0].w), },
  )
  for l in 0..<net.num_layers {
    layers[l] = GCNLayerGrad::{
      w: GraphLinearGrad::zero(net.layers[l].w),
    }
  }
  { layers, num_layers: net.num_layers, }
}

///|
/// helper: the GCN forward's `support` term, i.e. everything that
/// feeds the Linear. Split out because both the forward derivative
/// mask and the backward's self/edge routing need exactly this
/// tensor, and recomputing it two different ways is how the two drift
/// apart.
pub fn gcn_layer_support(
  layer : GCNLayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let support_size = g.n_nodes * layer.in_dim
  let support : Array[Float] = Array::make(support_size, 0.0F)
  for i in 0..<support_size {
    support[i] = layer.alpha * h[i]
  }
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * layer.in_dim
    let dst_off = d * layer.in_dim
    for k in 0..<layer.in_dim {
      support[dst_off + k] = support[dst_off + k] + w * h[src_off + k]
    }
  }
  support
}

///|
/// Backward of one GCN layer. Returns `(d_input, grad)`.
pub fn gcn_layer_backward(
  layer : GCNLayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> (Array[Float], GCNLayerGrad) {
  let support = gcn_layer_support(layer, g, h)
  // The forward's activation is ReLU, so mask the upstream gradient
  // with the forward's own pre-activation (recomputed from support).
  let pre = graph_linear_forward(layer.w, support, g.n_nodes)
  let mask : Array[Float] = Array::make(pre.length(), 0.0F)
  for i in 0..<pre.length() {
    mask[i] = if pre[i] > 0.0F { 1.0F } else { 0.0F }
  }
  let d_pre = elem_mul(d_out, mask)
  // Backward through the Linear: gives both d_support and d_W.
  let (d_support, gw) = graph_linear_backward(
    layer.w, d_pre, g.n_nodes, support,
  )
  // Route d_support back to the input: the self term is a plain
  // scaling, the edge term is the shared scatter backward. The width
  // is `layer.in_dim`, NOT `g.feat_dim` -- for a hidden layer the
  // support tensor is wider than the node features, and using
  // feat_dim truncates the gradient handed to the previous layer.
  let width = layer.in_dim
  let d_from_edges = scatter_sum_backward_w(g, d_support, width)
  let d_input : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for i in 0..<d_input.length() {
    d_input[i] = layer.alpha * d_support[i] + d_from_edges[i]
  }
  (d_input, GCNLayerGrad::{ w: gw, })
}

///|
/// Forward the GCN stack, caching each layer's input.
pub fn gcn_forward_with_inputs(
  net : GCN,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Array[Float]]) {
  let inputs : Array[Array[Float]] = Array::make(net.num_layers, [])
  let mut acc = h
  for l in 0..<net.num_layers {
    inputs[l] = acc
    acc = gcn_layer_forward(net.layers[l], g, acc)
  }
  (acc, inputs)
}

///|
/// Backward of a whole GCN stack. Returns `(d_input, grads)`.
///
/// The walk is in REVERSE layer order: the upstream gradient belongs
/// to the LAST layer's output, so layer num_layers-1 consumes it and
/// hands its own input gradient back to num_layers-2. Iterating 0, 1,
/// 2... instead feeds the final gradient to layer 0, which reads it
/// with the wrong width as soon as the stack is more than one layer
/// deep -- and is silently correct when it is exactly one.
pub fn gcn_backward(
  net : GCN,
  g : Graph,
  inputs : Array[Array[Float]],
  d_out : Array[Float],
) -> (Array[Float], GCNGrad) {
  let grads = GCNGrad::zero(net)
  let mut d = d_out
  for i in 0..<net.num_layers {
    let l = net.num_layers - 1 - i
    let (d_prev, gl) = gcn_layer_backward(
      net.layers[l], g, inputs[l], d,
    )
    grads.layers[l] = gl
    d = d_prev
  }
  (d, grads)
}

///|
/// Full training step for a GCN: forward, backward, SGD on every
/// layer. Returns `(new_net, d_input)`.
pub fn gcn_sgd_step(
  net : GCN,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (GCN, Array[Float]) {
  let (out, inputs) = gcn_forward_with_inputs(net, g, h)
  let (d_input, grads) = gcn_backward(net, g, inputs, d_out)
  ignore(out)
  let layers : Array[GCNLayer] = Array::make(net.num_layers, net.layers[0])
  for l in 0..<net.num_layers {
    let w_new = graph_linear_sgd_step(
      net.layers[l].w, grads.layers[l].w, lr,
    )
    layers[l] = { ..net.layers[l], w: w_new, }
  }
  let new_net : GCN = { ..net, layers, }
  (new_net, d_input)
}

///|
/// The gradient of a graph-level cross-entropy with respect to a set
/// of node-embedding logits: d_logits = softmax(logits) - onehot(y).
///
/// Lives here rather than in graph_classifier.mbt (v0.140.0) because
/// it is only ever needed by a backward pass; the forward-only path
/// never computes it.
pub fn graph_cross_entropy_grad(
  logits : Array[Float],
  target : Int,
) -> Array[Float] {
  let n = logits.length()
  let mut m = logits[0]
  for i in 1..<n {
    if logits[i] > m {
      m = logits[i]
    }
  }
  let mut z = 0.0F
  for i in 0..<n {
    z = z + expf(logits[i] - m)
  }
  let p = if target >= 0 && target < n { target } else { 0 }
  let d : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let soft = expf(logits[i] - m) / z
    d[i] = if i == p { soft - 1.0F } else { soft }
  }
  d
}
