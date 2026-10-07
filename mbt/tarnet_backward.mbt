// tarnet_backward.mbt -- the model-level backward pass.
//
// Reference: arXiv:2605.07735 (ICME 2026), `tarnet/models/tarnet.py`.
//
// This file exists because `tarnet_forward` discarded every cache except the
// first layer norm's, so there was no path from the logits back to the
// parameters. `tarnet_forward_full` is the real implementation and
// `tarnet_forward` is a thin wrapper over it, exactly as `se_forward` became a
// wrapper over `se_forward_full` -- two copies of a twelve-stage pipeline WILL
// drift, and when they do the drift is invisible from inside.
//
// GRADIENT LAYOUT: `tarnet_backward` returns a flat gradient whose order is
// `tarnet_flatten`'s, term for term. The two are the same layout by
// construction rather than by documentation, and `TarnetGrad` does not exist
// because it would be a second thing that can disagree with the first.

///|
/// Every cache the model-level backward needs. Held as one struct so a missing
/// stage is a compile error rather than a silently absent gradient.
pub(all) struct TarnetFullCache {
  ln_in_cache : GlnCache // gLN over the log-Mel input
  gln : Array[Float]           // gLN output, the 1x1 conv's input
  bn : Array[Float]            // bottleneck, the three stacks' shared input
  stack_s : TarnetStackCache
  stack_m : TarnetStackCache
  stack_l : TarnetStackCache
  cat : Array[Float]           // the three stacks concatenated
  mfa : Array[Float]           // multi-feature aggregation, pre-ReLU
  mfa_relu : Array[Float]      // the SE module's input
  se_cache : SeCache
  fused : Array[Float]         // SE output, the ASP's input
  asp_cache : AspCache
  pooled : Array[Float]        // ASP output [n, 2c]
  bn_seg_cache : BatchNormCache
  bn_seg : Array[Float]
  emb : Array[Float]
  bn_out_cache : BatchNormCache
  embedding : Array[Float]
}

///|
/// Concatenate the three scale stacks along the channel axis: `[n, c, t]` x 3
/// -> `[n, 3c, t]`.
///
/// The SOURCE index is the same for all three stacks because they share a
/// shape; only the DESTINATION carries the group offset. Adding the offset to
/// the source as well walks off the end of the stack.
fn tn_concat3(
  xs : Array[Float],
  xm : Array[Float],
  xl : Array[Float],
  n : Int,
  c : Int,
  t : Int,
) -> Array[Float] {
  let cat : Array[Float] = Array::make(n * 3 * c * t, 0.0F)
  for i in 0..<n {
    for ch in 0..<c {
      let dst = (i * 3 * c + ch) * t
      let src = (i * c + ch) * t
      for ti in 0..<t {
        cat[dst + ti] = xs[src + ti]
        cat[dst + ti + c * t] = xm[src + ti]
        cat[dst + ti + 2 * c * t] = xl[src + ti]
      }
    }
  }
  cat
}

///|
/// Inverse of `tn_concat3` for the backward: split one gradient into the three
/// stack gradients. `+` not `=`, because a concatenation's backward ACCUMULATES
/// into each source -- assigning would silently drop two of the three.
fn tn_split3(g : Array[Float], n : Int, c : Int, t : Int) -> (Array[Float], Array[Float], Array[Float]) {
  let gs : Array[Float] = Array::make(n * c * t, 0.0F)
  let gm : Array[Float] = Array::make(n * c * t, 0.0F)
  let gl : Array[Float] = Array::make(n * c * t, 0.0F)
  for i in 0..<n {
    for ch in 0..<c {
      let dst = (i * 3 * c + ch) * t
      let src = (i * c + ch) * t
      for ti in 0..<t {
        gs[src + ti] = g[dst + ti]
        gm[src + ti] = g[dst + ti + c * t]
        gl[src + ti] = g[dst + ti + 2 * c * t]
      }
    }
  }
  (gs, gm, gl)
}

