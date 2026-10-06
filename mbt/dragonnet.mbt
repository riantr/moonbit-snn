// dragonnet.mbt -- minimal runnable DragonNet.
//
// Reference: C. Shi, D. M. Blei, V. Veitch, "Adapting Neural Networks for the
// Estimation of Treatment Effects", arXiv:1906.02120, and the authors' own
// implementation at github.com/claudiashi57/dragonnet
// (`src/experiment/models.py`). Loss formulas below are transcribed from that
// file, NOT from memory -- an earlier draft of this work wrote a "target trial
// loss" that does not exist in the reference, which is the reason this header
// names a source.
//
// Structure (the Keras class this file ports):
//
//   x -> encoder1(200, elu) -> encoder2(200, elu) -> encoder3(200, elu) -> phi
//   phi -> y0_h1(100, elu) -> y0_h2(100, elu) -> y0_out(1) -> y0
//   phi -> y1_h1(100, elu) -> y1_h2(100, elu) -> y1_out(1) -> y1
//   phi -> t_head(1) -> t_hat
//   epsilon : trainable scalar, broadcast to every row
//   concat  = [y0, y1, t_hat, epsilon, phi]        (axis = 1)
//
// `t_head` is linear (no sigmoid), matching the Keras source. That output is
// therefore a logit, not a probability; the losses below apply their own
// smoothing to it, exactly as the reference does.
//
// The L2 penalty from `kernel_regularizer=l2(λ)` is added to the GRADIENTS
// (λ·W), which is how Keras folds a regularizer into the loss before
// backprop. It is deliberately NOT delegated to AdamW's decoupled
// `weight_decay`, which would follow a different trajectory. Only the y0/y1
// heads are regularized, matching the Keras source.

///|
/// ELU for the hidden layers (alpha = 1, the Keras default).
const DN_ALPHA : Float = 1.0F

///|
/// Keras' `binary_crossentropy` clips its argument to [1e-7, 1-1e-7] before
/// taking logs. Reproduced so a saturated logit cannot produce an infinity
/// that silently poisons every downstream gradient.
const DN_BCE_CLIP : Float = 1.0e-7F

///|
/// Propensity smoothing used by the vanilla classification loss:
/// `p = (t_hat + BCE_SMOOTH) / BCE_SCALE`, with the constants taken from the
/// reference's `binary_classification_loss`.
const DN_BCE_SMOOTH : Float = 0.001F
const DN_BCE_SCALE : Float = 1.002F

///|
/// Propensity smoothing used by the targeted-regularisation term:
/// `p_tilde = (t_hat + TARREG_SMOOTH) / TARREG_SCALE`. NOTE these are NOT the
/// same constants as the vanilla term -- the reference smooths with 0.01/1.02
/// here and 0.001/1.002 above. Reusing one set for both is a plausible-looking
/// bug that no type checker catches.
const DN_TARREG_SMOOTH : Float = 0.01F
const DN_TARREG_SCALE : Float = 1.02F

///|
/// The DragonNet parameter set: ten dense layers plus the targeting scalar.
pub(all) struct DragonNet {
  enc1 : LinearParam
  enc2 : LinearParam
  enc3 : LinearParam
  y0_h1 : LinearParam
  y0_h2 : LinearParam
  y0_out : LinearParam
  y1_h1 : LinearParam
  y1_h2 : LinearParam
  y1_out : LinearParam
  t_head : LinearParam
  epsilon : Float
  t_sigmoid : Bool
}

///|
/// Flat N(0, std) weights + zero bias. `Xoshiro` mutates through value
/// passing (`next_u64` writes `r.s0`..`r.s3`), so looping here advances the
/// stream rather than redrawing the same number.
fn dn_linear(rng : Xoshiro, in_f : Int, out_f : Int, std : Float) -> LinearParam {
  let w : Array[Float] = Array::make(out_f * in_f, 0.0F)
  for i in 0..<(out_f * in_f) {
    let (z1, z2) = box_muller(rng)
    let z = if z1 != 0.0 { z1 } else { z2 }
    w[i] = Float::from_double(z) * std
  }
  let b : Array[Float] = Array::make(out_f, 0.0F)
  LinearParam::new(w, b, in_f, out_f)
}

