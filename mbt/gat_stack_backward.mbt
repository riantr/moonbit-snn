// gat_stack_backward.mbt -- backward for the MULTI-HEAD GAT stack
// (v0.154.0).
//
// v0.153.0 gave a single GAT head a gradient. The stack around it had
// none, and the gap was not "wire the layers up" -- there were three
// structural facts the head backward knew nothing about:
//
//   1. CONCATENATION. Heads are concatenated, not averaged, so one node
//      row is `num_heads * head_width` wide and head hd owns the slice
//      `[hd*w, (hd+1)*w)`. Every upstream gradient therefore arrives as
//      one interleaved buffer and has to be UNINTERLEAVED before any of
//      the head code can run. A stack backward that sliced by the
//      concatenated width instead would hand head hd the gradients of
//      two nodes at once and pass the gate's shape checks.
//
//   2. SHARED INPUT. All heads of a layer read the SAME buffer, so
//      their input gradients SUM. This is the opposite of the head
//      backward's own accumulation (which sums over NODES into d_W);
//      getting it backwards produces a gradient scaled by the head
//      count, which is largest exactly where attention is most
//      confident.
//
//   3. THE ACTIVATION IS BETWEEN HEADS AND THE NEXT LAYER, NOT
//      AHEAD OF ONE. `graph_attention_forward` writes
//      `elu(head_output)` into the concatenated buffer, so the ELU
//      derivative multiplies the gradient of THAT buffer at the head's
//      own pre-activation -- once per hidden layer, and not at all on
//      the final layer, which is linear so its output can be read as
//      logits. `gat_layer_backward`'s `elu` flag is the single place
//      that knows this.
//
// Two of the three are "wrong by a scale factor", which is why the
// gradient gate is the only instrument that catches them: a stack
// backward with no concatenation, no head-sum, or no ELU mask still
// compiles, still runs, still produces a plausible-looking parameter
// vector -- it is simply the gradient of a different network.

// ---------------------------------------------------------------------------
// Gradients
// ---------------------------------------------------------------------------

///|
/// Parameter gradients for a whole multi-head GAT stack, indexed
/// `[layer][head]` to match `GraphAttention::layers`.
pub struct GraphAttentionGrad {
  layers : Array[Array[GATLayerGrad]]
  num_layers : Int
}

///|
/// Zero-initialised gradients for a stack.
///
/// The double seeding is deliberate and necessary, not sloppy:
/// `Array::make(n, x)` stores n REFERENCES to one value in MoonBit, so
/// seeding an outer array with one shared inner array would alias every
/// layer's head list onto layer 0's, and seeding the head list with one
/// shared `GATLayerGrad` would alias every head's `d_w` rows onto
/// head 0's. Both would pass a shape check and produce a gradient
/// multiplied by the layer and head counts. Every slot below is
/// overwritten in the loop, so the seed value itself is discarded.
pub fn GraphAttentionGrad::zero(net : GraphAttention) -> GraphAttentionGrad {
  let layers : Array[Array[GATLayerGrad]] = Array::make(
    net.num_layers, Array::make(0, GATLayerGrad::zero(net.layers[0][0])),
  )
  for l in 0..<net.num_layers {
    let heads : Array[GATLayerGrad] = Array::make(
      net.num_heads, GATLayerGrad::zero(net.layers[l][0]),
    )
    for hd in 0..<net.num_heads {
      heads[hd] = GATLayerGrad::zero(net.layers[l][hd])
    }
    layers[l] = heads
  }
  { layers, num_layers: net.num_layers, }
}

// ---------------------------------------------------------------------------
// Layer and stack backward
// ---------------------------------------------------------------------------

///|
/// Backward of one layer of a multi-head stack: split the interleaved
/// upstream gradient into per-head slices, run each head's backward, and
/// sum the heads' input gradients. Returns `(d_input, head_grads)`.
///
/// `d_up` is the concatenated gradient on this layer's OUTPUT, width
/// `num_heads * out_dim` per node -- the same interleaved layout the
/// forward wrote. `elu` is true for hidden layers, where the forward
/// applied ELU to each head's slice on its way into that buffer.
pub fn graph_attention_layer_backward(
  net : GraphAttention,
  l : Int,
  g : Graph,
  h : Array[Float],
  d_up : Array[Float],
  elu : Bool,
) -> (Array[Float], Array[GATLayerGrad]) {
  let heads = net.num_heads
  let head0 = net.layers[l][0]
  let d = head0.out_dim
  let row = heads * d
  // The heads SHARE their input, so their input gradients add.
  let d_input : Array[Float] = Array::make(g.n_nodes * head0.in_dim, 0.0F)
  let grads : Array[GATLayerGrad] = Array::make(
    heads, GATLayerGrad::zero(head0),
  )
  for hd in 0..<heads {
    // Un-interleave: head hd's gradient is the [hd*d, (hd+1)*d) slice
    // of every node's row, NOT a contiguous [n*d) block.
    let d_head : Array[Float] = Array::make(g.n_nodes * d, 0.0F)
    for v in 0..<g.n_nodes {
      let src = v * row + hd * d
      let dst = v * d
      for k in 0..<d {
        d_head[dst + k] = d_up[src + k]
      }
    }
    let (dh, gh) = gat_layer_backward(net.layers[l][hd], g, h, d_head, elu)
    add_into(d_input, dh)
    grads[hd] = gh
  }
  (d_input, grads)
}

