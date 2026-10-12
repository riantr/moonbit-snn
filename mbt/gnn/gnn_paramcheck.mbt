// gnn_paramcheck.mbt -- Parameter-gradient checks for the
// message-passing backwards (v0.150.0).
//
// v0.149.0 checks dL/dh, the gradient with respect to the INPUT node
// features. That is the check that exercises the hard part -- the
// edge-list routing -- because a node's input gradient is its own
// share plus every share routed back from the nodes that read it. It
// is NOT a check of the weight gradients, though: a broken dL/dW can
// coexist with a perfect dL/dh, because dL/dW is accumulated over
// nodes inside `graph_linear_backward` and dL/dh is accumulated over
// edges outside it.
//
// So this file checks dL/dW for one weight element per architecture:
// the FIRST row, FIRST column of the FIRST layer's first projection.
// One element is enough to catch a transposed weight gradient, a
// missing bias term, a wrong sign, and a factor-of-n error -- the four
// ways a weight gradient actually goes wrong.
//
// The perturbation helper deep-copies the weight matrix before
// perturbing. MoonBit arrays are reference types, so a shallow copy
// would corrupt the caller's model in place and the "numerical" side
// would then be differencing against an already-modified network.

///|
/// The weight-gradient check for a tagged network: which parameter
/// the check targets.
pub(all) enum ParamProbe {
  /// GCN: the layer's single projection.
  GcnWeight
  /// GIN: the first Linear of the layer's 2-layer MLP.
  GinMlp1
  /// PNA: the first (pre-MLP) Linear.
  PnaPre
  /// MPNN: the self branch.
  MpnnSelf
} derive(Eq, Debug)

///|
/// helper: deep-copy a GraphLinear's weight matrix with element
/// [0][0] shifted by `delta`. The inner arrays are rebuilt rather than
/// copied by reference, so the original parameter is untouched.
pub fn graph_linear_perturb_first(
  lin : GraphLinear,
  delta : Float,
) -> GraphLinear {
  let w : Array[Array[Float]] = Array::make(lin.out_dim, [])
  for o in 0..<lin.out_dim {
    let row : Array[Float] = Array::make(lin.in_dim, 0.0F)
    for k in 0..<lin.in_dim {
      row[k] = lin.w[o][k]
    }
    w[o] = row
  }
  w[0][0] = w[0][0] + delta
  { ..lin, w }
}

///|
/// helper: the first Linear of a tagged network's first layer, which
/// is the parameter the probe targets.
pub fn trainable_net_first_linear(
  net : TrainableGraphNet,
) -> GraphLinear {
  match net {
    Gcn(m) => m.layers[0].w
    Gin(m) => m.layers[0].mlp1
    Pna(m) => m.layers[0].pre_lin
    Mpnn(m) => m.layers[0].w_self
    // GATLayer stores `w` as a bare array rather than a GraphLinear,
    // so head 0 of layer 0 is exposed as a zero-bias view of it.
    Gat(m) => gat_layer_as_linear(m.layers[0][0])
  }
}

///|
/// Analytic dL/dW[0][0] for the probed weight, given an arbitrary
/// upstream gradient `d_out` on the network's output.
///
/// Taking `d_out` as a parameter rather than deriving it from labels
/// is what lets the same routine serve the cross-entropy objective and
/// the quadratic one the gate actually uses. Under the quadratic
/// objective `d_out` is simply the output itself.
pub fn trainable_net_param_grad_from_dout(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  d_out : Array[Float],
) -> Array[Float] {
  let out : Array[Float] = Array::make(1, 0.0F)
  match net {
    Gcn(m) => {
      let (_, inputs) = gcn_forward_with_inputs(m, g, h)
      let (_, grads) = gcn_backward(m, g, inputs, d_out)
      out[0] = grads.layers[0].w.d_w[0][0]
    }
    Gin(m) => {
      let (_, inputs) = gin_forward_with_inputs(m, g, h)
      let (_, grads) = gin_backward(m, g, inputs, d_out)
      out[0] = grads.layers[0].mlp1.d_w[0][0]
    }
    Pna(m) => {
      let (_, inputs) = pna_forward_with_inputs(m, g, h)
      let (_, grads) = pna_backward(m, g, inputs, d_out)
      out[0] = grads.layers[0].pre_lin.d_w[0][0]
    }
    Mpnn(m) => {
      let (_, inputs) = mpnn_forward_with_inputs(m, g, h)
      let (_, grads) = mpnn_backward(m, g, inputs, d_out)
      out[0] = grads.layers[0].w_self.d_w[0][0]
    }
    Gat(m) => {
      let (_, inputs) = graph_attention_forward_with_inputs(m, g, h)
      let (_, grads) = graph_attention_backward(m, g, inputs, d_out)
      // Head 0 of layer 0 -- the probed parameter, for the same reason
      // every other architecture probes its first layer's projection.
      out[0] = grads.layers[0][0].d_w[0][0]
    }
  }
  out
}