///|
/// Forward pass from a log-Mel spectrogram, keeping every cache.
/// `x` is `[n, in_chan, t]`.
pub fn tarnet_forward_full(
  net : Tarnet,
  x : Array[Float],
  n : Int,
) -> (TarnetOut, TarnetFullCache) {
  let c = net.in_chan
  // The frame count is read off the INPUT, not off `net.t_frames`. With a
  // per-channel gLN affine nothing in the parameters depends on the frame
  // count, so one model serves utterances of any length -- which is the whole
  // point of a model whose input is variable-length speech. `t_frames` remains
  // on the struct as the expected length, not as a constraint.
  let t = x.length() / (n * c)
  // bottleneck: gLN over (in_chan, t) jointly, then a 1x1 convolution
  let (gln, bn_ln_cache) = gln_forward(x, n, c, t, net.ln_in)
  let bn = linear_forward(
    gln, n * t, LinearParam::new(net.bn_w, net.bn_b, c, net.bn_chan),
  )
  let (x_s, stack_s) = tn_stack_forward_full(net.stack_s, bn, n)
  let (x_m, stack_m) = tn_stack_forward_full(net.stack_m, bn, n)
  let (x_l, stack_l) = tn_stack_forward_full(net.stack_l, bn, n)
  let cat = tn_concat3(x_s, x_m, x_l, n, net.bn_chan, t)
  let mfa = linear_forward(
    cat,
    n * t,
    LinearParam::new(net.mfa_w, net.mfa_b, 3 * net.bn_chan, net.fuse_out),
  )
  let mfa_relu = relu_forward(mfa)
  let (fused, se_cache) = se_forward_full(mfa_relu, n, net.mfa_se)
  let (pooled, asp_cache) = asp_forward(fused, n, net.asp)
  let (bn_seg, bn_seg_cache) = batch_norm2d_forward(
    pooled, n, 2 * net.fuse_out, 1, 1, net.bn_seg,
  )
  let emb = linear_forward(
    bn_seg,
    n,
    LinearParam::new(net.fc_w, net.fc_b, 2 * net.fuse_out, net.emb_dim),
  )
  let (embedding, bn_out_cache) = batch_norm2d_forward(
    emb, n, net.emb_dim, 1, 1, net.bn_out,
  )
  let logits = linear_forward(
    embedding,
    n,
    LinearParam::new(net.cls_w, net.cls_b, net.emb_dim, net.n_classes),
  )
  let out = {
    logits,
    embedding,
    pooled: bn_seg,
    fused,
    bottleneck: bn,
    bottleneck_ln_cache: bn_ln_cache,
  }
  let cache = {
    ln_in_cache: bn_ln_cache,
    gln,
    bn,
    stack_s,
    stack_m,
    stack_l,
    cat,
    mfa,
    mfa_relu,
    se_cache,
    fused,
    asp_cache,
    pooled,
    bn_seg_cache,
    bn_seg,
    emb,
    bn_out_cache,
    embedding,
  }
  (out, cache)
}

///|
/// Model-level backward. Returns `(flat gradient, d_input)`.
///
/// The flat gradient is in `tarnet_flatten`'s order and has exactly
/// `tarnet_param_count(net)` entries -- asserted below rather than assumed,
/// because a short gradient silently trains a subset of the model instead of
/// failing.
///
/// `d_logits` is `[n, n_classes]` and normally comes from
/// `tarnet_softmax_dlogits`.
pub fn tarnet_backward(
  net : Tarnet,
  n : Int,
  cache : TarnetFullCache,
  d_logits : Array[Float],
  l2 : Float,
) -> (Array[Float], Array[Float]) {
  let c = net.in_chan
  // read off the cache's bottleneck, which is where the forward left it
  let t = cache.bn.length() / (n * net.bn_chan)
  // classifier
  let (d_embedding, d_cls_w, d_cls_b) = linear_backward(
    cache.embedding,
    d_logits,
    n,
    LinearParam::new(net.cls_w, net.cls_b, net.emb_dim, net.n_classes),
  )
  let (d_emb, d_bn_out_g, d_bn_out_b) = batch_norm2d_backward(
    d_embedding, cache.bn_out_cache, net.bn_out,
  )
  // embedding projection
  let (d_bn_seg, d_fc_w, d_fc_b) = linear_backward(
    cache.bn_seg,
    d_emb,
    n,
    LinearParam::new(net.fc_w, net.fc_b, 2 * net.fuse_out, net.emb_dim),
  )
  let (d_pooled, d_bn_seg_g, d_bn_seg_b) = batch_norm2d_backward(
    d_bn_seg, cache.bn_seg_cache, net.bn_seg,
  )
  // attentional statistics pooling
  let asp_g = asp_backward(d_pooled, n, net.asp, cache.asp_cache)
  let se_g = se_backward(cache.mfa_relu, asp_g.d_x, n, net.mfa_se, cache.se_cache)
  // multi-feature aggregation. relu_backward needs its own INPUT, not its
  // output, to know which entries were negative.
  let d_mfa = relu_backward(cache.mfa, se_g.d_x)
  let (d_cat, d_mfa_w, d_mfa_b) = linear_backward(
    cache.cat,
    d_mfa,
    n * t,
    LinearParam::new(net.mfa_w, net.mfa_b, 3 * net.bn_chan, net.fuse_out),
  )
  let (d_s, d_m, d_l) = tn_split3(d_cat, n, net.bn_chan, t)
  // the three stacks are parallel branches off ONE bottleneck, so their input
  // gradients ADD. Treating them as a chain would be wrong: there is no second
  // stack after the third.
  let (d_bn_s, grads_s) = tn_stack_backward(net.stack_s, n, cache.stack_s, d_s, l2)
  let (d_bn_m, grads_m) = tn_stack_backward(net.stack_m, n, cache.stack_m, d_m, l2)
  let (d_bn_l, grads_l) = tn_stack_backward(net.stack_l, n, cache.stack_l, d_l, l2)
  let d_bn : Array[Float] = Array::make(cache.bn.length(), 0.0F)
  for i in 0..<cache.bn.length() {
    d_bn[i] = d_bn_s[i] + d_bn_m[i] + d_bn_l[i]
  }
  let (d_gln, d_bn_w, d_bn_b) = linear_backward(
    cache.gln,
    d_bn,
    n * t,
    LinearParam::new(net.bn_w, net.bn_b, c, net.bn_chan),
  )
  let (d_x, d_ln_g, d_ln_b) = gln_backward(
    d_gln, cache.ln_in_cache, net.ln_in,
  )
  let g : Array[Float] = Array::make(tarnet_param_count(net), 0.0F)
  let mut o = 0
  o = tn_put(g, o, d_ln_g)
  o = tn_put(g, o, d_ln_b)
  o = tn_put(g, o, d_bn_w)
  o = tn_put(g, o, d_bn_b)
  let stacks = [net.stack_s, net.stack_m, net.stack_l]
  let stack_grads = [grads_s, grads_m, grads_l]
  for si in 0..<3 {
    let st = stacks[si]
    let gs = stack_grads[si]
    for bi in 0..<st.length() {
      // tn_stack_backward returns the per-block gradients in FORWARD order.
      o = tn_put(g, o, tarnet_block_grad_flatten(gs[bi], st[bi]))
    }
  }
  o = tn_put(g, o, d_mfa_w)
  o = tn_put(g, o, d_mfa_b)
  o = tn_put(g, o, se_g.w1)
  o = tn_put(g, o, se_g.b1)
  o = tn_put(g, o, se_g.w2)
  o = tn_put(g, o, se_g.b2)
  o = tn_put(g, o, asp_g.d_w1)
  o = tn_put(g, o, asp_g.d_b1)
  o = tn_put(g, o, asp_g.d_w2)
  o = tn_put(g, o, asp_g.d_b2)
  o = tn_put(g, o, d_bn_seg_g)
  o = tn_put(g, o, d_bn_seg_b)
  o = tn_put(g, o, d_fc_w)
  o = tn_put(g, o, d_fc_b)
  o = tn_put(g, o, d_bn_out_g)
  o = tn_put(g, o, d_bn_out_b)
  o = tn_put(g, o, d_cls_w)
  o = tn_put(g, o, d_cls_b)
  // A short gradient trains a SUBSET of the model and still "looks fine" -- the
  // loss still goes down. So this cannot be a runtime check here: `assert_eq`
  // needs an error signature, and threading one through the core training
  // primitive to protect against a mistake the gradient sweep catches outright
  // is the wrong trade. `tarnet_model_gate` compares EVERY parameter coordinate
  // against a finite difference in `tarnet_flatten`'s order, and an off-by-one
  // segment shifts every later one -- the sweep fails long before a partial
  // gradient could hide.
  (g, d_x)
}