///|
/// Backward of a whole multi-head GAT stack. Returns
/// `(d_input, grads)`.
///
/// `inputs` is what `graph_attention_forward_with_inputs` returned: the
/// buffer each layer read. Layers are walked in REVERSE order, for the
/// reason given on `gin_backward` -- layer l's input gradient does not
/// exist until layer l+1 has produced it.
pub fn graph_attention_backward(
  net : GraphAttention,
  g : Graph,
  inputs : Array[Array[Float]],
  d_out : Array[Float],
) -> (Array[Float], GraphAttentionGrad) {
  let grads = GraphAttentionGrad::zero(net)
  let n_layers = net.num_layers
  let mut d = d_out
  let mut i = 0
  while i < n_layers {
    let l = n_layers - 1 - i
    // The last layer is the only linear one, so the only one that does
    // not mask its gradient through ELU.
    let elu = l < n_layers - 1
    let (d_prev, gl) = graph_attention_layer_backward(
      net, l, g, inputs[l], d, elu,
    )
    grads.layers[l] = gl
    d = d_prev
    i = i + 1
  }
  (d, grads)
}

///|
/// Full training step for a multi-head GAT stack: forward, backward, SGD
/// on every head of every layer. Returns `(new_net, d_input)`.
pub fn graph_attention_sgd_step(
  net : GraphAttention,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
  lr : Float,
) -> (GraphAttention, Array[Float]) {
  let (_, inputs) = graph_attention_forward_with_inputs(net, g, h)
  let (d_input, grads) = graph_attention_backward(net, g, inputs, d_out)
  let layers : Array[Array[GATLayer]] = Array::make(net.num_layers, [])
  for l in 0..<net.num_layers {
    let heads : Array[GATLayer] = Array::make(net.num_heads, net.layers[l][0])
    for hd in 0..<net.num_heads {
      heads[hd] = gat_layer_sgd_step(
        net.layers[l][hd], grads.layers[l][hd], lr,
      )
    }
    layers[l] = heads
  }
  ({ ..net, layers, }, d_input)
}

///|
/// Parameter count of a multi-head GAT stack. Each head carries its
/// projection `W` (in_dim x out_dim) and the two attention vectors
/// `a_src`, `a_dst`; there is no bias, because `GATLayer` has none.
pub fn graph_attention_num_params(net : GraphAttention) -> Int {
  let mut total = 0
  for l in 0..<net.num_layers {
    for hd in 0..<net.num_heads {
      let layer = net.layers[l][hd]
      total = total + layer.in_dim * layer.out_dim + 2 * layer.out_dim
    }
  }
  total
}

// ---------------------------------------------------------------------------
// Interop with the GraphLinear-shaped probes
// ---------------------------------------------------------------------------

///|
/// A `GATLayer`'s projection seen as a `GraphLinear`, so the generic
/// parameter probes in gnn_paramcheck.mbt can target a GAT without a
/// second probe kind.
///
/// This is a VIEW: `w` is shared by reference and the synthetic bias is
/// all zeros, because `GATLayer` genuinely has no bias term. Reading it
/// is fine; perturbing it through this value would corrupt the model,
/// which is why `gat_layer_perturb_first` below deep-copies instead.
pub fn gat_layer_as_linear(layer : GATLayer) -> GraphLinear {
  {
    in_dim: layer.in_dim,
    out_dim: layer.out_dim,
    w: layer.w,
    b: Array::make(layer.out_dim, 0.0F),
  }
}

///|
/// A copy of `layer` whose projection element [0][0] is shifted by
/// `delta`. Deep-copies the weight rows, so the caller's model is
/// untouched -- a shallow copy would let the finite-difference probe
/// modify the network it is differentiating.
pub fn gat_layer_perturb_first(layer : GATLayer, delta : Float) -> GATLayer {
  { ..layer, w: graph_linear_perturb_first(gat_layer_as_linear(layer), delta).w, }
}

// ---------------------------------------------------------------------------
// Evidence: is the ELU branch actually doing anything?
// ---------------------------------------------------------------------------

///|
/// Evidence about the ELU branch: how many hidden-layer head
/// pre-activations are at or below zero (where `gat_elu_grad` returns
/// `exp(x)` instead of 1 and therefore CHANGES the gradient), and the
/// range they span.
///
/// This exists because "the ELU mask is in the code path" and "the ELU
/// mask changes the answer" are different claims. ELU's derivative is
/// exactly 1 on its whole positive half-line, so if every hidden
/// pre-activation happens to be positive, a backward that omitted the
/// mask entirely would produce bit-identical numbers -- and a gradient
/// check would pass while proving nothing about ELU. The count makes
/// that coverage auditable; the range says why it came out the way it
/// did instead of leaving a bare zero to interpret. (It is not
/// hypothetical: the first run of this gate reported 0 active sites on
/// all four stack rows.)
///
/// Returns `(count, min_preact, max_preact)`. The min and max are 0.0
/// for a single-layer stack, which has no hidden layer at all.
pub fn gat_elu_coverage(
  net : GraphAttention,
  g : Graph,
  h : Array[Float],
) -> (Int, Float, Float) {
  let (_, inputs) = graph_attention_forward_with_inputs(net, g, h)
  let n_layers = net.num_layers
  let mut count = 0
  let mut lo = 0.0F
  let mut hi = 0.0F
  let mut seen = false
  let mut i = 0
  while i < n_layers - 1 {
    for hd in 0..<net.num_heads {
      let raw = gat_layer_forward(net.layers[i][hd], g, inputs[i])
      for j in 0..<raw.length() {
        let x = raw[j]
        if x <= 0.0F {
          count = count + 1
        }
        if !seen {
          lo = x
          hi = x
          seen = true
        } else {
          if x < lo {
            lo = x
          }
          if x > hi {
            hi = x
          }
        }
      }
    }
    i = i + 1
  }
  (count, lo, hi)
}
