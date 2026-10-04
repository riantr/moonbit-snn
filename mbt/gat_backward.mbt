// gat_backward.mbt -- GAT backward pass (v0.153.0).
//
// The last message-passing architecture without a gradient. GAT is
// harder than everything in v0.145.0-v0.148.0 for one structural
// reason, and that reason is the whole content of this file:
//
// **the attention score couples BOTH endpoints of one edge.**
//
// Every reducer already shipped -- sum, mean, max, min, std, GCN's
// weighted sum -- sends an edge's influence to exactly ONE end (the
// source, whose features are being aggregated). GAT's score
//
//   e_uv = a_src . (W h_u) + a_dst . (W h_v)
//
// reads both h_u and h_v, so dL/de_uv fans out to BOTH. That needs
// two scatter DIRECTIONS in one pass: the source-half of the score
// gradient accumulates on the source node, the destination-half on the
// destination node. The existing `scatter_*_backward` family only ever
// scatters destination -> source, because no other architecture here
// needed the other way.
//
// The backward, in order:
//   1. d_p[s]   += alpha_e * d_out[d]        weighted sum, to source
//      d_alpha_e  = <d_out[d], p[s]>
//   2. softmax per destination d (over its IN-edges):
//      d_logits_e = alpha_e * (d_alpha_e - SUM_e' alpha_e' d_alpha_e')
//   3. d_tsrc[s] += d_logits_e                <- fan-out, source side
//      d_tdst[d] += d_logits_e                <- fan-out, dest side
//   4. t_src = leaky_relu(a_src . p):  d_a_src[o] += d_s[v] * p[v][o]
//                                    d_p[v][o]   += d_s[v] * a_src[o]
//      t_dst = a_dst . p (linear):   d_a_dst[o] += d_tdst[v] * p[v][o]
//                                    d_p[v][o]   += d_tdst[v] * a_dst[o]
//   5. d_W[o][k] += SUM_v d_p[v][o] * h[v][k]      (outer product, nodes)
//      d_h[v][k] += SUM_o d_p[v][o] * W[o][k]      (per node, dense)
//
// GATLayer carries NO bias (the paper's single-head form), so there is
// no d_b.

///|
/// Parameter gradients for one GAT head. `d_w` mirrors the layer's
/// (out_dim x in_dim) projection; there is no bias term.
pub struct GATLayerGrad {
  d_w : Array[Array[Float]]
  d_a_src : Array[Float]
  d_a_dst : Array[Float]
}

///|
/// Zero-initialised gradients matching a GATLayer. Rows are built
/// EXPLICITLY: `Array::make(out_dim, Array::make(in_dim, 0.0F))` hands
/// back out_dim aliases of one shared inner array in MoonBit, which
/// silently collapses every row of d_w into row 0.
pub fn GATLayerGrad::zero(layer : GATLayer) -> GATLayerGrad {
  let dw : Array[Array[Float]] = Array::make(layer.out_dim, [])
  for o in 0..<layer.out_dim {
    dw[o] = Array::make(layer.in_dim, 0.0F)
  }
  {
    d_w: dw,
    d_a_src: Array::make(layer.out_dim, 0.0F),
    d_a_dst: Array::make(layer.out_dim, 0.0F),
  }
}

///|
/// helper: the shared linear projection p = W h, [n_nodes x out_dim].
/// GATLayer stores `w` as a bare array rather than a GraphLinear, so
/// this cannot reuse `graph_linear_forward`.
pub fn gat_project(
  layer : GATLayer,
  h : Array[Float],
  n_nodes : Int,
) -> Array[Float] {
  let d = layer.out_dim
  let p : Array[Float] = Array::make(n_nodes * d, 0.0F)
  for v in 0..<n_nodes {
    let src = v * layer.in_dim
    let dst = v * d
    for o in 0..<d {
      let mut acc = 0.0F
      for k in 0..<layer.in_dim {
        acc = acc + layer.w[o][k] * h[src + k]
      }
      p[dst + o] = acc
    }
  }
  p
}

///|
/// helper: the per-node attention logits, t_src (post-LeakyReLU) and
/// t_dst (linear). Shared by the forward and the backward so the two
/// cannot disagree about which score an edge received.
pub fn gat_attn_terms(
  layer : GATLayer,
  p : Array[Float],
  n_nodes : Int,
) -> (Array[Float], Array[Float]) {
  let d = layer.out_dim
  let t_src : Array[Float] = Array::make(n_nodes, 0.0F)
  let t_dst : Array[Float] = Array::make(n_nodes, 0.0F)
  for v in 0..<n_nodes {
    let base = v * d
    let mut s = 0.0F
    let mut t = 0.0F
    for o in 0..<d {
      s = s + layer.a_src[o] * p[base + o]
      t = t + layer.a_dst[o] * p[base + o]
    }
    t_src[v] = gat_leaky_relu(s)
    t_dst[v] = t
  }
  (t_src, t_dst)
}

