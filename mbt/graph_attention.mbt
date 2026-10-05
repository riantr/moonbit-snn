// graph_attention.mbt -- Graph Attention Network (v0.139.0).
//
// GAT (Velickovic et al. 2018) replaces the fixed mean/sum aggregation
// of GCN with a learned attention coefficient per edge:
//
//   e_uv = a_L^T [W h_u || W h_v]
//   alpha_uv = softmax_v(e_uv)        (softmax over the IN-neighbours)
//   h'_v = sigma( sum_u alpha_uv · W h_u )
//
// where `||` is concatenation. The attention head `a` is itself
// learnable, which is what lets GAT focus on important neighbours
// instead of averaging all of them.
//
// Multi-head attention concatenates K independent heads (rather than
// averaging them) to keep the output dimension stable.
//
// Scope of v0.139.0:
//   - GATLayer (single head) + gat_layer_forward.
//   - GraphAttention (multi-head stack) + graph_attention_forward.
//   - LeakyReLU (slope 0.2, the GAT default).
//
// Reference: Velickovic et al. 2018.

///|
/// LeakyReLU with slope 0.2 (the GAT default negative slope).
pub fn gat_leaky_relu(x : Float) -> Float {
  if x > 0.0F { x } else { 0.2F * x }
}

///|
/// GATLayer: one attention head mapping in_dim -> out_dim.
pub struct GATLayer {
  in_dim : Int
  out_dim : Int
  // Shared linear projection W: (out_dim x in_dim).
  w : Array[Array[Float]]
  // Attention vector a, split into a_src and a_dst halves so the
  // score e_uv = a_src . Wh_u + a_dst . Wh_v.
  a_src : Array[Float]
  a_dst : Array[Float]
}

///|
/// Build a GATLayer with xavier-normal init.
pub fn GATLayer::new(
  in_dim : Int,
  out_dim : Int,
  seed : UInt64,
) -> GATLayer {
  let std = sqrtf(2.0F / Float::from_int(in_dim))
  let rng = Xoshiro::from_state(
    seed + 10UL, seed + 11UL, seed + 12UL, seed + 13UL,
  )
  let w = xavier_normal(out_dim, in_dim, std, rng)
  let a_src : Array[Float] = Array::make(out_dim, 0.0F)
  let a_dst : Array[Float] = Array::make(out_dim, 0.0F)
  for i in 0..<out_dim {
    a_src[i] = next_f32(rng) * 2.0F - 1.0F
    a_dst[i] = next_f32(rng) * 2.0F - 1.0F
  }
  { in_dim, out_dim, w, a_src, a_dst }
}