///|
/// Check dL/dW[0][0] of the first-layer projection against central
/// differences of the QUADRATIC objective, judged relatively for the
/// same reason `gradcheck_input_relative` is: the weight gradient's
/// magnitude tracks the activation scale, which varies by three orders
/// of magnitude across these architectures.
///
/// Only two loss evaluations are needed, because only one weight is
/// perturbed -- this is the cheap half of the gate.
/// `corrupt` scales the ANALYTIC side before comparing. It exists for
/// one reason: the negative control. Passing 1.5 must produce a
/// FAILing report, which is the only evidence that this check can
/// reject anything at all. Leave it at the default for real checks.
pub fn gradcheck_param_relative(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  eps~ : Float = gradcheck_eps(),
  tol~ : Float = gradcheck_tol(),
  corrupt~ : Float = 1.0F,
) -> GradCheckReport {
  let out = trainable_net_forward(net, g, h)
  let analytic = trainable_net_param_grad_from_dout(net, g, h, out)
  if corrupt != 1.0F {
    analytic[0] = analytic[0] * corrupt
  }
  let plus = trainable_net_perturb_first(net, eps)
  let minus = trainable_net_perturb_first(net, 0.0F - eps)
  let l_plus = trainable_net_l2_loss(plus, g, h)
  let l_minus = trainable_net_l2_loss(minus, g, h)
  let numeric : Array[Float] = Array::make(1, 0.0F)
  numeric[0] = (l_plus - l_minus) / (2.0F * eps)
  let a0 = analytic[0]
  let scale = if a0 < 0.0F { 0.0F - a0 } else { a0 }
  let (max_diff, worst) = max_abs_diff(analytic, numeric)
  let eff = if scale < 0.000001F { tol } else { tol * scale }
  {
    name,
    max_diff,
    tol: eff,
    worst_index: worst,
    passed: max_diff <= eff,
  }
}

///|
/// The analytic dL/dW[0][0] of the probed weight, as a 1-element array
/// so it can go through the same `gradcheck_make_report` machinery as
/// the input check.
pub fn trainable_net_param_grad(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(1, 0.0F)
  match net {
    Gcn(m) => {
      let (_, inputs) = gcn_forward_with_inputs(m, g, h)
      let logits = gcn_forward(m, g, h)
      let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
      let (_, grads) = gcn_backward(m, g, inputs, d_logits)
      out[0] = grads.layers[0].w.d_w[0][0]
    }
    Gin(m) => {
      let (_, inputs) = gin_forward_with_inputs(m, g, h)
      let logits = gin_forward(m, g, h)
      let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
      let (_, grads) = gin_backward(m, g, inputs, d_logits)
      out[0] = grads.layers[0].mlp1.d_w[0][0]
    }
    Pna(m) => {
      let (_, inputs) = pna_forward_with_inputs(m, g, h)
      let logits = pna_forward(m, g, h)
      let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
      let (_, grads) = pna_backward(m, g, inputs, d_logits)
      out[0] = grads.layers[0].pre_lin.d_w[0][0]
    }
    Mpnn(m) => {
      let (_, inputs) = mpnn_forward_with_inputs(m, g, h)
      let logits = mpnn_forward(m, g, h)
      let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
      let (_, grads) = mpnn_backward(m, g, inputs, d_logits)
      out[0] = grads.layers[0].w_self.d_w[0][0]
    }
    Gat(m) => {
      let (_, inputs) = graph_attention_forward_with_inputs(m, g, h)
      let logits = graph_attention_forward(m, g, h)
      let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
      let (_, grads) = graph_attention_backward(m, g, inputs, d_logits)
      out[0] = grads.layers[0][0].d_w[0][0]
    }
  }
  out
}