///|
/// helper: the softmax-normalised attention coefficients, computed
/// exactly as `gat_layer_forward` does (max-subtracted, per
/// destination node, over IN-edges).
pub fn gat_alpha(g : Graph, t_src : Array[Float], t_dst : Array[Float]) -> Array[Float] {
  let logits : Array[Float] = Array::make(g.n_edges, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= t_src.length() || d < 0 || d >= t_dst.length() {
      logits[e] = 0.0F
    } else {
      logits[e] = t_src[s] + t_dst[d]
    }
  }
  let deg = graph_in_degree(g)
  let alpha : Array[Float] = Array::make(g.n_edges, 0.0F)
  for v in 0..<g.n_nodes {
    if deg[v] == 0 {
      continue
    }
    let mut m = logits[0]
    let mut first = true
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      if first {
        m = logits[e]
        first = false
      } else if logits[e] > m {
        m = logits[e]
      }
    }
    let mut sum_exp = 0.0F
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      let x = expf(logits[e] - m)
      alpha[e] = x
      sum_exp = sum_exp + x
    }
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      alpha[e] = alpha[e] / sum_exp
    }
  }
  alpha
}

///|
/// helper: the in-neighbour softmax, as a JACOBIAN-VECTOR PRODUCT.
/// The dense Jacobian is [deg x deg] and never materialised; the
/// standard identity
///   dL/dz_e = alpha_e * ( dL/dalpha_e - SUM_e' alpha_e' dL/dalpha_e' )
/// costs O(deg) instead. The inner sum runs once per destination node
/// and is shared by all of its in-edges.
fn gat_softmax_jvp(
  g : Graph,
  alpha : Array[Float],
  d_alpha : Array[Float],
) -> Array[Float] {
  let d_logits : Array[Float] = Array::make(g.n_edges, 0.0F)
  for v in 0..<g.n_nodes {
    // Weighted sum of d_alpha over v's in-edges.
    let mut dot = 0.0F
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      dot = dot + alpha[e] * d_alpha[e]
    }
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      d_logits[e] = alpha[e] * (d_alpha[e] - dot)
    }
  }
  d_logits
}

///|
/// Accumulate per-edge values onto their SOURCE nodes. This is the
/// fan-out direction the existing scatter family does not provide:
/// the `scatter_*_backward` helpers all go destination -> source,
/// because no other architecture reads both endpoints of an edge.
pub fn scatter_add_by_src(
  g : Graph,
  per_edge : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(g.n_nodes, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    if s < 0 || s >= g.n_nodes {
      continue
    }
    out[s] = out[s] + per_edge[e]
  }
  out
}

///|
/// Accumulate per-edge values onto their DESTINATION nodes.
pub fn scatter_add_by_dst(
  g : Graph,
  per_edge : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(g.n_nodes, 0.0F)
  for e in 0..<g.n_edges {
    let d = g.edge_dst[e]
    if d < 0 || d >= g.n_nodes {
      continue
    }
    out[d] = out[d] + per_edge[e]
  }
  out
}

