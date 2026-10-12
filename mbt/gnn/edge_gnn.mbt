// edge_gnn.mbt -- Edge-feature-aware message passing and the H2GCN
// heterophily architecture (v0.144.0).
//
// This closes the two gaps Batch W (v0.137.0-v0.140.0) left open.
//
// GAP 1 -- edge attributes are ignored.
// Every layer shipped so far uses the Gilmer MESSAGE as the identity
// on node features, so a molecular graph knows that two atoms are
// bonded but not whether the bond is single or double, and a citation
// graph cannot tell a "cites" edge from a "cited by" edge. The fix is
// the RelationalConv / EdgeConv message (DGL, Wang et al. 2019):
//
//   m_uv = W_n . h_u + W_e . e_uv
//   h_v' = act( W_s . h_v + mean_{u in N(v)} m_uv )
//
// The edge term is per-edge, so it is accumulated inside the same loop
// that walks the edge list -- there is no separate message matrix to
// materialise for large graphs.
//
// GAP 2 -- heterophily.
// On a heterophilous graph (same-class nodes tend to be connected,
// as in the Cora / Citeseer benchmarks' denser version) every
// smoothing GNN -- GCN, MPNN, GIN, PNA -- actively hurts, because the
// neighbourhood is dominated by other classes and averaging it
// overwrites the node's own evidence. H2GCN (Zhu et al. 2020,
// "Beyond Homophily in Graph Neural Networks: Current Limitations and
// Effective Designs") attacks this by keeping THREE feature spaces
// separate all the way to the output instead of collapsing them:
//
//   X_ego^(t+1)   = ReLU( X_ego^(t) W_ego )
//   X_neigh^(t+1) = ReLU( A [X_ego^(t) + X_neigh^(t)] W_neigh )
//   X_high^(t+1)  = ReLU( A ( A [X_ego^(t) + X_neigh^(t)] + X_high^(t) ) W_high )
//
// Ego never mixes in neighbours (safe under heterophily), neighbours
// mix 1-hop, and the "high" space mixes 2-hop plus the previous
// high-order estimate. The three are concatenated and projected only
// at the very end, so no single mixture can destroy the ego signal.
//
// Also here: the three homophily diagnostics the H2GCN paper uses to
// decide WHICH regime a dataset is in. Measure this before choosing
// an architecture -- a GNN choice made without it is a guess.
//
// Scope of v0.144.0:
//   - EdgeGraph: a Graph plus a per-edge feature matrix.
//   - EdgeLayer / EdgeGNN: edge-aware RelationalConv and its stack.
//   - H2GCNLayer / H2GCN: three separated spaces, combined at the end.
//   - edge_homophily / node_homophily / degree_histogram.

///|
/// A graph whose edges carry features. Node features keep the same
/// flat row-major layout as `Graph`; edge features are
/// [n_edges x edge_feat_dim] row-major, so edge e's vector starts at
/// `e * edge_feat_dim`.
pub struct EdgeGraph {
  n_nodes : Int
  feat_dim : Int
  features : Array[Float]
  n_edges : Int
  edge_src : Array[Int]
  edge_dst : Array[Int]
  edge_feat_dim : Int
  edge_features : Array[Float]
}

///|
/// Build an EdgeGraph from node features, a COO edge list, and edge
/// features. A short `edge_features` array is treated as zeros.
pub fn EdgeGraph::new(
  n_nodes : Int,
  feat_dim : Int,
  features : Array[Float],
  edge_src : Array[Int],
  edge_dst : Array[Int],
  edge_feat_dim : Int,
  edge_features : Array[Float],
) -> EdgeGraph {
  let n_edges = edge_src.length()
  let need = n_edges * edge_feat_dim
  let ef : Array[Float] = Array::make(need, 0.0F)
  let avail = if edge_features.length() < need {
    edge_features.length()
  } else {
    need
  }
  for i in 0..<avail {
    ef[i] = edge_features[i]
  }
  {
    n_nodes,
    feat_dim,
    features,
    n_edges,
    edge_src,
    edge_dst,
    edge_feat_dim,
    edge_features: ef,
  }
}