///|
/// Build a DragonNet. `width` is the shared-representation width (200 in the
/// paper) and `half` the outcome-head width (100).
///
/// `std` defaults to 0.05, which is the reference's `RandomNormal` stddev for
/// the representation layers; the paper's heads use Keras' default
/// `glorot_uniform`, which a single normal std cannot reproduce exactly. The
/// choice affects initialisation only, never the gradient algebra.
pub fn DragonNet::new(
  input_dim : Int,
  width~ : Int = 200,
  half~ : Int = 100,
  seed~ : UInt64 = 42UL,
  std~ : Float = 0.05F,
  t_sigmoid~ : Bool = true,
) -> DragonNet {
  let rng = Xoshiro::new(seed)
  let eps : Float = Float::from_double({
    let (z1, z2) = box_muller(rng)
    if z1 != 0.0 { z1 } else { z2 }
  }) * std
  {
    enc1: dn_linear(rng, input_dim, width, std),
    enc2: dn_linear(rng, width, width, std),
    enc3: dn_linear(rng, width, width, std),
    y0_h1: dn_linear(rng, width, half, std),
    y0_h2: dn_linear(rng, half, half, std),
    y0_out: dn_linear(rng, half, 1, std),
    y1_h1: dn_linear(rng, width, half, std),
    y1_h2: dn_linear(rng, half, half, std),
    y1_out: dn_linear(rng, half, 1, std),
    t_head: dn_linear(rng, width, 1, std),
    epsilon: eps,
    t_sigmoid: t_sigmoid,
  }
}

///|
/// Forward output plus every pre-activation the backward pass needs.
///
/// The pre-activations are stored rather than the ELU outputs because
/// `elu_backward` needs the pre-activation, and recomputing it in the
/// backward would be one more place for the two directions to disagree.
pub(all) struct DragonNetOut {
  y0 : Array[Float]
  y1 : Array[Float]
  t_hat : Array[Float]
  epsilon : Float
  phi : Array[Float]
  // pre-activations, [n, width] / [n, half]
  z1 : Array[Float]
  z2 : Array[Float]
  z3 : Array[Float]
  a0 : Array[Float]
  b0 : Array[Float]
  c1 : Array[Float]
  d1 : Array[Float]
}

///|
/// Element-wise logistic, the propensity head's activation.
fn dn_sigmoid_all(x : Array[Float], n : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = 1.0F / (1.0F + expf(-x[i]))
  }
  out
}

///|
/// Forward pass. `x` is `[n, input_dim]` row-major flat.
pub fn dragonnet_forward(
  net : DragonNet,
  x : Array[Float],
  n : Int,
) -> DragonNetOut {
  let z1 = linear_forward(x, n, net.enc1)
  let z2 = linear_forward(elu_forward(z1, alpha=DN_ALPHA), n, net.enc2)
  let z3 = linear_forward(elu_forward(z2, alpha=DN_ALPHA), n, net.enc3)
  let phi = elu_forward(z3, alpha=DN_ALPHA)
  let a0 = linear_forward(phi, n, net.y0_h1)
  let b0 = linear_forward(elu_forward(a0, alpha=DN_ALPHA), n, net.y0_h2)
  let y0 = linear_forward(elu_forward(b0, alpha=DN_ALPHA), n, net.y0_out)
  let c1 = linear_forward(phi, n, net.y1_h1)
  let d1 = linear_forward(elu_forward(c1, alpha=DN_ALPHA), n, net.y1_h2)
  let y1 = linear_forward(elu_forward(d1, alpha=DN_ALPHA), n, net.y1_out)
  let t_raw = linear_forward(phi, n, net.t_head)
  // The paper's `make_dragonnet` puts a SIGMOID on the propensity head. A
  // variant seen in the wild drops it and leaves the head linear; with the
  // reference's smoothing `p = (t_hat + 0.001)/1.002` that produces negative
  // "probabilities", the BCE clip saturates, and the propensity term becomes
  // a constant -- the head then trains on nothing at all. `t_sigmoid`
  // defaults to the faithful setting; the linear variant is still supported
  // and its gradient stays correct (clipped, hence zero) either way.
  let t_hat = if net.t_sigmoid { dn_sigmoid_all(t_raw, n) } else { t_raw }
  // The Keras concat is [y0, y1, t_hat, epsilon, phi]; the four heads are
  // [n, 1] each and epsilon is broadcast to every row, which is what
  // `epsilon * tf.ones_like(inputs)[:, 0:1]` does.
  {
    y0,
    y1,
    t_hat,
    epsilon: net.epsilon,
    phi,
    z1,
    z2,
    z3,
    a0,
    b0,
    c1,
    d1,
  }
}