///|
/// Backward of one GAT head. Returns `(d_input, grad)`.
///
/// `elu` selects the hidden-layer activation: pass true when this head
/// sits in a hidden layer of a multi-head stack (whose concatenated
/// output is passed through ELU) and false for the final layer, whose
/// output is linear and therefore needs no derivative mask.
///
/// The mask is applied HERE, not by the caller, because it needs the
/// head's own PRE-activation -- the same value `gat_layer_forward`
/// produces -- and a caller that had to recompute it would be one more
/// place for the forward and the backward to disagree. It also means
/// `elu = true` cannot silently degrade to a no-op, which is what this
/// parameter used to be: the single-head caller below reached the tail
/// of this function, found a placeholder, and returned an UNMASKED
/// gradient for every hidden head. An unmasked gradient is not a small
/// error -- it is the gradient of a different network.
pub fn gat_layer_backward(
  layer : GATLayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  elu : Bool,
) -> (Array[Float], GATLayerGrad) {
  let n = g.n_nodes
  let d = layer.out_dim
  let grad = GATLayerGrad::zero(layer)
  let p = gat_project(layer, h, n)
  let (t_src, t_dst) = gat_attn_terms(layer, p, n)
  let alpha = gat_alpha(g, t_src, t_dst)
  // ELU mask on the incoming gradient. `d_out` belongs to the caller,
  // so the masked copy is a fresh buffer; `d_out` is never mutated.
  let eff = if elu { gat_apply_elu_mask(layer, g, h, d_out) } else { d_out }
  // 1. Weighted sum: out[d] += alpha_e * p[s]. The gradient goes to
  //    the SOURCE (that is the only node the sum reads), and the
  //    coefficient's gradient is the inner product with p[s].
  let d_p : Array[Float] = Array::make(n * d, 0.0F)
  let d_alpha : Array[Float] = Array::make(g.n_edges, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let dst = g.edge_dst[e]
    if s < 0 || s >= n || dst < 0 || dst >= n {
      continue
    }
    let a = alpha[e]
    let src_off = s * d
    let dst_off = dst * d
    let mut dot = 0.0F
    for o in 0..<d {
      dot = dot + eff[dst_off + o] * p[src_off + o]
      d_p[src_off + o] = d_p[src_off + o] + a * eff[dst_off + o]
    }
    d_alpha[e] = dot
  }
  // 2. Softmax over in-neighbours, as a JVP.
  let d_logits = gat_softmax_jvp(g, alpha, d_alpha)
  // 3. THE FAN-OUT: the score read both endpoints, so its gradient
  //    goes to both -- source side via t_src, dest side via t_dst.
  let d_tsrc = scatter_add_by_src(g, d_logits)
  let d_tdst = scatter_add_by_dst(g, d_logits)
  // 4. Through the attention vectors and the LeakyReLU.
  for v in 0..<n {
    let base = v * d
    // t_src = leaky_relu(a_src . p); the raw score's derivative is 1
    // for x > 0 and the negative slope 0.2 otherwise.
    let mut s = 0.0F
    for o in 0..<d {
      s = s + layer.a_src[o] * p[base + o]
    }
    let slope = if s > 0.0F { 1.0F } else { 0.2F }
    let d_s = d_tsrc[v] * slope
    let grad_asrc = grad.d_a_src
    let grad_adst = grad.d_a_dst
    for o in 0..<d {
      grad_asrc[o] = grad_asrc[o] + d_s * p[base + o]
      grad_adst[o] = grad_adst[o] + d_tdst[v] * p[base + o]
      d_p[base + o] = d_p[base + o] + d_s * layer.a_src[o] +
        d_tdst[v] * layer.a_dst[o]
    }
  }
  // 5. Through the projection: outer product into d_W, dense into d_h.
  let d_h : Array[Float] = Array::make(n * layer.in_dim, 0.0F)
  for v in 0..<n {
    let src = v * layer.in_dim
    let poff = v * d
    for o in 0..<d {
      let go = d_p[poff + o]
      let grow = grad.d_w[o]
      let wrow = layer.w[o]
      for k in 0..<layer.in_dim {
        grow[k] = grow[k] + go * h[src + k]
        d_h[src + k] = d_h[src + k] + wrow[k] * go
      }
    }
  }
  // The ELU mask was applied to `eff` at the top, before any of this.
  (d_h, grad)
}

///|
/// Apply the ELU derivative to an upstream gradient: the multi-head
/// stack passes every hidden layer's concatenated head output through
/// ELU, so the gradient a head must differentiate is its pre-activation
/// gradient scaled by `gat_elu_grad` AT THE HEAD'S OWN PRE-ACTIVATION.
///
/// Returns a fresh buffer; `d_out` is the caller's and is left alone.
pub fn gat_apply_elu_mask(
  layer : GATLayer,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> Array[Float] {
  let raw = gat_layer_forward(layer, g, h)
  let out : Array[Float] = Array::make(d_out.length(), 0.0F)
  for i in 0..<d_out.length() {
    out[i] = d_out[i] * gat_elu_grad(raw[i])
  }
  out
}

///|
/// ELU derivative, exported so the multi-head stack can build the
/// same mask it applies in the forward.
pub fn gat_elu_grad(x : Float) -> Float {
  if x > 0.0F { 1.0F } else { expf(x) }
}

///|
/// One SGD step on a GAT head. Returns a FRESH GATLayer; the input is
/// not mutated, so a head can be threaded through a loop like the
/// other parameters.
pub fn gat_layer_sgd_step(
  layer : GATLayer,
  grad : GATLayerGrad,
  lr : Float,
) -> GATLayer {
  let w : Array[Array[Float]] = Array::make(layer.out_dim, [])
  for o in 0..<layer.out_dim {
    let row : Array[Float] = Array::make(layer.in_dim, 0.0F)
    for k in 0..<layer.in_dim {
      row[k] = layer.w[o][k] - lr * grad.d_w[o][k]
    }
    w[o] = row
  }
  let a_src : Array[Float] = Array::make(layer.out_dim, 0.0F)
  let a_dst : Array[Float] = Array::make(layer.out_dim, 0.0F)
  for o in 0..<layer.out_dim {
    a_src[o] = layer.a_src[o] - lr * grad.d_a_src[o]
    a_dst[o] = layer.a_dst[o] - lr * grad.d_a_dst[o]
  }
  { ..layer, w, a_src, a_dst, }
}