///|
/// Forward one GAT attention head:
///   h'_v = sum_{u in N(v)} alpha_uv · W h_u
///
/// The attention coefficients are softmax-normalised over the
/// in-neighbours of each destination node. Nodes with no incoming
/// edge keep a zero row.
pub fn gat_layer_forward(
  layer : GATLayer,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let n = g.n_nodes
  let d = layer.out_dim
  // Project every node: p = W h, flat [n x d].
  let p : Array[Float] = Array::make(n * d, 0.0F)
  for v in 0..<n {
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
  // Pre-compute the two attention terms per node.
  let t_src : Array[Float] = Array::make(n, 0.0F)
  let t_dst : Array[Float] = Array::make(n, 0.0F)
  for v in 0..<n {
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
  // Per-edge attention logits, then a stable softmax per destination.
  let logits : Array[Float] = Array::make(g.n_edges, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let dst = g.edge_dst[e]
    if s < 0 || s >= n || dst < 0 || dst >= n {
      logits[e] = 0.0F
    } else {
      logits[e] = t_src[s] + t_dst[dst]
    }
  }
  let deg = graph_in_degree(g)
  let alpha : Array[Float] = Array::make(g.n_edges, 0.0F)
  for v in 0..<n {
    if deg[v] == 0 {
      continue
    }
    // Max over incoming edges for numerical stability.
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
  // Weighted sum of the projected neighbours.
  let out : Array[Float] = Array::make(n * d, 0.0F)
  for e in 0..<g.n_edges {
    let s = g.edge_src[e]
    let dst = g.edge_dst[e]
    if s < 0 || s >= n || dst < 0 || dst >= n {
      continue
    }
    let a = alpha[e]
    let src_off = s * d
    let dst_off = dst * d
    for o in 0..<d {
      out[dst_off + o] = out[dst_off + o] + a * p[src_off + o]
    }
  }
  out
}

///|
/// GraphAttention: a stack of multi-head GAT layers. Within a layer
/// the K heads are CONCATENATED (K * out_dim total), which is the
/// multi-head convention in the GAT paper. Hidden layers apply an
/// ELU activation; the final layer does not.
pub struct GraphAttention {
  num_heads : Int
  num_layers : Int
  hidden_dim : Int
  out_dim : Int
  // layers[layer] is a flat [num_heads * head_dim] block; we store
  // heads as Array[Array[GATLayer]] indexed [layer][head].
  layers : Array[Array[GATLayer]]
}

///|
/// Build a multi-head GAT stack. `dims` has `num_layers + 1` entries:
///
///   dims[0]        the raw INPUT width
///   dims[l]        the PER-HEAD output width of layer l-1, for l >= 1
///
/// The per-head qualifier is the whole point. Multi-head attention
/// CONCATENATES its heads, so layer l does not read `dims[l]` -- it
/// reads `num_heads * dims[l]`. Feeding layer l a bare `dims[l]` built
/// a stack whose declared input width was `num_heads` times too small;
/// `gat_layer_forward` then re-strided the concatenated buffer with the
/// layer's own (small) `in_dim` and read the wrong elements, silently,
/// for every layer past the first. With `num_heads == 1` the two agree,
/// which is why this only ever bit multi-layer multi-head stacks --
/// the one shape nothing in the package had built until now.
pub fn GraphAttention::new(
  dims : Array[Int],
  num_heads : Int,
  seed : UInt64,
) -> GraphAttention {
  let num_layers = dims.length() - 1
  let layers : Array[Array[GATLayer]] = Array::make(num_layers, [])
  for l in 0..<num_layers {
    // Concatenation, not averaging: layer l's input width is the
    // concatenated width of layer l-1, not its per-head width.
    let in_dim = if l == 0 { dims[0] } else { num_heads * dims[l] }
    let out_dim = dims[l + 1]
    let heads : Array[GATLayer] = Array::make(
      num_heads, GATLayer::new(in_dim, out_dim, seed),
    )
    for hd in 0..<num_heads {
      heads[hd] = GATLayer::new(
        in_dim,
        out_dim,
        seed + (l * num_heads + hd + 1).to_uint64() * 1000UL,
      )
    }
    layers[l] = heads
  }
  { num_heads, num_layers, hidden_dim: dims[1], out_dim: dims[num_layers], layers, }
}

///|
/// Per-head output width of stack layer `l`. Every stride in the stack
/// goes through here, because "the width of layer l's output" has two
/// different answers -- `out_dim` for one head, `num_heads * out_dim`
/// for the concatenated row -- and picking the wrong one is invisible:
/// the arithmetic still runs, the factors still look orthonormal, and
/// the loss still decreases. (That is what happened in Batch X.)
pub fn gat_stack_out_width(net : GraphAttention, l : Int) -> Int {
  net.layers[l][0].out_dim
}

///|
/// Node-embedding width of a multi-head GAT stack: the heads are
/// concatenated, so the node row is `num_heads` times the last layer's
/// per-head width.
pub fn gat_stack_node_dim(net : GraphAttention) -> Int {
  net.num_heads * gat_stack_out_width(net, net.num_layers - 1)
}

///|
/// helper: ELU activation (x > 0 ? x : exp(x) - 1), the GAT hidden
/// activation.
pub fn gat_elu(x : Float) -> Float {
  if x > 0.0F { x } else { expf(x) - 1.0F }
}

///|
/// Forward the multi-head GAT stack, caching each layer's input.
/// Returns `(output, inputs)` where `inputs[l]` is the buffer layer l
/// reads -- the concatenated output of layer l-1.
///
/// This is the real implementation; `graph_attention_forward` is a
/// wrapper over it. Having one body rather than two is deliberate: the
/// backward pass slices `d_out` by the same per-layer widths this
/// function writes, and a second copy of the layout arithmetic is
/// exactly how a backward ends up disagreeing with its own forward.
pub fn graph_attention_forward_with_inputs(
  net : GraphAttention,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Array[Array[Float]]) {
  let heads = net.num_heads
  let n_layers = net.num_layers
  let inputs : Array[Array[Float]] = Array::make(n_layers, [])
  let mut acc = h
  let mut i = 0
  while i < n_layers {
    let d = gat_stack_out_width(net, i)
    // The LAST layer is linear so its output can be read as logits;
    // every earlier layer concatenates its heads and applies ELU.
    let last = i == n_layers - 1
    inputs[i] = acc
    let out : Array[Float] = Array::make(g.n_nodes * heads * d, 0.0F)
    for hd in 0..<heads {
      let ho = gat_layer_forward(net.layers[i][hd], g, acc)
      for v in 0..<g.n_nodes {
        // `ho` is this head's OWN output, laid out with the head's
        // out_dim as its row stride. It is emphatically not the layer's
        // in_dim and not the concatenated width: indexing it by either
        // reads the wrong elements, and reads the wrong ones SILENTLY
        // whenever out_dim happens to differ from them -- which is why
        // a 1-head stack whose in_dim equals its out_dim passed while
        // every real multi-head width failed.
        let src = v * d
        let dst = v * heads * d + hd * d
        for k in 0..<d {
          let hv = ho[src + k]
          if last {
            out[dst + k] = hv
          } else {
            out[dst + k] = gat_elu(hv)
          }
        }
      }
    }
    acc = out
    i = i + 1
  }
  (acc, inputs)
}

///|
/// Forward the multi-head GAT stack. Hidden layers apply ELU to the
/// concatenated head outputs; the final layer is linear so its output
/// can be used directly as logits.
///
/// Concatenation means a layer's output width is `num_heads * dim_l`,
/// so the input width of layer l+1 must equal `num_heads * dim_l`.
/// `GraphAttention::new` builds exactly that; this function does not
/// have to know the rule because every stride is read off the layer it
/// is slicing.
pub fn graph_attention_forward(
  net : GraphAttention,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let (out, _) = graph_attention_forward_with_inputs(net, g, h)
  out
}