///|
/// The Keras concat, for callers that want the model's actual output tensor.
/// Row layout `[y0, y1, t_hat, epsilon, phi[0..width)]` -- FOUR scalars then
/// the representation, so each row is `4 + width` long. Counting three (easy
/// to do, since only three of them are tensors) overruns the buffer by one
/// element per row.
pub fn dragonnet_concat(
  out : DragonNetOut,
  n : Int,
  width : Int,
) -> Array[Float] {
  let cols = 4 + width
  let res : Array[Float] = Array::make(n * cols, 0.0F)
  for b in 0..<n {
    let o = b * cols
    res[o] = out.y0[b]
    res[o + 1] = out.y1[b]
    res[o + 2] = out.t_hat[b]
    res[o + 3] = out.epsilon
    let base = b * width
    for k in 0..<width {
      res[o + 4 + k] = out.phi[base + k]
    }
  }
  res
}

///|
/// Propensity and its derivative with respect to `t_hat`, honouring the clip.
///
/// Returns `(p, dp_dt_hat)`. The derivative is **zero whenever the clip is
/// active**: Keras' `binary_crossentropy` clips its argument to
/// [1e-7, 1-1e-7], so outside that band the loss is a constant and its
/// gradient is zero. Computing `-t/p + (1-t)/(1-p)` from the CLAMPED `p`
/// while pretending the clamp is not there produces gradients of order 1e7
/// for a term the loss does not actually depend on -- which is exactly what a
/// finite-difference check is for.
///
/// This function is the single place the clipping decision is made, so the
/// loss and its gradient cannot disagree about whether a given propensity was
/// clipped.
fn dn_prop(
  t_hat : Float,
  smooth : Float,
  scale : Float,
) -> (Float, Float) {
  let raw = (t_hat + smooth) / scale
  let lo = DN_BCE_CLIP
  let hi = 1.0F - DN_BCE_CLIP
  if raw < lo {
    (lo, 0.0F)
  } else if raw > hi {
    (hi, 0.0F)
  } else {
    (raw, 1.0F / scale)
  }
}

///|
/// Clip a propensity to the range Keras' binary_crossentropy assumes.
fn dn_clip_p(p : Float) -> Float {
  let lo = DN_BCE_CLIP
  let hi = 1.0F - DN_BCE_CLIP
  if p < lo {
    lo
  } else if p > hi {
    hi
  } else {
    p
  }
}

///|
/// Total training objective, transcribed from the reference's
/// `make_tarreg_loss(...)[tarreg_ATE_unbounded_domain_loss]`:
///
///   L_reg    = SUM_i (1-t_i)*(y_i - y0_i)^2  +  SUM_i t_i*(y_i - y1_i)^2
///   L_bce    = SUM_i -[ t_i*log(p_i) + (1-t_i)*log(1-p_i) ]
///              with p = (t_hat + 0.001)/1.002
///   y_pred_i = t_i*y1_i + (1-t_i)*y0_i
///   h_i      = t_i/p~_i - (1-t_i)/(1-p~_i)
///              with p~ = (t_hat + 0.01)/1.02
///   L_tarreg = SUM_i (y_i - (y_pred_i + epsilon*h_i))^2
///   L        = L_reg + L_bce + ratio * L_tarreg
///
/// `ratio` is the reference's `ratio`; 0.0 reduces this to the vanilla
/// DragonNet loss, in which `epsilon` has no effect at all -- it appears
/// nowhere in `L_reg + L_bce`.
pub fn dragonnet_loss(
  out : DragonNetOut,
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
) -> Float {
  let mut total = 0.0F
  for i in 0..<n {
    let ti = t[i]
    let yi = y[i]
    let e0 = yi - out.y0[i]
    let e1 = yi - out.y1[i]
    total = total + (1.0F - ti) * e0 * e0 + ti * e1 * e1
    // vanilla classification term; `dn_prop` decides the clipping, and the
    // loss uses the same value the gradient differentiates
    let (p, _dp) = dn_prop(out.t_hat[i], DN_BCE_SMOOTH, DN_BCE_SCALE)
    total = total - (ti * logf(p) + (1.0F - ti) * logf(1.0F - p))
    // targeted regularisation term
    let (pt, _dpt) = dn_prop(out.t_hat[i], DN_TARREG_SMOOTH, DN_TARREG_SCALE)
    let h = ti / pt - (1.0F - ti) / (1.0F - pt)
    let y_pred = ti * out.y1[i] + (1.0F - ti) * out.y0[i]
    let q = yi - (y_pred + out.epsilon * h)
    total = total + ratio * q * q
  }
  total
}