///|
/// Drop the edge features and return the plain sub-graph. Useful for
/// feeding a backbone that has no edge pathway.
pub fn EdgeGraph::to_graph(eg : EdgeGraph) -> Graph {
  Graph::new(
    eg.n_nodes,
    eg.feat_dim,
    eg.features,
    eg.edge_src,
    eg.edge_dst,
    Array::make(0, 0.0F),
  )
}

///|
/// helper: unweighted mean of the in-neighbour features. Separate
/// from `scatter_mean` in graph.mbt, which multiplies by the stored
/// edge weights; the mean over a plain neighbour multiset is what
/// RelationalConv and H2GCN both specify.
fn edge_neighbour_mean(g : Graph, messages : Array[Float]) -> Array[Float] {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let cnt : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * dim
    let dst_off = d * dim
    for k in 0..<dim {
      out[dst_off + k] = out[dst_off + k] + messages[src_off + k]
    }
    cnt[d] = cnt[d] + 1
  }
  for v in 0..<g.n_nodes {
    if cnt[v] == 0 {
      continue
    }
    let inv = 1.0F / Float::from_int(cnt[v])
    let off = v * dim
    for k in 0..<dim {
      out[off + k] = out[off + k] * inv
    }
  }
  out
}

///|
/// One RelationalConv layer: node features and edge features both
/// contribute to the message.
pub struct EdgeLayer {
  in_dim : Int
  edge_dim : Int
  out_dim : Int
  w_neigh : GraphLinear
  w_edge : GraphLinear
  w_self : GraphLinear
}

///|
/// Build an EdgeLayer.
pub fn EdgeLayer::new(
  in_dim : Int,
  edge_dim : Int,
  out_dim : Int,
  seed : UInt64,
) -> EdgeLayer {
  {
    in_dim,
    edge_dim,
    out_dim,
    w_neigh: GraphLinear::new(in_dim, out_dim, seed),
    w_edge: GraphLinear::new(edge_dim, out_dim, seed + 10UL),
    w_self: GraphLinear::new(in_dim, out_dim, seed + 20UL),
  }
}

///|
/// Forward one edge-aware layer:
///   m_uv = W_n . h_u + W_e . e_uv
///   h_v' = act( W_s . h_v + mean_{u in N(v)} m_uv )
///
/// The per-edge message is accumulated directly into the destination
/// node's slot, so no [n_edges x out_dim] intermediate is allocated.
pub fn edge_layer_forward(
  layer : EdgeLayer,
  eg : EdgeGraph,
  h : Array[Float],
) -> Array[Float] {
  let out_dim = layer.out_dim
  let agg : Array[Float] = Array::make(eg.n_nodes * out_dim, 0.0F)
  let cnt : Array[Int] = Array::make(eg.n_nodes, 0)
  for e in 0..<eg.n_edges {
    let s = eg.edge_src[e]
    let d = eg.edge_dst[e]
    if s < 0 || s >= eg.n_nodes || d < 0 || d >= eg.n_nodes {
      continue
    }
    let src_off = s * layer.in_dim
    let ef_off = e * eg.edge_feat_dim
    let dst_off = d * out_dim
    for k in 0..<out_dim {
      // m_uv[k] = bias_n + sum_j W_n[k][j] h_u[j]
      //                     + bias_e + sum_j W_e[k][j] e_uv[j]
      let mut m = layer.w_neigh.b[k] + layer.w_edge.b[k]
      let wn = layer.w_neigh.w[k]
      for j in 0..<layer.in_dim {
        m = m + wn[j] * h[src_off + j]
      }
      let we = layer.w_edge.w[k]
      for j in 0..<eg.edge_feat_dim {
        m = m + we[j] * eg.edge_features[ef_off + j]
      }
      agg[dst_off + k] = agg[dst_off + k] + m
    }
    cnt[d] = cnt[d] + 1
  }
  // Mean-aggregate, then add the self projection and activate.
  for v in 0..<eg.n_nodes {
    if cnt[v] > 0 {
      let inv = 1.0F / Float::from_int(cnt[v])
      let off = v * out_dim
      for k in 0..<out_dim {
        agg[off + k] = agg[off + k] * inv
      }
    }
  }
  let self_out = graph_linear_forward(layer.w_self, h, eg.n_nodes)
  let agg_size = eg.n_nodes * out_dim
  let out : Array[Float] = Array::make(agg_size, 0.0F)
  for i in 0..<agg_size {
    out[i] = graph_relu_one(self_out[i] + agg[i])
  }
  out
}

