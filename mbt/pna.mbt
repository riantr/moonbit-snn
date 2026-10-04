// pna.mbt -- Principal Neighbourhood Aggregation (v0.142.0).
//
// Reference: Corso, Cavalleri, Beaini, Liò, Veličković 2020,
// "Principal Neighbourhood Aggregation for Graph Neural Networks",
// NeurIPS.
//
// PNA attacks the weakness every aggregator in v0.137.0 / v0.141.0
// shares: a single reduction (mean, sum, max) discards information
// about the *shape* of a neighbourhood. A node with neighbours
// {0, 0} and a node with neighbours {0, 1} have the same max, and
// a node with neighbours {1, 1, 1} and {-1, -1, -1} differ in sign
// but not in max. PNA concatenates four complementary reductions —
//
//   mean  = (1/d) sum_u h_u          order-sensitive magnitude
//   max   = max_u h_u                upper envelope
//   min   = min_u h_u                lower envelope
//   std   = sqrt(E[x^2] - E[x]^2)    spread
//
// — so a downstream Linear sees the whole first two moments of the
// neighbour multiset instead of one summary of it.
//
// On top of that, PNA rescales each aggregate by the node's degree.
// High-degree hubs and low-degree leaves have wildly different
// neighbour-count statistics, and a fixed aggregation weight makes
// the two populations incomparable. Three scalers are provided:
// identity, amplification (log(d+1) up-weighting hubs), and
// attenuation (the inverse).
//
// Scope of v0.142.0:
//   - PNAAggregator / PNAScaler enums.
//   - pna_aggregate: mean / max / min / std over the in-neighbour
//     multiset, all UNWEIGHTED (the paper aggregates a multiset, not
//     a weighted adjacency).
//   - pna_degree_scale: the degree scaler.
//   - PNALayer: concat([h, mean, max, min, std]) -> 2-layer MLP.
//   - PNA: an L-layer stack.
//   - pna_forward + pna_num_params + pna_mean_in_degree.
//
// Convention: the four aggregators are always concatenated in the
// fixed order mean, max, min, std, so the first Linear's input width
// is always 5 * in_dim. A layer is not built from a configurable
// subset, because varying that width per layer is the fastest way to
// produce a silent shape mismatch that `moon check` cannot see.

///|
/// The neighbourhood reductions PNA concatenates.
pub(all) enum PNAAggregator {
  Mean
  Max
  Min
  Std
} derive(Eq, Debug)

///|
/// How a per-node aggregate is rescaled by the node's degree.
pub(all) enum PNAScaler {
  /// No degree dependence.
  Identity
  /// scale = log(d + 1) / mean(d): up-weights high-degree hubs.
  Amplification
  /// scale = mean(d) / log(d + 1): down-weights high-degree hubs.
  Attenuation
} derive(Eq, Debug)

///|
/// helper: number of distinct aggregators concatenated by a layer.
pub fn pna_num_aggregators() -> Int {
  4
}

///|
/// helper: raw (unweighted) sums of the in-neighbour features, plus
/// the per-node in-degree. Shared by the mean and std reducers.
fn pna_sum_and_deg(
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Int]) {
  let dim = g.feat_dim
  let sum : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let deg : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * dim
    let dst_off = d * dim
    for k in 0..<dim {
      sum[dst_off + k] = sum[dst_off + k] + h[src_off + k]
    }
    deg[d] = deg[d] + 1
  }
  (sum, deg)
}

///|
/// Mean over the in-neighbour multiset. Isolated nodes get a zero row.
pub fn pna_mean(g : Graph, h : Array[Float]) -> Array[Float] {
  let dim = g.feat_dim
  let (sum, deg) = pna_sum_and_deg(g, h)
  for v in 0..<g.n_nodes {
    if deg[v] == 0 {
      continue
    }
    let inv = 1.0F / Float::from_int(deg[v])
    let off = v * dim
    for k in 0..<dim {
      sum[off + k] = sum[off + k] * inv
    }
  }
  sum
}

///|
/// Max over the in-neighbour multiset. Isolated nodes get a zero row.
///
/// Note the seeding: the accumulator is seeded from each node's FIRST
/// in-edge, NOT from 0.0, so a node whose every neighbour is negative
// still reports its (negative) maximum. Initialising to 0.0 instead
// would silently clamp every all-negative neighbourhood to 0.
pub fn pna_max(g : Graph, h : Array[Float]) -> Array[Float] {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * dim
    let dst_off = d * dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<dim {
      let m = h[src_off + k]
      if first || m > out[dst_off + k] {
        out[dst_off + k] = m
      }
    }
  }
  out
}