///|
/// Analytic gradients of `dragonnet_loss` with respect to
/// `(y0, y1, t_hat, epsilon)`. Returns `(d_y0, d_y1, d_t_hat, d_epsilon)`.
pub fn dragonnet_loss_grads(
  out : DragonNetOut,
  y : Array[Float],
  t : Array[Float],
  n : Int,
  ratio : Float,
) -> (Array[Float], Array[Float], Array[Float], Float) {
  let d_y0 : Array[Float] = Array::make(n, 0.0F)
  let d_y1 : Array[Float] = Array::make(n, 0.0F)
  let d_t : Array[Float] = Array::make(n, 0.0F)
  let mut d_eps = 0.0F
  for i in 0..<n {
    let ti = t[i]
    let yi = y[i]
    let a = out.y1[i]
    let b = out.y0[i]
    // d/dy1 of  (1-t)(y-y0)^2 + t(y-y1)^2
    //        = -2t(y-y1) = 2t(y1-y)
    let gy1 = 2.0F * ti * (a - yi)
    // d/dy0 of  (1-t)(y-y0)^2 + t(y-y1)^2
    //        = -2(1-t)(y-y0) = 2(1-t)(y0-y)
    let gy0 = 2.0F * (1.0F - ti) * (b - yi)
    // vanilla BCE: dp/dt_hat from dn_prop is ZERO when the propensity was
    // clipped, which is what makes the gradient agree with the loss when the
    // propensity head's output falls outside [0,1].
    let (p, dp) = dn_prop(out.t_hat[i], DN_BCE_SMOOTH, DN_BCE_SCALE)
    let gt = dp * (-ti / p + (1.0F - ti) / (1.0F - p))
    // targeted regularisation. q = y - (y_pred + eps*h), so
    //   dL/dy_pred = -2q, dL/deps = -2q*h,
    //   dh/dp = -t/p^2 - (1-t)/(1-p)^2, and dp/dt_hat likewise zero if clipped
    let (pt, dpt) = dn_prop(out.t_hat[i], DN_TARREG_SMOOTH, DN_TARREG_SCALE)
    let h = ti / pt - (1.0F - ti) / (1.0F - pt)
    let y_pred = ti * a + (1.0F - ti) * b
    let q = yi - (y_pred + out.epsilon * h)
    let dh = -ti / (pt * pt) - (1.0F - ti) / ((1.0F - pt) * (1.0F - pt))
    let k = -2.0F * q * out.epsilon * dh * dpt
    // y_pred is a convex blend of y1 and y0 with weights t and 1-t.
    d_y1[i] = gy1 + ratio * (-2.0F * q * ti)
    d_y0[i] = gy0 + ratio * (-2.0F * q * (1.0F - ti))
    d_t[i] = gt + ratio * k
    d_eps = d_eps + ratio * (-2.0F * q * h)
  }
  (d_y0, d_y1, d_t, d_eps)
}

///|
/// Gradients for one dense layer: `(d_weight, d_bias)`.
pub type DnLayerGrad = (Array[Float], Array[Float])