///|
/// Return a tagged network whose probed weight has been shifted by
/// `delta`. The original is not modified.
pub fn trainable_net_perturb_first(
  net : TrainableGraphNet,
  delta : Float,
) -> TrainableGraphNet {
  match net {
    Gcn(m) => {
      let layers : Array[GCNLayer] = Array::make(m.num_layers, m.layers[0])
      for l in 0..<m.num_layers {
        layers[l] = { ..m.layers[l], }
      }
      layers[0] = {
        ..m.layers[0],
        w: graph_linear_perturb_first(m.layers[0].w, delta),
      }
      Gcn({ ..m, layers, })
    }
    Gin(m) => {
      let layers : Array[GINLayer] = Array::make(m.num_layers, m.layers[0])
      for l in 0..<m.num_layers {
        layers[l] = { ..m.layers[l], }
      }
      layers[0] = {
        ..m.layers[0],
        mlp1: graph_linear_perturb_first(m.layers[0].mlp1, delta),
      }
      Gin({ ..m, layers, })
    }
    Pna(m) => {
      let layers : Array[PNALayer] = Array::make(m.num_layers, m.layers[0])
      for l in 0..<m.num_layers {
        layers[l] = { ..m.layers[l], }
      }
      layers[0] = {
        ..m.layers[0],
        pre_lin: graph_linear_perturb_first(m.layers[0].pre_lin, delta),
      }
      Pna({ ..m, layers, })
    }
    Mpnn(m) => {
      let layers : Array[MPnnLayer] = Array::make(m.num_layers, m.layers[0])
      for l in 0..<m.num_layers {
        layers[l] = { ..m.layers[l], }
      }
      layers[0] = {
        ..m.layers[0],
        w_self: graph_linear_perturb_first(m.layers[0].w_self, delta),
      }
      Mpnn({ ..m, layers, })
    }
    Gat(m) => {
      let layers : Array[Array[GATLayer]] = Array::make(m.num_layers, [])
      for l in 0..<m.num_layers {
        let heads : Array[GATLayer] = Array::make(m.num_heads, m.layers[l][0])
        for hd in 0..<m.num_heads {
          // A fresh copy per head: sharing one perturbed layer across
          // heads would move `num_heads` parameters at once and make
          // the finite difference a different quantity.
          heads[hd] = { ..m.layers[l][hd], }
        }
        layers[l] = heads
      }
      let heads0 : Array[GATLayer] = Array::make(m.num_heads, m.layers[0][0])
      for hd in 0..<m.num_heads {
        heads0[hd] = { ..m.layers[0][hd], }
      }
      heads0[0] = gat_layer_perturb_first(m.layers[0][0], delta)
      layers[0] = heads0
      Gat({ ..m, layers, })
    }
  }
}

///|
/// Check dL/dW[0][0] of a tagged network's first-layer projection
/// against central differences.
pub fn gradcheck_param(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
  eps~ : Float = gradcheck_eps(),
  tol~ : Float = gradcheck_tol(),
) -> GradCheckReport {
  let analytic = trainable_net_param_grad(net, g, h, labels, num_classes)
  // Central difference on ONE weight, so only two loss evaluations.
  let plus = trainable_net_perturb_first(net, eps)
  let minus = trainable_net_perturb_first(net, 0.0F - eps)
  let l_plus = trainable_net_loss(plus, g, h, labels, num_classes)
  let l_minus = trainable_net_loss(minus, g, h, labels, num_classes)
  let numeric : Array[Float] = Array::make(1, 0.0F)
  numeric[0] = (l_plus - l_minus) / (2.0F * eps)
  gradcheck_make_report(name, analytic, numeric, tol)
}

///|
/// Relative form of the report: max_diff scaled by the magnitude of the
/// numerical gradient, so a check is not judged harder merely because
/// its gradient happens to be large. Returns 0.0 when the numerical
/// gradient is ~0 (a flat direction, where an absolute tolerance is the
/// only meaningful test).
pub fn gradcheck_relative(rep : GradCheckReport, analytic : Array[Float]) -> Float {
  let n = analytic.length()
  if n == 0 {
    return 0.0F
  }
  let mut scale = 0.0F
  for i in 0..<n {
    let a = if analytic[i] < 0.0F { 0.0F - analytic[i] } else { analytic[i] }
    if a > scale {
      scale = a
    }
  }
  if scale < 0.000001F {
    return 0.0F
  }
  rep.max_diff / scale
}