///|
/// Min over the in-neighbour multiset. Isolated nodes get a zero row.
pub fn pna_min(g : Graph, h : Array[Float]) -> Array[Float] {
  let dim = g.feat_dim
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  let seeded : Array[Int] = Array::make(g.n_nodes, 0)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * dim
    let dst_off = d * dim
    let first = seeded[d] == 0
    seeded[d] = 1
    for k in 0..<dim {
      let m = h[src_off + k]
      if first || m < out[dst_off + k] {
        out[dst_off + k] = m
      }
    }
  }
  out
}

///|
/// Standard deviation over the in-neighbour multiset, computed as
///   sqrt( E[x^2] - E[x]^2 )
/// and clamped at 0, because the two moments are subtracted in
/// Float32 and can land a few ULP below zero. Isolated nodes get a
/// zero row (a single-element neighbourhood has zero spread, and an
/// empty one is defined as 0 to match the other reducers).
pub fn pna_std(g : Graph, h : Array[Float]) -> Array[Float] {
  let dim = g.feat_dim
  let (sum, deg) = pna_sum_and_deg(g, h)
  let sq : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let d = g.edge_dst[e]
    if s < 0 || s >= g.n_nodes || d < 0 || d >= g.n_nodes {
      continue
    }
    let src_off = s * dim
    let dst_off = d * dim
    for k in 0..<dim {
      let x = h[src_off + k]
      sq[dst_off + k] = sq[dst_off + k] + x * x
    }
  }
  let out : Array[Float] = Array::make(g.n_nodes * dim, 0.0F)
  for v in 0..<g.n_nodes {
    if deg[v] == 0 {
      continue
    }
    let inv = 1.0F / Float::from_int(deg[v])
    let off = v * dim
    for k in 0..<dim {
      let m = sum[off + k] * inv
      let e2 = sq[off + k] * inv
      let variance = e2 - m * m
      if variance > 0.0F {
        out[off + k] = sqrtf(variance)
      }
    }
  }
  out
}

///|
/// Reduce the in-neighbour multiset with the chosen aggregator.
pub fn pna_aggregate(
  g : Graph,
  h : Array[Float],
  agg : PNAAggregator,
) -> Array[Float] {
  match agg {
    Mean => pna_mean(g, h)
    Max => pna_max(g, h)
    Min => pna_min(g, h)
    Std => pna_std(g, h)
  }
}

///|
/// Mean in-degree over the graph, floored at `delta`. The paper uses
/// a small delta (0.001 by default) to stop the normalisation from
/// blowing up on a near-empty graph.
pub fn pna_mean_in_degree(g : Graph, delta : Float) -> Float {
  let deg = graph_in_degree(g)
  let mut total = 0
  for i in 0..<g.n_nodes {
    total = total + deg[i]
  }
  let m = Float::from_int(total) / Float::from_int(g.n_nodes)
  if m < delta { delta } else { m }
}

///|
/// Per-node degree scaler. `mean_deg` should come from
/// `pna_mean_in_degree`.
///
/// Amplification is log(d+1)/mean(d), attenuation is its reciprocal.
/// Degree 0 is special-cased to a scale of 1.0 under attenuation:
/// log(0+1) = 0 would otherwise divide by zero, and an isolated node
/// has no neighbour statistics to attenuate anyway.
pub fn pna_degree_scale(
  scaler : PNAScaler,
  deg : Int,
  mean_deg : Float,
) -> Float {
  match scaler {
    Identity => 1.0F
    Amplification => {
      let d = Float::from_int(deg)
      logf(d + 1.0F) / mean_deg
    }
    Attenuation => {
      if deg == 0 {
        1.0F
      } else {
        let d = Float::from_int(deg)
        mean_deg / logf(d + 1.0F)
      }
    }
  }
}

///|
/// A single PNA layer: five scaled blocks (self + 4 aggregations)
/// concatenated and fed to a 2-layer MLP.
pub struct PNALayer {
  in_dim : Int
  hidden_dim : Int
  out_dim : Int
  delta : Float
  scaler : PNAScaler
  pre_lin : GraphLinear
  post_lin : GraphLinear
}

