// graph_backward.mbt -- Gradient primitives shared by every GNN
// backward (v0.145.0).
//
// Batches V (v0.133.0-v0.140.0) and X (v0.141.0-v0.144.0) shipped
// six message-passing architectures, all forward-only. They all defer
// to the SAME hard problem, and it is not the Linear algebra -- it is
// the scatter.
//
// A GNN forward is `out[d] = AGG over edges e ending at d`. Its
// backward is a *gather-scatter* in the opposite direction: every
// incoming edge of node d must READ d's upstream gradient and route a
// share of it back to its own source. For a node with in-degree d_v
// that is d_v writes into one row, which is why this is the part worth
// getting right once rather than seven times:
//
//   d_messages[s] += edge_weight[e] * d_out[d]        (sum)
//   d_messages[s] += edge_weight[e] * d_out[d] / deg[d] (mean)
//   d_messages[s] += d_out[d]        at the argmax only (max/min)
//
// The third is the only one that is not a pure scatter: the gradient
// of a max is a ROUTING, not a sum -- it must be routed to the single
// element that won, which means the forward pass has to remember where
// that was. Hence `scatter_max_forward_with_idx` below: the forward
// is re-run to recover the argmax, and the max backward is a *gather*
// (every winner reads its own gradient) rather than a scatter.
//
// Scope of v0.145.0:
//   - scatter_sum_backward / scatter_mean_backward (edge-list gradient
//     accumulation -- the shared primitive).
//   - scatter_max_forward_with_idx + scatter_max_backward /
//     scatter_min_forward_with_idx + scatter_min_backward (routing).
//   - GraphLinearGrad + graph_linear_backward.
//   - graph_linear_sgd_step: one vanilla SGD step on a GraphLinear.
//
// The argument-gradient correctness of these primitives is exactly
// the contract `linear_backward` in linear_backward.mbt already has
// for a dense layer, so the two are written to be read side by side.

///|
/// Backward of `scatter_sum`. Routes each destination's upstream
/// gradient back along its incoming edges:
///
///   d_messages[s] += edge_weight[e] * d_out[d]
///
/// Note this is the same edge-list walk as the forward, so a node
/// accumulates one contribution per incoming edge. Nodes with no
/// outgoing contribution keep a zero row.
pub fn scatter_sum_backward(
  g : Graph,
  d_out : Array[Float],
) -> Array[Float] {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * dim
    let dst_off = d * dim
    for k in 0..<dim {
      out[src_off + k] = out[src_off + k] + w * d_out[dst_off + k]
    }
  }
  out
}

///|
/// Backward of `scatter_mean`. The forward divided by the
/// destination's in-degree, so the backward multiplies by the same
/// factor:
///
///   d_messages[s] += edge_weight[e] * d_out[d] / deg[d]
///
/// The 1/deg is attached to the DESTINATION, not the source. Getting
/// that backwards silently rescales every node's gradient by the
/// ratio of its in- and out-degree, which is invisible on a
/// regular graph and wrong on a power-law one.
pub fn scatter_mean_backward(
  g : Graph,
  d_out : Array[Float],
) -> Array[Float] {
  let dim = g.feat_dim
  let deg = graph_in_degree(g)
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    if deg[d] == 0 {
      continue
    }
    let w = g.edge_weight[e]
    let scale = w / Float::from_int(deg[d])
    let src_off = s * dim
    let dst_off = d * dim
    for k in 0..<dim {
      out[src_off + k] = out[src_off + k] + scale * d_out[dst_off + k]
    }
  }
  out
}

///|
/// Scatter-max forward that also records, per destination node and
/// per feature, WHICH incoming edge won. The backward needs the argmax
/// and cannot recover it from the forward's output alone (many
/// neighbourhoods share the same maximum value).
///
/// Ties go to the FIRST edge in list order, matching the forward's
/// `first || m > out[...]` tie-break, so the recorded index and the
/// value the forward actually produced always agree.
pub fn scatter_max_forward_with_idx(
  g : Graph,
  messages : Array[Float],
) -> (Array[Float], Array[Int]) {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let argmax : Array[Int] = Array::make(g.n_nodes * dim, -1)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * dim
    let dst_off = d * dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<dim {
      let m = w * messages[src_off + k]
      if first || m > out[dst_off + k] {
        out[dst_off + k] = m
        argmax[dst_off + k] = e
      }
    }
  }
  (out, argmax)
}

///|
/// Backward of `scatter_max`. This is a ROUTING, not a scatter: the
/// gradient goes only to the edge that produced the maximum.
///
///   for each (d, k):  d_messages[src(argmax[d,k])][k] += w * d_out[d][k]
///
/// `argmax` must come from `scatter_max_forward_with_idx` on the SAME
/// messages; passing an index array from a different forward (or from
/// the plain `scatter_max`) routes the gradient to the wrong edge.
pub fn scatter_max_backward(
  g : Graph,
  d_out : Array[Float],
  argmax : Array[Int],
) -> Array[Float] {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for d in 0..<g.n_nodes {
    let dst_off = d * dim
    for k in 0..<dim {
      let e = argmax[dst_off + k]
      if e < 0 || e >= g.n_edges {
        continue
      }
      let s = g.edge_src[e]
      if s < 0 || s >= g.n_nodes {
        continue
      }
      let scale = g.edge_weight[e]
      let src_off = s * dim
      out[src_off + k] = out[src_off + k] + scale * d_out[dst_off + k]
    }
  }
  out
}