///|
/// The full parameter gradient of a DragonNet, plus `epsilon`.
pub(all) struct DragonNetGrad {
  enc1 : DnLayerGrad
  enc2 : DnLayerGrad
  enc3 : DnLayerGrad
  y0_h1 : DnLayerGrad
  y0_h2 : DnLayerGrad
  y0_out : DnLayerGrad
  y1_h1 : DnLayerGrad
  y1_h2 : DnLayerGrad
  y1_out : DnLayerGrad
  t_head : DnLayerGrad
  epsilon : Float
}

///|
/// Add the Keras L2 penalty's gradient λ·W to `d_weight` in place. This is
/// the derivative of `λ/2 * sum(W^2)`; Keras adds the regularised loss to the
/// objective before backprop, so the data gradient and this term simply add.
/// The bias gradient passes through untouched -- Keras' `l2` regularizer is
/// declared on the kernel, not the bias, so dropping `d_bias` here would
/// silently train a network whose biases never move.
fn dn_l2(
  d_weight : Array[Float],
  d_bias : Array[Float],
  weight : Array[Float],
  lambda : Float,
) -> DnLayerGrad {
  let n = weight.length()
  for i in 0..<n {
    d_weight[i] = d_weight[i] + lambda * weight[i]
  }
  (d_weight, d_bias)
}

///|
/// Backward pass from `(d_y0, d_y1, d_t_hat, d_epsilon)` down to every layer.
///
/// `d_epsilon` is taken as a parameter rather than recomputed here because it
/// does NOT flow through the network: epsilon is a bare scalar that never
/// touches a matmul, so the only path into it is the loss. A version that
/// silently wrote `epsilon: 0.0F` here would type-check, compile, and train
/// the targeting parameter not at all.
///
/// `l2_lambda` is applied to the six outcome-head layers only, matching the
/// Keras source; pass 0.0 to disable. The gradient returned is the SUM over
/// the batch, so a caller dividing by `n` gets a mean.
pub fn dragonnet_backward(
  net : DragonNet,
  x : Array[Float],
  out : DragonNetOut,
  d_y0 : Array[Float],
  d_y1 : Array[Float],
  d_t : Array[Float],
  d_epsilon : Float,
  n : Int,
  l2_lambda : Float,
) -> DragonNetGrad {
  let lam = l2_lambda
  // ---- y0 head (linear output, so no mask before y0_out) ----
  let (d_b0, dw_y0_out, db_y0_out) = linear_backward(
    elu_forward(out.b0, alpha=DN_ALPHA), d_y0, n, net.y0_out,
  )
  let d_b0 = elu_backward(out.b0, d_b0, alpha=DN_ALPHA)
  let (d_a0, dw_y0_h2, db_y0_h2) = linear_backward(
    elu_forward(out.a0, alpha=DN_ALPHA), d_b0, n, net.y0_h2,
  )
  let d_a0 = elu_backward(out.a0, d_a0, alpha=DN_ALPHA)
  let (d_phi0, dw_y0_h1, db_y0_h1) = linear_backward(
    out.phi, d_a0, n, net.y0_h1,
  )
  // ---- y1 head ----
  let (d_d1, dw_y1_out, db_y1_out) = linear_backward(
    elu_forward(out.d1, alpha=DN_ALPHA), d_y1, n, net.y1_out,
  )
  let d_d1 = elu_backward(out.d1, d_d1, alpha=DN_ALPHA)
  let (d_c1, dw_y1_h2, db_y1_h2) = linear_backward(
    elu_forward(out.c1, alpha=DN_ALPHA), d_d1, n, net.y1_h2,
  )
  let d_c1 = elu_backward(out.c1, d_c1, alpha=DN_ALPHA)
  let (d_phi1, dw_y1_h1, db_y1_h1) = linear_backward(
    out.phi, d_c1, n, net.y1_h1,
  )
  // ---- propensity head ----
  // `d_t` is the gradient w.r.t. the head's OUTPUT. If that output went
  // through a sigmoid, the head's pre-activation gradient is scaled by
  // p(1-p) with p = out.t_hat (already the post-activation value).
  let d_t_raw : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let p = out.t_hat[i]
    let f = if net.t_sigmoid { p * (1.0F - p) } else { 1.0F }
    d_t_raw[i] = d_t[i] * f
  }
  let (d_phet, dw_t, db_t) = linear_backward(
    out.phi, d_t_raw, n, net.t_head,
  )
  // ---- the shared representation receives all three heads ----
  // Accumulated in place: `linear_backward` returns a fresh buffer, so
  // folding the other two heads into it needs no `mut` and no third buffer.
  let width = net.enc3.out_features
  for i in 0..<(n * width) {
    d_phi0[i] = d_phi0[i] + d_phi1[i] + d_phet[i]
  }
  let d_phi = d_phi0
  let (d_e2, dw_enc3, db_enc3) = linear_backward(
    elu_forward(out.z2, alpha=DN_ALPHA), d_phi, n, net.enc3,
  )
  let d_e2 = elu_backward(out.z2, d_e2, alpha=DN_ALPHA)
  let (d_e1, dw_enc2, db_enc2) = linear_backward(
    elu_forward(out.z1, alpha=DN_ALPHA), d_e2, n, net.enc2,
  )
  let d_e1 = elu_backward(out.z1, d_e1, alpha=DN_ALPHA)
  let (_d_x, dw_enc1, db_enc1) = linear_backward(x, d_e1, n, net.enc1)
  {
    enc1: (dw_enc1, db_enc1),
    enc2: (dw_enc2, db_enc2),
    enc3: (dw_enc3, db_enc3),
    y0_h1: dn_l2(dw_y0_h1, db_y0_h1, net.y0_h1.weight, lam),
    y0_h2: dn_l2(dw_y0_h2, db_y0_h2, net.y0_h2.weight, lam),
    y0_out: dn_l2(dw_y0_out, db_y0_out, net.y0_out.weight, lam),
    y1_h1: dn_l2(dw_y1_h1, db_y1_h1, net.y1_h1.weight, lam),
    y1_h2: dn_l2(dw_y1_h2, db_y1_h2, net.y1_h2.weight, lam),
    y1_out: dn_l2(dw_y1_out, db_y1_out, net.y1_out.weight, lam),
    t_head: (dw_t, db_t),
    epsilon: d_epsilon,
  }
}

