// graph.mbt -- Graph struct and scatter aggregations (v0.137.0).
//
// A Graph holds a node feature matrix [n_nodes x feat_dim] and a
// directed edge list in COO form. Because this project stores all
// tensors as flat row-major `Array[Float]`, the edge list is three
// parallel arrays (`edge_src`, `edge_dst`, `edge_weight`) rather than
// a single [n_edges x 2] matrix.
//
// The scatter aggregations are the core primitive behind every
// message-passing GNN: they gather the neighbour messages of a node
// and reduce them with sum / mean / max.
//
// Scope of v0.137.0:
//   - Graph struct (features + COO edge list + optional edge weights).
//   - Graph::new / Graph::zeros / Graph::from_edges.
//   - scatter_sum / scatter_mean / scatter_max over an edge list.
//   - degree helper.
//   - normalise_adjacency (symmetric GCN normalisation D^-1/2 A D^-1/2).
//
// Reference: Gilmer et al. 2017 (message passing); Kipf & Welling
// 2017 (GCN normalisation).

///|
/// Graph: node features plus a directed COO edge list.
pub struct Graph {
  n_nodes : Int
  feat_dim : Int
  // Node features, flat row-major [n_nodes x feat_dim].
  features : Array[Float]
  n_edges : Int
  // Edge list: edge i runs from edge_src[i] to edge_dst[i].
  edge_src : Array[Int]
  edge_dst : Array[Int]
  // Per-edge weights (1.0 for an unweighted graph).
  edge_weight : Array[Float]
}

///|
/// Build a graph from a node feature matrix and a COO edge list.
/// `edge_weight` may be empty, in which case every edge gets weight 1.
pub fn Graph::new(
  n_nodes : Int,
  feat_dim : Int,
  features : Array[Float],
  edge_src : Array[Int],
  edge_dst : Array[Int],
  edge_weight : Array[Float],
) -> Graph {
  let n_edges = edge_src.length()
  let w : Array[Float] = Array::make(n_edges, 1.0F)
  for i in 0..<n_edges {
    if i < edge_weight.length() {
      w[i] = edge_weight[i]
    }
  }
  { n_nodes, feat_dim, features, n_edges, edge_src, edge_dst, edge_weight: w, }
}

///|
/// Build an unweighted graph with zero-initialised features.
pub fn Graph::zeros(
  n_nodes : Int,
  feat_dim : Int,
  edge_src : Array[Int],
  edge_dst : Array[Int],
) -> Graph {
  let features : Array[Float] = Array::make(n_nodes * feat_dim, 0.0F)
  Graph::new(n_nodes, feat_dim, features, edge_src, edge_dst, Array::make(0, 0.0F))
}

///|
/// Out-degree of each node: the number of edges whose source is that
/// node. Self-loops are counted if present in the edge list.
pub fn graph_out_degree(g : Graph) -> Array[Int] {
  let deg : Array[Int] = Array::make(g.n_nodes, 0)
  for i in 0..<g.n_edges {
    let s = g.edge_src[i]
    if s >= 0 && s < g.n_nodes {
      deg[s] = deg[s] + 1
    }
  }
  deg
}

///|
/// In-degree of each node.
pub fn graph_in_degree(g : Graph) -> Array[Int] {
  let deg : Array[Int] = Array::make(g.n_nodes, 0)
  for i in 0..<g.n_edges {
    let d = g.edge_dst[i]
    if d >= 0 && d < g.n_nodes {
      deg[d] = deg[d] + 1
    }
  }
  deg
}

///|
/// helper: allocate a [n_nodes x feat_dim] output buffer.
fn alloc_features(n_nodes : Int, feat_dim : Int) -> Array[Float] {
  Array::make(n_nodes * feat_dim, 0.0F)
}

///|
/// Scatter-sum of source-node features onto destination nodes,
/// optionally scaled by the per-edge weight:
///
///   out[d] += edge_weight[e] * features[s]
///
/// Isolated destination nodes keep their zero-initialised row.
pub fn scatter_sum(
  g : Graph,
  messages : Array[Float],
) -> Array[Float] {
  let out = alloc_features(g.n_nodes, g.feat_dim)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * g.feat_dim
    let dst_off = d * g.feat_dim
    for k in 0..<g.feat_dim {
      out[dst_off + k] = out[dst_off + k] + w * messages[src_off + k]
    }
  }
  out
}