///|
/// Number of parameters in one EdgeLayer.
pub fn edge_layer_num_params(layer : EdgeLayer) -> Int {
  layer.w_neigh.in_dim * layer.w_neigh.out_dim + layer.w_neigh.out_dim +
  layer.w_edge.in_dim * layer.w_edge.out_dim + layer.w_edge.out_dim +
  layer.w_self.in_dim * layer.w_self.out_dim + layer.w_self.out_dim
}

///|
/// Stack of L edge-aware layers. `dims` has `num_layers + 1` entries;
/// the edge-feature width is fixed at dims[0] and carried unchanged
/// through the stack, so a caller must supply edge features already
/// projected to that width.
pub struct EdgeGNN {
  layers : Array[EdgeLayer]
  num_layers : Int
  edge_dim : Int
  out_dim : Int
}

///|
/// Build an L-layer EdgeGNN.
pub fn EdgeGNN::new(dims : Array[Int], edge_dim : Int, seed : UInt64) -> EdgeGNN {
  let num_layers = dims.length() - 1
  let layers : Array[EdgeLayer] = Array::make(
    num_layers, EdgeLayer::new(dims[0], edge_dim, dims[1], seed),
  )
  for l in 0..<num_layers {
    layers[l] = EdgeLayer::new(
      dims[l],
      edge_dim,
      dims[l + 1],
      seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  { layers, num_layers, edge_dim, out_dim: dims[num_layers], }
}

///|
/// Forward the full EdgeGNN stack. Every layer applies its ReLU,
/// including the last.
pub fn edge_gnn_forward(
  net : EdgeGNN,
  eg : EdgeGraph,
  h : Array[Float],
) -> Array[Float] {
  let mut acc = edge_layer_forward(net.layers[0], eg, h)
  for l in 1..<net.num_layers {
    acc = edge_layer_forward(net.layers[l], eg, acc)
  }
  acc
}

///|
/// Total parameter count of the stack.
pub fn edge_gnn_num_params(net : EdgeGNN) -> Int {
  let mut total = 0
  for l in 0..<net.num_layers {
    total = total + edge_layer_num_params(net.layers[l])
  }
  total
}

///|
/// One H2GCN layer, keeping ego / neighbour / high-order spaces
/// separate. Each of the three has its own weight matrix; the layer
/// returns three [n_nodes x out_dim] arrays rather than one mixture.
pub struct H2GCNLayer {
  in_dim : Int
  out_dim : Int
  w_ego : GraphLinear
  w_neigh : GraphLinear
  w_high : GraphLinear
}

///|
/// Build an H2GCNLayer.
pub fn H2GCNLayer::new(
  in_dim : Int,
  out_dim : Int,
  seed : UInt64,
) -> H2GCNLayer {
  {
    in_dim,
    out_dim,
    w_ego: GraphLinear::new(in_dim, out_dim, seed),
    w_neigh: GraphLinear::new(in_dim, out_dim, seed + 10UL),
    w_high: GraphLinear::new(in_dim, out_dim, seed + 20UL),
  }
}

///|
/// Forward one H2GCN layer, returning (ego, neigh, high).
///
///   ego  = act( W_ego . ego )
///   neigh = act( W_neigh . mean_{u}( ego_u + neigh_u ) )
///   high = act( W_high . mean_u( mean_v( ego_v + neigh_v ) + high_u ) )
///
/// The ego path never reads the neighbourhood, which is the whole
/// point: on a heterophilous graph it preserves the node's own class
/// evidence that averaging would overwrite.
pub fn h2gcn_layer_forward(
  layer : H2GCNLayer,
  g : Graph,
  ego : Array[Float],
  neigh : Array[Float],
  high : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let dim = layer.out_dim
  // ego path: purely pointwise.
  let ego_out = graph_linear_forward(layer.w_ego, ego, g.n_nodes)
  // 1-hop of (ego + neigh).
  let n_size = g.n_nodes * dim
  let ego_neigh : Array[Float] = Array::make(n_size, 0.0F)
  for i in 0..<n_size {
    ego_neigh[i] = ego[i] + neigh[i]
  }
  let agg1 = edge_neighbour_mean(g, ego_neigh)
  // (1-hop of (ego + neigh)) + high, then one more 1-hop: a genuine
  // 2-hop neighbourhood term.
  let agg1_high : Array[Float] = Array::make(n_size, 0.0F)
  for i in 0..<n_size {
    agg1_high[i] = agg1[i] + high[i]
  }
  let agg2 = edge_neighbour_mean(g, agg1_high)
  let neigh_pre = graph_linear_forward(layer.w_neigh, agg1, g.n_nodes)
  let high_pre = graph_linear_forward(layer.w_high, agg2, g.n_nodes)
  let neigh_out : Array[Float] = Array::make(n_size, 0.0F)
  let high_out : Array[Float] = Array::make(n_size, 0.0F)
  for i in 0..<n_size {
    neigh_out[i] = graph_relu_one(neigh_pre[i])
    high_out[i] = graph_relu_one(high_pre[i])
  }
  let ego_act : Array[Float] = Array::make(n_size, 0.0F)
  for i in 0..<n_size {
    ego_act[i] = graph_relu_one(ego_out[i])
  }
  (ego_act, neigh_out, high_out)
}

///|
/// H2GCN: a stack of separated-space layers plus a final projection
/// that concatenates the three spaces.
pub struct H2GCN {
  layers : Array[H2GCNLayer]
  num_layers : Int
  out_dim : Int
  combine : GraphLinear
}

///|
/// Build an L-layer H2GCN. `dims` has `num_layers + 1` entries and
/// describes the per-space widths; `node_dim` is the width of the
/// concatenated-and-projected output.
pub fn H2GCN::new(
  dims : Array[Int],
  node_dim : Int,
  seed : UInt64,
) -> H2GCN {
  let num_layers = dims.length() - 1
  let layers : Array[H2GCNLayer] = Array::make(
    num_layers, H2GCNLayer::new(dims[0], dims[1], seed),
  )
  for l in 0..<num_layers {
    layers[l] = H2GCNLayer::new(
      dims[l], dims[l + 1], seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  {
    layers,
    num_layers,
    out_dim: dims[num_layers],
    combine: GraphLinear::new(dims[num_layers] * 3, node_dim, seed + 30UL),
  }
}

///|
/// Forward the full H2GCN stack and combine the three spaces into a
/// single [n_nodes x node_dim] embedding.
pub fn h2gcn_forward(
  net : H2GCN,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  // The width of the concatenated spaces is the LAST layer's width,
  // not the first's. Using layers[0].out_dim happens to agree on a
  // uniform stack and silently reads out of bounds on a tapered one
  // (dims [16, 8, 4]), which `moon check` cannot see.
  let dim = net.out_dim
  let (e0, n0, g0) = h2gcn_layer_forward(net.layers[0], g, h, h, h)
  let mut ae = e0
  let mut an = n0
  let mut ag = g0
  for l in 1..<net.num_layers {
    let (e, nn, gg) = h2gcn_layer_forward(net.layers[l], g, ae, an, ag)
    ae = e
    an = nn
    ag = gg
  }
  // Concatenate [ego | neigh | high] per node.
  let concat_dim = dim * 3
  let total = g.n_nodes * concat_dim
  let z : Array[Float] = Array::make(total, 0.0F)
  for v in 0..<g.n_nodes {
    let src = v * dim
    let dst = v * concat_dim
    for k in 0..<dim {
      z[dst + k] = ae[src + k]
      z[dst + dim + k] = an[src + k]
      z[dst + 2 * dim + k] = ag[src + k]
    }
  }
  graph_linear_forward(net.combine, z, g.n_nodes)
}

///|
/// Number of parameters in one H2GCNLayer.
pub fn h2gcn_layer_num_params(layer : H2GCNLayer) -> Int {
  layer.w_ego.in_dim * layer.w_ego.out_dim + layer.w_ego.out_dim +
  layer.w_neigh.in_dim * layer.w_neigh.out_dim + layer.w_neigh.out_dim +
  layer.w_high.in_dim * layer.w_high.out_dim + layer.w_high.out_dim
}

///|
/// Total parameter count of the stack, including the final combine.
pub fn h2gcn_num_params(net : H2GCN) -> Int {
  let mut total = net.combine.in_dim * net.combine.out_dim +
    net.combine.out_dim
  for l in 0..<net.num_layers {
    total = total + h2gcn_layer_num_params(net.layers[l])
  }
  total
}

///|
/// Edge homophily: the fraction of edges whose endpoints carry the
/// same label. 1.0 is fully homophilous, 0.0 means no edge is
/// intra-class, values below ~0.3 indicate a heterophilous dataset.
///
/// Out-of-range edge endpoints are skipped rather than counted, so a
/// malformed label array lowers the denominator.
pub fn edge_homophily(
  g : Graph,
  labels : Array[Int],
  n_nodes : Int,
) -> Float {
  if g.n_edges == 0 {
    return 0.0F
  }
  let mut same = 0
  let mut counted = 0
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= n_nodes || d < 0 || d >= n_nodes {
      continue
    }
    if s >= labels.length() || d >= labels.length() {
      continue
    }
    counted = counted + 1
    if labels[s] == labels[d] {
      same = same + 1
    }
  }
  if counted == 0 {
    return 0.0F
  }
  Float::from_int(same) / Float::from_int(counted)
}

///|
/// Node homophily: the fraction of non-isolated nodes ALL of whose
/// in-neighbours share that node's label. Stricter than edge
/// homophily -- a node with 9 same-label and 1 other-class neighbour
/// counts as heterophilous here but contributes 9/10 to edge
/// homophily. Isolated nodes are excluded from both numerator and
/// denominator.
///
/// One pass over the edge list sets a per-node mismatch flag, so the
/// cost is O(n_edges + n_nodes) rather than the O(n_nodes * n_edges)
/// a per-node rescan of the edge list would cost.
pub fn node_homophily(
  g : Graph,
  labels : Array[Int],
  n_nodes : Int,
) -> Float {
  let deg = graph_in_degree(g)
  let mismatched : Array[Int] = Array::make(n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= n_nodes || d < 0 || d >= n_nodes {
      continue
    }
    if s >= labels.length() || d >= labels.length() {
      continue
    }
    if labels[s] != labels[d] {
      mismatched[d] = 1
    }
  }
  let mut ok = 0
  let mut total = 0
  for v in 0..<n_nodes {
    if deg[v] == 0 || v >= labels.length() {
      continue
    }
    total = total + 1
    if mismatched[v] == 0 {
      ok = ok + 1
    }
  }
  if total == 0 {
    return 0.0F
  }
  Float::from_int(ok) / Float::from_int(total)
}

///|
/// In-degree histogram with `n_bins` equal-width bins over
/// [0, max_in_degree]. This is the distribution that PNA's degree
/// scaler is tuned against: a very skewed histogram is exactly the
/// case where amplification vs attenuation matters.
pub fn degree_histogram(g : Graph, n_bins : Int) -> Array[Int] {
  let hist : Array[Int] = Array::make(n_bins, 0)
  if n_bins == 0 {
    return hist
  }
  let deg = graph_in_degree(g)
  let mut max_deg = 0
  for i in 0..<g.n_nodes {
    if deg[i] > max_deg {
      max_deg = deg[i]
    }
  }
  let span = max_deg + 1
  for i in 0..<g.n_nodes {
    let b = deg[i] * n_bins / span
    let b = if b >= n_bins { n_bins - 1 } else { b }
    hist[b] = hist[b] + 1
  }
  hist
}