///|
/// The ten dense layers in the canonical order used by
/// `dragonnet_flatten` / `dragonnet_unflatten` / `dragonnet_grad_flatten`.
/// Changing this order silently invalidates a saved parameter vector, so it
/// lives in exactly one place.
fn dn_layers(net : DragonNet) -> Array[LinearParam] {
  [
    net.enc1,
    net.enc2,
    net.enc3,
    net.y0_h1,
    net.y0_h2,
    net.y0_out,
    net.y1_h1,
    net.y1_h2,
    net.y1_out,
    net.t_head,
  ]
}

///|
/// Start index of layer `layer`'s block inside the flat parameter vector
/// (bias first, then weight). Layer 10 is epsilon, which starts at
/// `dragonnet_param_count`. Exposed so a caller can address one layer's
/// coordinates without replicating the layout.
pub fn dragonnet_layer_offset(net : DragonNet, layer : Int) -> Int {
  let layers = dn_layers(net)
  let mut k = 0
  let mut i = 0
  while i < layer && i < layers.length() {
    k = k + layers[i].bias.length() + layers[i].weight.length()
    i = i + 1
  }
  k
}

///|
/// Number of scalar parameters, epsilon included: every layer's bias then its
/// weight, in `dn_layers` order, with `epsilon` last.
pub fn dragonnet_param_count(net : DragonNet) -> Int {
  let mut total = 1
  for l in dn_layers(net) {
    total = total + l.bias.length() + l.weight.length()
  }
  total
}

///|
/// Pack every parameter into one flat vector, in `dragonnet_param_count`
/// order. Reading and writing the network as a single array is what makes an
/// exhaustive finite-difference gradient check possible without the caller
/// having to know each layer's shape.
pub fn dragonnet_flatten(net : DragonNet) -> Array[Float] {
  let flat : Array[Float] = Array::make(dragonnet_param_count(net), 0.0F)
  let mut k = 0
  for l in dn_layers(net) {
    let b = l.bias
    for i in 0..<b.length() {
      flat[k] = b[i]
      k = k + 1
    }
    let w = l.weight
    for i in 0..<w.length() {
      flat[k] = w[i]
      k = k + 1
    }
  }
  flat[k] = net.epsilon
  flat
}