///|
/// Scatter-min forward that records the argmin. Same tie-break (first
/// edge wins) as `scatter_max_forward_with_idx`.
pub fn scatter_min_forward_with_idx(
  g : Graph,
  messages : Array[Float],
) -> (Array[Float], Array[Int]) {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let argmin : Array[Int] = Array::make(g.n_nodes * dim, -1)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * dim
    let dst_off = d * dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<dim {
      let m = w * messages[src_off + k]
      if first || m < out[dst_off + k] {
        out[dst_off + k] = m
        argmin[dst_off + k] = e
      }
    }
  }
  (out, argmin)
}

///|
/// Backward of `scatter_min`. Structurally identical to
/// `scatter_max_backward` -- only the recorded argmin differs.
pub fn scatter_min_backward(
  g : Graph,
  d_out : Array[Float],
  argmin : Array[Int],
) -> Array[Float] {
  scatter_max_backward(g, d_out, argmin)
}

///|
/// Parameter gradients for a `GraphLinear`. `d_w` mirrors the
/// (out_dim x in_dim) shape of the weight matrix; `d_b` matches the
/// bias length.
pub struct GraphLinearGrad {
  d_w : Array[Array[Float]]
  d_b : Array[Float]
}

///|
/// Zero-initialised gradients matching a GraphLinear's shapes.
pub fn GraphLinearGrad::zero(lin : GraphLinear) -> GraphLinearGrad {
  let dw : Array[Array[Float]] = Array::make(
    lin.out_dim, Array::make(lin.in_dim, 0.0F),
  )
  {
    d_w: dw,
    d_b: Array::make(lin.out_dim, 0.0F),
  }
}

///|
/// Backward of a dense per-node Linear. `h` must be the input that
/// produced `d_out` (i.e. the node embeddings fed to the layer).
/// Returns `(d_input, grad)`.
///
/// The weight gradient is a sum of outer products, one per node:
///   d_w[o][k] += d_out[v][o] * h[v][k]
/// which is the same all-reduce pattern as the scatter backward, just
/// over nodes instead of edges.
pub fn graph_linear_backward(
  lin : GraphLinear,
  d_out : Array[Float],
  n_nodes : Int,
  h : Array[Float],
) -> (Array[Float], GraphLinearGrad) {
  let d_input : Array[Float] = Array::make(n_nodes * lin.in_dim, 0.0F)
  let grad = GraphLinearGrad::zero(lin)
  for v in 0..<n_nodes {
    let src = v * lin.in_dim
    let dst = v * lin.out_dim
    for o in 0..<lin.out_dim {
      let go = d_out[dst + o]
      grad.d_b[o] = grad.d_b[o] + go
      let wrow = grad.d_w[o]
      let lrow = lin.w[o]
      let d_row = d_input
      for k in 0..<lin.in_dim {
        wrow[k] = wrow[k] + go * h[src + k]
        d_row[src + k] = d_row[src + k] + lrow[k] * go
      }
    }
  }
  (d_input, grad)
}

///|
/// One vanilla SGD step on a GraphLinear. Returns a FRESH GraphLinear
/// (the input is not mutated) so a parameter can be threaded through a
/// `for` loop the same way the forward outputs are.
pub fn graph_linear_sgd_step(
  lin : GraphLinear,
  grad : GraphLinearGrad,
  lr : Float,
) -> GraphLinear {
  let w : Array[Array[Float]] = Array::make(lin.out_dim, [])
  for o in 0..<lin.out_dim {
    let row : Array[Float] = Array::make(lin.in_dim, 0.0F)
    for k in 0..<lin.in_dim {
      row[k] = lin.w[o][k] - lr * grad.d_w[o][k]
    }
    w[o] = row
  }
  let b : Array[Float] = Array::make(lin.out_dim, 0.0F)
  for o in 0..<lin.out_dim {
    b[o] = lin.b[o] - lr * grad.d_b[o]
  }
  { ..lin, w, b }
}

///|
/// Add `b` into `a` in place, returning the same array. Every GNN
/// backward accumulates into the input gradient from two paths (the
/// self term and the neighbour term), and MoonBit arrays are
/// reference types, so mutating through the shared buffer is the
/// intended idiom -- not a hidden aliasing bug.
pub fn add_into(a : Array[Float], b : Array[Float]) -> Unit {
  let n = if a.length() < b.length() { a.length() } else { b.length() }
  for i in 0..<n {
    a[i] = a[i] + b[i]
  }
}

///|
/// Elementwise product `a .* b` into a fresh array, used to apply a
/// ReLU derivative mask to an upstream gradient.
pub fn elem_mul(a : Array[Float], b : Array[Float]) -> Array[Float] {
  let n = if a.length() < b.length() { a.length() } else { b.length() }
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = a[i] * b[i]
  }
  out
}