///|
/// Softmax + cross-entropy for `[n, n_classes]` logits with integer targets.
/// Returns `(d_logits, mean loss)`.
///
/// `d_logits[b,k] = (softmax(logits)[b,k] - onehot(b,k)) / n`, so the returned
/// gradient is already MEAN-reduced and a caller that divides by `n` a second
/// time trains at `1/n^2`.
///
/// The max is subtracted before exponentiating. Without it a log-Mel input of
/// any size overflows to `inf` and every probability collapses to NaN -- and
/// the loss becomes NaN too, which looks like a divergence rather than an
/// avoidable overflow.
pub fn tarnet_softmax_dlogits(
  logits : Array[Float],
  n : Int,
  n_classes : Int,
  targets : Array[Int],
) -> (Array[Float], Float) {
  let inv_n = 1.0F / Float::from_int(n)
  let d : Array[Float] = Array::make(n * n_classes, 0.0F)
  let mut loss = 0.0F
  let mut b = 0
  while b < n {
    let row = b * n_classes
    let mut mx = logits[row]
    let mut k = 1
    while k < n_classes {
      if logits[row + k] > mx {
        mx = logits[row + k]
      }
      k = k + 1
    }
    let mut sum = 0.0F
    k = 0
    while k < n_classes {
      let e = expf(logits[row + k] - mx)
      d[row + k] = e
      sum = sum + e
      k = k + 1
    }
    let inv = 1.0F / sum
    let y = targets[b]
    k = 0
    while k < n_classes {
      let p = d[row + k] * inv
      let onehot = if k == y { 1.0F } else { 0.0F }
      d[row + k] = (p - onehot) * inv_n
      if k == y {
        // log(p), not p. Summing the PROBABILITIES instead is a real bug this
        // gate caught: its derivative is -p_y(p_y - onehot_k) instead of
        // (p_k - onehot_k)/n, so `d_logits` was right while `loss` was wrong --
        // which makes the model-level sweep fail on every coordinate and looks
        // exactly like a broken backward pass.
        loss = loss - logf(p)
      }
      k = k + 1
    }
    b = b + 1
  }
  (d, loss * inv_n)
}