///|
/// Inverse of `dragonnet_flatten`. Returns a NEW `DragonNet`; the input is
/// left alone. `flat` must be exactly `dragonnet_param_count(net)` long.
pub fn dragonnet_unflatten(
  net : DragonNet,
  flat : Array[Float],
) -> DragonNet {
  let want = dragonnet_param_count(net)
  if flat.length() != want {
    abort(
      "dragonnet_unflatten: expected \{want} parameters, got \{flat.length()}",
    )
  }
  let layers = dn_layers(net)
  let rebuilt = []
  let mut k = 0
  for l in layers {
    let nb = l.bias.length()
    let nw = l.weight.length()
    let b2 : Array[Float] = Array::make(nb, 0.0F)
    for i in 0..<nb {
      b2[i] = flat[k]
      k = k + 1
    }
    let w2 : Array[Float] = Array::make(nw, 0.0F)
    for i in 0..<nw {
      w2[i] = flat[k]
      k = k + 1
    }
    rebuilt.push(LinearParam::new(w2, b2, l.in_features, l.out_features))
  }
  {
    enc1: rebuilt[0],
    enc2: rebuilt[1],
    enc3: rebuilt[2],
    y0_h1: rebuilt[3],
    y0_h2: rebuilt[4],
    y0_out: rebuilt[5],
    y1_h1: rebuilt[6],
    y1_h2: rebuilt[7],
    y1_out: rebuilt[8],
    t_head: rebuilt[9],
    epsilon: flat[k],
    t_sigmoid: net.t_sigmoid,
  }
}

///|
/// Pack a gradient in the same order as `dragonnet_flatten`.
pub fn dragonnet_grad_flatten(g : DragonNetGrad) -> Array[Float] {
  let grads = dn_layers_of_grad(g)
  let mut total = 1
  for lg in grads {
    total = total + lg.1.length() + lg.0.length()
  }
  let flat : Array[Float] = Array::make(total, 0.0F)
  let mut k = 0
  for lg in grads {
    let b = lg.1
    for i in 0..<b.length() {
      flat[k] = b[i]
      k = k + 1
    }
    let w = lg.0
    for i in 0..<w.length() {
      flat[k] = w[i]
      k = k + 1
    }
  }
  flat[k] = g.epsilon
  flat
}

///|
/// The gradient's ten layers in the same order as `dn_layers`.
fn dn_layers_of_grad(g : DragonNetGrad) -> Array[DnLayerGrad] {
  [
    g.enc1,
    g.enc2,
    g.enc3,
    g.y0_h1,
    g.y0_h2,
    g.y0_out,
    g.y1_h1,
    g.y1_h2,
    g.y1_out,
    g.t_head,
  ]
}

///|
/// The L2 penalty that Keras' `kernel_regularizer=l2(λ)` contributes, on the
/// six outcome heads only -- exactly the layers the reference decorates.
///
/// Exposed separately (rather than folded into `dragonnet_loss`) because it
/// depends on the WEIGHTS, which the forward output does not carry, and
/// because `dragonnet_backward` differentiates it on the gradient side. A
/// caller reconstructing the full objective must add these two, and keeping
/// them as separate published functions is what stops that composition from
/// being written twice and drifting.
pub fn dragonnet_l2_penalty(net : DragonNet, l2_lambda : Float) -> Float {
  let heads = [
    net.y0_h1,
    net.y0_h2,
    net.y0_out,
    net.y1_h1,
    net.y1_h2,
    net.y1_out,
  ]
  let mut acc = 0.0F
  for l in heads {
    let w = l.weight
    for i in 0..<w.length() {
      acc = acc + w[i] * w[i]
    }
  }
  0.5F * l2_lambda * acc
}

///|
/// Adam moments for the ten layers plus the targeting scalar. The scalar is
/// carried as a one-element `AdamState` so it goes through exactly the same
/// update rule as the kernels rather than a second, subtly different one.
pub(all) struct DragonNetAdam {
  layers : Array[AdamState]
  epsilon : AdamState
}