///|
/// Build a PNALayer.
pub fn PNALayer::new(
  in_dim : Int,
  hidden_dim : Int,
  out_dim : Int,
  delta : Float,
  scaler : PNAScaler,
  seed : UInt64,
) -> PNALayer {
  let n_agg = pna_num_aggregators()
  let concat_dim = in_dim * (n_agg + 1)
  {
    in_dim,
    hidden_dim,
    out_dim,
    delta,
    scaler,
    pre_lin: GraphLinear::new(concat_dim, hidden_dim, seed),
    post_lin: GraphLinear::new(hidden_dim, out_dim, seed + 10UL),
  }
}

///|
/// Forward one PNA layer:
///   z_v = concat( scale_v * h_v, scale_v * mean, scale_v * max,
///                      scale_v * min, scale_v * std )
///   h_v' = act( W_2 · relu( W_1 · z_v + b_1 ) + b_2 )
/// The SAME per-node scale multiplies all five blocks, matching the
/// paper (the scaler is a property of the node's degree, not of the
/// individual reduction).
pub fn pna_layer_forward(
  layer : PNALayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let dim = g.feat_dim
  let n_agg = pna_num_aggregators()
  let aggs = [
    pna_mean(g, h),
    pna_max(g, h),
    pna_min(g, h),
    pna_std(g, h),
  ]
  let in_deg = graph_in_degree(g)
  let mean_deg = pna_mean_in_degree(g, layer.delta)
  let concat_dim = dim * (n_agg + 1)
  let z : Array[Float] = Array::make(g.n_nodes * concat_dim, 0.0F)
  for v in 0..<g.n_nodes {
    let scale = pna_degree_scale(layer.scaler, in_deg[v], mean_deg)
    let src_off = v * dim
    let dst_off = v * concat_dim
    // Block 0 is the node's own features.
    for k in 0..<dim {
      z[dst_off + k] = scale * h[src_off + k]
    }
    // Blocks 1..n_agg are the aggregations, in fixed order.
    for a in 0..<n_agg {
      let base = dst_off + dim * (a + 1)
      for k in 0..<dim {
        z[base + k] = scale * aggs[a][src_off + k]
      }
    }
  }
  let hid = graph_linear_forward(layer.pre_lin, z, g.n_nodes)
  let hid_act = graph_relu(hid)
  let out = graph_linear_forward(layer.post_lin, hid_act, g.n_nodes)
  graph_relu(out)
}

///|
/// Number of parameters in one PNALayer.
pub fn pna_layer_num_params(layer : PNALayer) -> Int {
  layer.pre_lin.in_dim * layer.pre_lin.out_dim + layer.pre_lin.out_dim +
  layer.post_lin.in_dim * layer.post_lin.out_dim + layer.post_lin.out_dim
}

///|
/// Stack of L PNA layers.
pub struct PNA {
  layers : Array[PNALayer]
  num_layers : Int
  scaler : PNAScaler
  delta : Float
  out_dim : Int
}

///|
/// Build an L-layer PNA. `dims` must have `num_layers + 1` entries.
/// The default scaler is Amplification, which is the paper's
/// recommended setting for most benchmarks.
pub fn PNA::new(
  dims : Array[Int],
  hidden_dim : Int,
  delta : Float,
  scaler : PNAScaler,
  seed : UInt64,
) -> PNA {
  let num_layers = dims.length() - 1
  let layers : Array[PNALayer] = Array::make(
    num_layers,
    PNALayer::new(dims[0], hidden_dim, dims[1], delta, scaler, seed),
  )
  for l in 0..<num_layers {
    layers[l] = PNALayer::new(
      dims[l],
      hidden_dim,
      dims[l + 1],
      delta,
      scaler,
      seed + (l + 1).to_uint64() * 1000UL,
    )
  }
  { layers, num_layers, scaler, delta, out_dim: dims[num_layers], }
}

///|
/// Forward the full PNA stack. Every layer applies its ReLU,
/// including the last.
pub fn pna_forward(net : PNA, g : Graph, h : Array[Float]) -> Array[Float] {
  let mut acc = pna_layer_forward(net.layers[0], g, h)
  for l in 1..<net.num_layers {
    acc = pna_layer_forward(net.layers[l], g, acc)
  }
  acc
}

///|
/// Total parameter count of the stack.
pub fn pna_num_params(net : PNA) -> Int {
  let mut total = 0
  for l in 0..<net.num_layers {
    total = total + pna_layer_num_params(net.layers[l])
  }
  total
}