///|
/// Scatter-mean with an EXPLICIT message width.
///
/// The width parameter exists because a GNN layer's internal tensors
/// are not the node-feature width. For layer l > 0 the incoming
/// activations are `dims[l]` wide while `g.feat_dim` is still
/// `dims[0]`, so any reduction that assumes feat_dim strides over the
/// wrong length and silently returns a truncated result. Every layer
/// forward must pass `layer.in_dim` here, not `g.feat_dim`.
pub fn scatter_mean_w(
  g : Graph,
  messages : Array[Float],
  width : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(g.n_nodes * width, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * width
    let dst_off = d * width
    for k in 0..<width {
      out[dst_off + k] = out[dst_off + k] + w * messages[src_off + k]
    }
  }
  let deg = graph_in_degree(g)
  for d in 0..<g.n_nodes {
    if deg[d] == 0 {
      continue
    }
    let inv = 1.0F / Float::from_int(deg[d])
    let off = d * width
    for k in 0..<width {
      out[off + k] = out[off + k] * inv
    }
  }
  out
}

///|
/// Scatter-mean, assuming messages are `g.feat_dim` wide. Correct when
/// the tensors being reduced ARE the node features (and for layer 0 of
/// any stack); use `scatter_mean_w` inside deeper layers.
pub fn scatter_mean(
  g : Graph,
  messages : Array[Float],
) -> Array[Float] {
  scatter_mean_w(g, messages, g.feat_dim)
}

///|
/// Scatter-max: element-wise maximum of the incoming messages per
/// destination node. Nodes with no incoming edge keep a zero row.
///
/// The accumulator is seeded from each node's FIRST incoming message,
/// not from 0.0. A separate `seeded` flag is required rather than the
/// static in-degree: in-degree is non-zero for every node that has an
/// edge at all, so testing it would leave the accumulator at its 0.0
/// initial value and silently clamp any all-negative neighbourhood
/// to 0 instead of reporting its (negative) maximum.
pub fn scatter_max(
  g : Graph,
  messages : Array[Float],
) -> Array[Float] {
  let out = alloc_features(g.n_nodes, g.feat_dim)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * g.feat_dim
    let dst_off = d * g.feat_dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<g.feat_dim {
      let m = w * messages[src_off + k]
      if first || m > out[dst_off + k] {
        out[dst_off + k] = m
      }
    }
  }
  out
}

///|
/// Scatter-min: element-wise minimum of the incoming messages per
/// destination node. Nodes with no incoming edge keep a zero row.
/// Uses the same first-message seeding as `scatter_max`.
pub fn scatter_min(
  g : Graph,
  messages : Array[Float],
) -> Array[Float] {
  let out = alloc_features(g.n_nodes, g.feat_dim)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let w = g.edge_weight[e]
    let src_off = s * g.feat_dim
    let dst_off = d * g.feat_dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<g.feat_dim {
      let m = w * messages[src_off + k]
      if first || m < out[dst_off + k] {
        out[dst_off + k] = m
      }
    }
  }
  out
}

///|
/// helper: build a self-loop edge list (src == dst) for every node,
/// appended after the existing edges.
pub fn graph_add_self_loops(g : Graph) -> Graph {
  let total = g.n_edges + g.n_nodes
  let src : Array[Int] = Array::make(total, 0)
  let dst : Array[Int] = Array::make(total, 0)
  let w : Array[Float] = Array::make(total, 0.0F)
  for e in 0..<g.n_edges {
    src[e] = g.edge_src[e]
    dst[e] = g.edge_dst[e]
    w[e] = g.edge_weight[e]
  }
  for i in 0..<g.n_nodes {
    src[g.n_edges + i] = i
    dst[g.n_edges + i] = i
    w[g.n_edges + i] = 1.0F
  }
  { ..g, n_edges: total, edge_src: src, edge_dst: dst, edge_weight: w, }
}

///|
/// Symmetric GCN adjacency normalisation D^-1/2 (A + I) D^-1/2.
/// Returns a COO edge list with the normalised weights baked in
/// (self-loops are added first, as in Kipf & Welling 2017 eq. 2).
pub fn normalise_adjacency(g : Graph) -> Graph {
  let with_loops = graph_add_self_loops(g)
  let n = with_loops.n_nodes
  // Degree here is the total (in + out) degree after symmetrising, so
  // compute it from the loop-augmented list in both directions.
  let deg : Array[Int] = Array::make(n, 0)
  for e in 0..<with_loops.n_edges {
    let s = with_loops.edge_src[e]
    let d = with_loops.edge_dst[e]
    if s >= 0 && s < n {
      deg[s] = deg[s] + 1
    }
    if d >= 0 && d < n {
      deg[d] = deg[d] + 1
    }
  }
  let inv_sqrt : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    if deg[i] > 0 {
      inv_sqrt[i] = 1.0F / sqrtf(Float::from_int(deg[i]))
    }
  }
  let w : Array[Float] = Array::make(with_loops.n_edges, 0.0F)
  for e in 0..<with_loops.n_edges {
    let s = with_loops.edge_src[e]
    let d = with_loops.edge_dst[e]
    if s < 0 || s >= n || d < 0 || d >= n {
      continue
    }
    w[e] = inv_sqrt[s] * inv_sqrt[d]
  }
  { ..with_loops, edge_weight: w, }
}