///|
/// Allocate zeroed Adam state sized to `net`.
pub fn DragonNetAdam::new(net : DragonNet) -> DragonNetAdam {
  {
    layers: [
      adam_init(net.enc1.weight.length(), net.enc1.bias.length()),
      adam_init(net.enc2.weight.length(), net.enc2.bias.length()),
      adam_init(net.enc3.weight.length(), net.enc3.bias.length()),
      adam_init(net.y0_h1.weight.length(), net.y0_h1.bias.length()),
      adam_init(net.y0_h2.weight.length(), net.y0_h2.bias.length()),
      adam_init(net.y0_out.weight.length(), net.y0_out.bias.length()),
      adam_init(net.y1_h1.weight.length(), net.y1_h1.bias.length()),
      adam_init(net.y1_h2.weight.length(), net.y1_h2.bias.length()),
      adam_init(net.y1_out.weight.length(), net.y1_out.bias.length()),
      adam_init(net.t_head.weight.length(), net.t_head.bias.length()),
    ],
    epsilon: adam_init(1, 0),
  }
}

///|
/// One full-batch Adam step. Returns a FRESH `DragonNet`; neither the input
/// net nor the optimizer state is mutated in place beyond the state arrays
/// Adam itself rebuilds.
pub fn dragonnet_adam_step(
  net : DragonNet,
  g : DragonNetGrad,
  opt : DragonNetAdam,
  lr : Float,
  beta1 : Float,
  beta2 : Float,
  eps : Float,
) -> (DragonNet, DragonNetAdam) {
  let states = opt.layers
  let (n1, s1) = adam_update_linear(net.enc1, g.enc1.0, g.enc1.1, states[0], lr, beta1, beta2, eps)
  states[0] = s1
  let (n2, s2) = adam_update_linear(net.enc2, g.enc2.0, g.enc2.1, states[1], lr, beta1, beta2, eps)
  states[1] = s2
  let (n3, s3) = adam_update_linear(net.enc3, g.enc3.0, g.enc3.1, states[2], lr, beta1, beta2, eps)
  states[2] = s3
  let (h1, s4) = adam_update_linear(net.y0_h1, g.y0_h1.0, g.y0_h1.1, states[3], lr, beta1, beta2, eps)
  states[3] = s4
  let (h2, s5) = adam_update_linear(net.y0_h2, g.y0_h2.0, g.y0_h2.1, states[4], lr, beta1, beta2, eps)
  states[4] = s5
  let (o0, s6) = adam_update_linear(net.y0_out, g.y0_out.0, g.y0_out.1, states[5], lr, beta1, beta2, eps)
  states[5] = s6
  let (i1, s7) = adam_update_linear(net.y1_h1, g.y1_h1.0, g.y1_h1.1, states[6], lr, beta1, beta2, eps)
  states[6] = s7
  let (i2, s8) = adam_update_linear(net.y1_h2, g.y1_h2.0, g.y1_h2.1, states[7], lr, beta1, beta2, eps)
  states[7] = s8
  let (o1, s9) = adam_update_linear(net.y1_out, g.y1_out.0, g.y1_out.1, states[8], lr, beta1, beta2, eps)
  states[8] = s9
  let (th, s10) = adam_update_linear(net.t_head, g.t_head.0, g.t_head.1, states[9], lr, beta1, beta2, eps)
  states[9] = s10
  // epsilon: same rule, carried as weight=[eps], bias=[] (no bias gradient).
  // `adam_update_arrays` returns a 3-tuple; the middle element is that empty
  // bias array, which has no gradient because DragonNet's epsilon is a bare
  // scalar with no bias of its own.
  let (new_eps_arr, _, s11) = adam_update_arrays(
    [net.epsilon], [], [g.epsilon], [], opt.epsilon, lr, beta1, beta2, eps,
  )
  let next : DragonNet = {
    enc1: n1,
    enc2: n2,
    enc3: n3,
    y0_h1: h1,
    y0_h2: h2,
    y0_out: o0,
    y1_h1: i1,
    y1_h2: i2,
    y1_out: o1,
    t_head: th,
    epsilon: new_eps_arr[0],
    t_sigmoid: net.t_sigmoid,
  }
  (next, { layers: states, epsilon: s11 })
}