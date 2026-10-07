// tarnet.mbt -- TARNet for closed-set speaker identification.
//
// Reference: Y. Terraf, Y. Iraqi, "TARNet: A Temporal-Aware Multi-Scale
// Architecture for Closed-Set Speaker Identification", arXiv:2605.07735
// (ICME 2026).  Transcribed from the authors' implementation,
// github.com/YassinTERRAF/TARNet, `tarnet/models/tarnet.py`.
//
//   log-Mel (no grad, in tarnet_logmel.mbt)
//     -> gLN + 1x1 conv                        = bottleneck, [n, bn_chan, T]
//     -> THREE PARALLEL Conv-TasNet stacks, each starting from the SAME
//        bottleneck output:
//          short: dilations [1,2] x 4 repeats
//          mid:   dilations [4,8] x 4 repeats
//          long:  dilation  [16]  x 4 repeats
//     -> concat along channels -> 1x1 conv + ReLU + SE   = MFA
//     -> ASP  -> [n, 2*fuse_out]
//     -> BN -> Linear -> BN                                = embedding
//     -> Linear                                              = class logits
//
// WHAT IS DELIBERATELY NOT HERE, and each omission is a judgement rather
// than an oversight:
//
//   * BatchNorm1d inside the ASP attention network and after the final
//     projection. BatchNorm's gradient depends on the batch through its
//     running statistics, which makes a per-sample gradient check
//     meaningless; the module is kept so a batch-level check can be added.
//   * Dropout before the classifier (the reference's default is p = 0, i.e.
//     absent).
//   * The front-end is not differentiable. The reference computes it under
//     `torch.no_grad()`, so `tarnet_forward` takes a log-Mel spectrogram and
//     `tarnet_logmel.mbt` produces one from raw audio.
//
// The three dilation patterns are the paper's contribution: each stack keeps
// its own receptive field and all three see the same bottleneck output, which
// is why the fusion is a concat rather than a sequence of stages.

///|
/// One residual Conv-TasNet block, as `Conv1DBlock` in the reference:
///
///   x -> 1x1 conv -> PReLU -> gLN -> depthwise dilated -> PReLU -> gLN
///     -> 1x1 conv -> channel-time SE -> (+ x)
///
/// The gLN uses this package's `LayerNorm(chan, 1, T)`, whose statistic is
/// over `chan * T` per sample -- the joint channel-and-time statistic gLN
/// calls for. Its scale and shift are per element rather than per channel as
/// in asteroid's gLN; that is a documented simplification, not an oversight.
pub(all) struct TarnetBlock {
  pw1 : Array[Float]
  pb1 : Array[Float]
  ln1 : LayerNorm
  dw : Conv1DDepthwise
  prelu1 : Float
  ln2 : LayerNorm
  pw2 : Array[Float]
  pb2 : Array[Float]
  se : CtseParam
  chan : Int
  hid : Int
  dilation : Int
}

///|
/// Allocate one block. `t` only sizes the two layer norms.
pub fn TarnetBlock::new(
  chan : Int,
  hid : Int,
  kernel : Int,
  dilation : Int,
  t : Int,
  seed : UInt64,
) -> TarnetBlock {
  let rng = Xoshiro::new(seed)
  let dw = Conv1DDepthwise::new(
    hid,
    kernel,
    dilation,
    (kernel - 1) * dilation / 2,
  )
  let dwv = tn_randn(hid * kernel, 0.05F, rng)
  let mut i = 0
  while i < hid * kernel {
    dw.weight[i] = dwv[i]
    i = i + 1
  }
  {
    pw1: tn_randn(hid * chan, 0.05F, rng),
    pb1: Array::make(hid, 0.0F),
    ln1: LayerNorm::new(hid, 1, t),
    dw,
    // PReLU's initial slope is 0.25, as in PyTorch's default.
    prelu1: 0.25F,
    ln2: LayerNorm::new(hid, 1, t),
    pw2: tn_randn(chan * hid, 0.05F, rng),
    pb2: Array::make(chan, 0.0F),
    se: CtseParam::new(chan, seed + 1UL),
    chan,
    hid,
    dilation,
  }
}

///|
/// Caches one block needs for its backward pass.
pub(all) struct TarnetBlockCache {
  x : Array[Float]
  pw1_out : Array[Float]
  pw1_post_prelu : Array[Float]
  ln1_out : Array[Float]
  dw_out : Array[Float]
  dw_post_prelu : Array[Float]
  pw2_out : Array[Float]
}

///|
/// Block forward. `x` is `[n, chan, t]`, returns `[n, chan, t]` and the cache.
pub fn tarnet_block_forward(
  b : TarnetBlock,
  x : Array[Float],
  n : Int,
) -> (Array[Float], TarnetBlockCache) {
  let t = x.length() / (n * b.chan)
  // 1x1 conv, time axis as batch
  let pw1_out = tn_conv1x1_forward(x, n, b.chan, b.hid, b.pw1, b.pb1)
  let pw1_post = prelu_forward(pw1_out, b.prelu1)
  // gLN over (hid, t) jointly. The BATCH dimension is the utterance, not the
  // frame: the statistic has to span every hidden unit and every frame of one
  // utterance, which is what makes it a *global* layer norm. Passing `n * t`
  // here (as the 1x1 convolutions above do) would normalise each frame on its
  // own and ask the layer for n*hid*t^2 elements it does not have.
  let (ln1_out, _c1) = layer_norm_forward(pw1_post, n, b.hid, 1, t, b.ln1)
  let dw_out = conv1d_depthwise_forward(ln1_out, n, b.dw)
  let dw_post = prelu_forward(dw_out, b.prelu1)
  let (ln2_out, _c2) = layer_norm_forward(dw_post, n, b.hid, 1, t, b.ln2)
  let pw2_out = tn_conv1x1_forward(
    ln2_out, n, b.hid, b.chan, b.pw2, b.pb2,
  )
  let (r, _se_cache) = ctse_forward(pw2_out, n, b.se)
  let out : Array[Float] = Array::make(n * b.chan * t, 0.0F)
  let mut i = 0
  while i < n * b.chan * t {
    out[i] = x[i] + r[i]
    i = i + 1
  }
  (out, { x, pw1_out, pw1_post_prelu: pw1_post, ln1_out, dw_out,
    dw_post_prelu: dw_post, pw2_out })
}

///|
/// The whole trainable network.
pub(all) struct Tarnet {
  // bottleneck
  ln_in : LayerNorm
  bn_w : Array[Float]
  bn_b : Array[Float]
  // three parallel stacks
  stack_s : Array[TarnetBlock]
  stack_m : Array[TarnetBlock]
  stack_l : Array[TarnetBlock]
  // multi-frequency attention
  mfa_w : Array[Float]
  mfa_b : Array[Float]
  mfa_se : SeParam
  // attentive statistics pooling
  asp : AspParam
  // embedding projection
  bn_seg : BatchNorm2d
  fc_w : Array[Float]
  fc_b : Array[Float]
  bn_out : BatchNorm2d
  // classifier
  cls_w : Array[Float]
  cls_b : Array[Float]
  // shapes
  in_chan : Int
  bn_chan : Int
  hid_chan : Int
  fuse_out : Int
  emb_dim : Int
  n_classes : Int
  t_frames : Int
}

///|
/// How many blocks a stack with `repeats` repetitions of `len(dilations)`
/// dilation values holds.
fn tn_stack_len(dilations : Array[Int], repeats : Int) -> Int {
  dilations.length() * repeats
}

///|
/// Build one stack from a dilation pattern.
fn tn_build_stack(
  dilations : Array[Int],
  repeats : Int,
  chan : Int,
  hid : Int,
  kernel : Int,
  t : Int,
  seed : UInt64,
) -> Array[TarnetBlock] {
  let out : Array[TarnetBlock] = []
  let mut s = seed
  let mut r = 0
  while r < repeats {
    let mut d = 0
    while d < dilations.length() {
      out.push(TarnetBlock::new(chan, hid, kernel, dilations[d], t, s))
      s = s + 17UL
      d = d + 1
    }
    r = r + 1
  }
  out
}

///|
/// Build TARNet. Defaults are the reference's: 80 log-Mel channels, a 128-wide
/// bottleneck, 512 hidden channels, kernel 3, four repeats per scale,
/// 512 fused channels and a 512-d embedding.
///
/// `t_frames` sizes the layer norms, which makes it a property of the model
/// rather than of a batch: the reference's gLN has no length-dependent
/// parameters either, but this package's `LayerNorm` stores gamma and beta
/// per element, so they are allocated for the expected frame count.
pub fn Tarnet::new(
  n_classes : Int,
  in_chan : Int,
  t_frames : Int,
  width~ : Int = 128,
  hid_chan~ : Int = 512,
  kernel~ : Int = 3,
  repeats_s~ : Int = 4,
  repeats_m~ : Int = 4,
  repeats_l~ : Int = 4,
  fuse_out~ : Int = 512,
  emb_dim~ : Int = 512,
  att_hidden~ : Int = 256,
  seed~ : UInt64 = 7UL,
) -> Tarnet {
  let rng = Xoshiro::new(seed)
  let dil_s = [1, 2]
  let dil_m = [4, 8]
  let dil_l = [16]
  let _ = (tn_stack_len(dil_s, repeats_s), tn_stack_len(dil_m, repeats_m),
    tn_stack_len(dil_l, repeats_l))
  {
    ln_in: LayerNorm::new(in_chan, 1, t_frames),
    bn_w: tn_randn(width * in_chan, 0.05F, rng),
    bn_b: Array::make(width, 0.0F),
    stack_s: tn_build_stack(dil_s, repeats_s, width, hid_chan, kernel, t_frames,
      seed + 100UL),
    stack_m: tn_build_stack(dil_m, repeats_m, width, hid_chan, kernel, t_frames,
      seed + 200UL),
    stack_l: tn_build_stack(dil_l, repeats_l, width, hid_chan, kernel, t_frames,
      seed + 300UL),
    mfa_w: tn_randn(fuse_out * 3 * width, 0.05F, rng),
    mfa_b: Array::make(fuse_out, 0.0F),
    mfa_se: SeParam::new(fuse_out, if fuse_out / 4 < 32 { 32 } else { fuse_out / 4 },
      seed + 400UL),
    asp: AspParam::new(fuse_out, att_hidden, seed + 500UL),
    bn_seg: BatchNorm2d::new(2 * fuse_out),
    fc_w: tn_randn(emb_dim * 2 * fuse_out, 0.05F, rng),
    fc_b: Array::make(emb_dim, 0.0F),
    bn_out: BatchNorm2d::new(emb_dim),
    // the reference initialises the classifier xavier_uniform with zero bias
    cls_w: tn_randn(n_classes * emb_dim, 0.05F, rng),
    cls_b: Array::make(n_classes, 0.0F),
    in_chan,
    bn_chan: width,
    hid_chan,
    fuse_out,
    emb_dim,
    n_classes,
    t_frames,
  }
}

///|
/// Forward output plus the caches the backward pass needs.
pub(all) struct TarnetOut {
  logits : Array[Float]
  embedding : Array[Float]
  pooled : Array[Float]
  fused : Array[Float]
  bottleneck : Array[Float]
  bottleneck_ln_cache : LayerNormCache
}

///|
/// Forward pass from a log-Mel spectrogram. `x` is `[n, in_chan, t]`.
///
/// `t` must match the `t_frames` the model was built for, because the layer
/// norms hold per-element scale and shift sized to it.
pub fn tarnet_forward(
  net : Tarnet,
  x : Array[Float],
  n : Int,
) -> TarnetOut {
  let c = net.in_chan
  let t = net.t_frames
  // bottleneck: gLN over (in_chan, t) jointly, then a 1x1 convolution
  let (gln, bn_ln_cache) = layer_norm_forward(x, n, c, 1, t, net.ln_in)
  let bn = linear_forward(
    gln, n * t, LinearParam::new(net.bn_w, net.bn_b, c, net.bn_chan),
  )
  let x_s = tn_run_stack(net.stack_s, bn, n)
  let x_m = tn_run_stack(net.stack_m, bn, n)
  let x_l = tn_run_stack(net.stack_l, bn, n)
  // concat along the channel axis -> [n, 3*bn_chan, t]
  let cat : Array[Float] = Array::make(n * 3 * net.bn_chan * t, 0.0F)
  let a = x_s.length() / n
  let mut i = 0
  while i < n {
    let mut ch = 0
    while ch < net.bn_chan {
      // The three stacks all have the same shape, so the SOURCE index is the same
      // for all of them; only the DESTINATION carries the group offset. Adding
      // the offset to the source too walks straight off the end of the stack.
      let dst = (i * 3 * net.bn_chan + ch) * t
      let src = (i * net.bn_chan + ch) * t
      let mut ti = 0
      while ti < t {
        cat[dst + ti] = x_s[src + ti]
        cat[dst + ti + net.bn_chan * t] = x_m[src + ti]
        cat[dst + ti + 2 * net.bn_chan * t] = x_l[src + ti]
        ti = ti + 1
      }
      ch = ch + 1
    }
    i = i + 1
  }
  let mfa = linear_forward(
    cat,
    n * t,
    LinearParam::new(net.mfa_w, net.mfa_b, 3 * net.bn_chan, net.fuse_out),
  )
  let mfa_relu = relu_forward(mfa)
  let fused = se_forward(mfa_relu, n, net.mfa_se)
  let (pooled, _asp_cache) = asp_forward(fused, n, net.asp)
  let (bn_seg, _s1) = batch_norm2d_forward(
    pooled, n, 2 * net.fuse_out, 1, 1, net.bn_seg,
  )
  let emb = linear_forward(
    bn_seg,
    n,
    LinearParam::new(net.fc_w, net.fc_b, 2 * net.fuse_out, net.emb_dim),
  )
  let (embedding, _s2) = batch_norm2d_forward(
    emb, n, net.emb_dim, 1, 1, net.bn_out,
  )
  let logits = linear_forward(
    embedding,
    n,
    LinearParam::new(net.cls_w, net.cls_b, net.emb_dim, net.n_classes),
  )
  { logits, embedding, pooled: bn_seg, fused, bottleneck: bn, bottleneck_ln_cache: bn_ln_cache }
}

///|
/// Run one parallel stack end to end.
fn tn_run_stack(
  stack : Array[TarnetBlock],
  x : Array[Float],
  n : Int,
) -> Array[Float] {
  let mut cur = x
  let mut i = 0
  while i < stack.length() {
    let (next, _cache) = tarnet_block_forward(stack[i], cur, n)
    cur = next
    i = i + 1
  }
  cur
}
// ---------------------------------------------------------------------------
// flat parameter view
// ---------------------------------------------------------------------------
//
// WHY: the model-level gradient gate needs to perturb the WHOLE parameter
// vector along one direction and compare -- a directional derivative, which
// averages over every coordinate and so cannot be defeated by individually
// small gradients. Same reason `ctse_flatten` exists.
//
// Layout, in this exact order:
//   ln_in.gamma, ln_in.beta
//   bn_w, bn_b
//   for each stack in (stack_s, stack_m, stack_l), for each block in order:
//     pw1, pb1, ln1.gamma, ln1.beta, dw.weight, dw.bias, prelu1,
//     ln2.gamma, ln2.beta, pw2, pb2, then ctse_flatten of the block's se
//   mfa_w, mfa_b, mfa_se(w1,b1,w2,b2)
//   asp(w1,b1,w2,b2)
//   bn_seg.gamma, bn_seg.beta, fc_w, fc_b, bn_out.gamma, bn_out.beta
//   cls_w, cls_b

///|
/// Copy `len` scalars out of `flat` starting at `off`.
fn tn_take(flat : Array[Float], off : Int, len : Int) -> Array[Float] {
  let a : Array[Float] = Array::make(len, 0.0F)
  let mut i = 0
  while i < len {
    a[i] = flat[off + i]
    i = i + 1
  }
  a
}

///|
/// Write `src` into `dst` at `off`; returns the next free offset.
fn tn_put(dst : Array[Float], off : Int, src : Array[Float]) -> Int {
  let mut i = 0
  while i < src.length() {
    dst[off + i] = src[i]
    i = i + 1
  }
  off + src.length()
}

///|
/// How many scalars one `TarnetBlock` contributes.
fn tn_block_len(b : TarnetBlock, t : Int) -> Int {
  b.hid * b.chan + b.hid +
  b.hid * t * 2 +
  b.dw.weight.length() + b.dw.bias.length() + 1 +
  b.hid * t * 2 +
  b.chan * b.hid + b.chan +
  ctse_param_count(b.se)
}

///|
/// Number of scalars in a flattened `Tarnet`.
pub fn tarnet_param_count(net : Tarnet) -> Int {
  let t = net.t_frames
  let mut total = net.in_chan * t * 2
  total = total + net.bn_chan * net.in_chan + net.bn_chan
  let stacks = [net.stack_s, net.stack_m, net.stack_l]
  for st in stacks {
    for b in st {
      total = total + tn_block_len(b, t)
    }
  }
  total = total + net.fuse_out * 3 * net.bn_chan + net.fuse_out
  let bk = net.mfa_se.bottleneck
  let c = net.mfa_se.channels
  total = total + bk * c + bk + c * bk + c
  let ac = net.asp.channels
  let ah = net.asp.att_hidden
  total = total + ah * 3 * ac + ah + ac * ah + ac
  total = total + 2 * net.fuse_out * 2
  total = total + net.emb_dim * 2 * net.fuse_out + net.emb_dim
  total = total + net.emb_dim * 2
  total = total + net.n_classes * net.emb_dim + net.n_classes
  total
}

///|
/// Flatten a `Tarnet`. Order is documented above `tarnet_param_count`.
pub fn tarnet_flatten(net : Tarnet) -> Array[Float] {
  let flat : Array[Float] = Array::make(tarnet_param_count(net), 0.0F)
  let mut o = 0
  o = tn_put(flat, o, net.ln_in.gamma)
  o = tn_put(flat, o, net.ln_in.beta)
  o = tn_put(flat, o, net.bn_w)
  o = tn_put(flat, o, net.bn_b)
  let stacks = [net.stack_s, net.stack_m, net.stack_l]
  for st in stacks {
    for b in st {
      o = tn_put(flat, o, b.pw1)
      o = tn_put(flat, o, b.pb1)
      o = tn_put(flat, o, b.ln1.gamma)
      o = tn_put(flat, o, b.ln1.beta)
      o = tn_put(flat, o, b.dw.weight)
      o = tn_put(flat, o, b.dw.bias)
      o = tn_put(flat, o, [b.prelu1])
      o = tn_put(flat, o, b.ln2.gamma)
      o = tn_put(flat, o, b.ln2.beta)
      o = tn_put(flat, o, b.pw2)
      o = tn_put(flat, o, b.pb2)
      o = tn_put(flat, o, ctse_flatten(b.se))
    }
  }
  o = tn_put(flat, o, net.mfa_w)
  o = tn_put(flat, o, net.mfa_b)
  o = tn_put(flat, o, net.mfa_se.w1)
  o = tn_put(flat, o, net.mfa_se.b1)
  o = tn_put(flat, o, net.mfa_se.w2)
  o = tn_put(flat, o, net.mfa_se.b2)
  o = tn_put(flat, o, net.asp.w1)
  o = tn_put(flat, o, net.asp.b1)
  o = tn_put(flat, o, net.asp.w2)
  o = tn_put(flat, o, net.asp.b2)
  o = tn_put(flat, o, net.bn_seg.gamma)
  o = tn_put(flat, o, net.bn_seg.beta)
  o = tn_put(flat, o, net.fc_w)
  o = tn_put(flat, o, net.fc_b)
  o = tn_put(flat, o, net.bn_out.gamma)
  o = tn_put(flat, o, net.bn_out.beta)
  o = tn_put(flat, o, net.cls_w)
  o = tn_put(flat, o, net.cls_b)
  flat
}

///|
/// Rebuild one block from `flat`, starting at `off`. Returns the block and the
/// next free offset.
fn tn_block_unflatten(
  proto : TarnetBlock,
  flat : Array[Float],
  off : Int,
  t : Int,
) -> (TarnetBlock, Int) {
  let mut o = off
  let pw1 = tn_take(flat, o, proto.hid * proto.chan)
  o = o + pw1.length()
  let pb1 = tn_take(flat, o, proto.hid)
  o = o + pb1.length()
  let l1g = tn_take(flat, o, proto.hid * t)
  o = o + l1g.length()
  let l1b = tn_take(flat, o, proto.hid * t)
  o = o + l1b.length()
  let dww = tn_take(flat, o, proto.dw.weight.length())
  o = o + dww.length()
  let dwb = tn_take(flat, o, proto.dw.bias.length())
  o = o + dwb.length()
  let prelu = tn_take(flat, o, 1)[0]
  o = o + 1
  let l2g = tn_take(flat, o, proto.hid * t)
  o = o + l2g.length()
  let l2b = tn_take(flat, o, proto.hid * t)
  o = o + l2b.length()
  let pw2 = tn_take(flat, o, proto.chan * proto.hid)
  o = o + pw2.length()
  let pb2 = tn_take(flat, o, proto.chan)
  o = o + pb2.length()
  let se_flat = tn_take(flat, o, ctse_param_count(proto.se))
  o = o + se_flat.length()
  let blk = {
    pw1: pw1,
    pb1: pb1,
    ln1: { gamma: l1g, beta: l1b, eps: proto.ln1.eps, c: proto.hid, h: 1, w: t },
    dw: conv1d_depthwise_with(proto.dw, dww, dwb),
    prelu1: prelu,
    ln2: { gamma: l2g, beta: l2b, eps: proto.ln2.eps, c: proto.hid, h: 1, w: t },
    pw2: pw2,
    pb2: pb2,
    se: ctse_unflatten(proto.se, se_flat),
    chan: proto.chan,
    hid: proto.hid,
    dilation: proto.dilation,
  }
  (blk, o)
}

///|
/// Rebuild one stack from `flat`, starting at `off`.
fn tn_stack_unflatten(
  proto : Array[TarnetBlock],
  flat : Array[Float],
  off : Int,
  t : Int,
) -> (Array[TarnetBlock], Int) {
  let out : Array[TarnetBlock] = []
  let mut o = off
  let mut i = 0
  while i < proto.length() {
    let (blk, next) = tn_block_unflatten(proto[i], flat, o, t)
    out.push(blk)
    o = next
    i = i + 1
  }
  (out, o)
}

///|
/// Inverse of `tarnet_flatten`. Returns a NEW `Tarnet`; the input is untouched.
pub fn tarnet_unflatten(
  net : Tarnet,
  flat : Array[Float],
) -> Tarnet {
  let want = tarnet_param_count(net)
  if flat.length() != want {
    abort("tarnet_unflatten: expected \{want} parameters, got \{flat.length()}")
  }
  let t = net.t_frames
  let mut o = 0
  let ln_g = tn_take(flat, o, net.in_chan * t)
  o = o + ln_g.length()
  let ln_b = tn_take(flat, o, net.in_chan * t)
  o = o + ln_b.length()
  let bn_w = tn_take(flat, o, net.bn_chan * net.in_chan)
  o = o + bn_w.length()
  let bn_b = tn_take(flat, o, net.bn_chan)
  o = o + bn_b.length()
  let (s_s, o1) = tn_stack_unflatten(net.stack_s, flat, o, t)
  let (s_m, o2) = tn_stack_unflatten(net.stack_m, flat, o1, t)
  let (s_l, o3) = tn_stack_unflatten(net.stack_l, flat, o2, t)
  let mut o = o3
  let mfa_w = tn_take(flat, o, net.fuse_out * 3 * net.bn_chan)
  o = o + mfa_w.length()
  let mfa_b = tn_take(flat, o, net.fuse_out)
  o = o + mfa_b.length()
  let se1 = net.mfa_se
  let sw1 = tn_take(flat, o, se1.bottleneck * se1.channels)
  o = o + sw1.length()
  let sb1 = tn_take(flat, o, se1.bottleneck)
  o = o + sb1.length()
  let sw2 = tn_take(flat, o, se1.channels * se1.bottleneck)
  o = o + sw2.length()
  let sb2 = tn_take(flat, o, se1.channels)
  o = o + sb2.length()
  let asp = net.asp
  let aw1 = tn_take(flat, o, asp.att_hidden * 3 * asp.channels)
  o = o + aw1.length()
  let ab1 = tn_take(flat, o, asp.att_hidden)
  o = o + ab1.length()
  let aw2 = tn_take(flat, o, asp.channels * asp.att_hidden)
  o = o + aw2.length()
  let ab2 = tn_take(flat, o, asp.channels)
  o = o + ab2.length()
  let sg_g = tn_take(flat, o, 2 * net.fuse_out)
  o = o + sg_g.length()
  let sg_b = tn_take(flat, o, 2 * net.fuse_out)
  o = o + sg_b.length()
  let fc_w = tn_take(flat, o, net.emb_dim * 2 * net.fuse_out)
  o = o + fc_w.length()
  let fc_b = tn_take(flat, o, net.emb_dim)
  o = o + fc_b.length()
  let bo_g = tn_take(flat, o, net.emb_dim)
  o = o + bo_g.length()
  let bo_b = tn_take(flat, o, net.emb_dim)
  o = o + bo_b.length()
  let cls_w = tn_take(flat, o, net.n_classes * net.emb_dim)
  o = o + cls_w.length()
  let cls_b = tn_take(flat, o, net.n_classes)
  o = o + cls_b.length()
  {
    ln_in: { gamma: ln_g, beta: ln_b, eps: net.ln_in.eps, c: net.in_chan, h: 1, w: t },
    bn_w: bn_w,
    bn_b: bn_b,
    stack_s: s_s,
    stack_m: s_m,
    stack_l: s_l,
    mfa_w: mfa_w,
    mfa_b: mfa_b,
    mfa_se: {
      w1: sw1,
      b1: sb1,
      w2: sw2,
      b2: sb2,
      channels: se1.channels,
      bottleneck: se1.bottleneck,
    },
    asp: {
      w1: aw1,
      b1: ab1,
      w2: aw2,
      b2: ab2,
      channels: asp.channels,
      att_hidden: asp.att_hidden,
    },
    bn_seg: {
      gamma: sg_g,
      beta: sg_b,
      running_mean: net.bn_seg.running_mean.copy(),
      running_var: net.bn_seg.running_var.copy(),
      momentum: net.bn_seg.momentum,
      eps: net.bn_seg.eps,
      training: net.bn_seg.training,
    },
    fc_w: fc_w,
    fc_b: fc_b,
    bn_out: {
      gamma: bo_g,
      beta: bo_b,
      running_mean: net.bn_out.running_mean.copy(),
      running_var: net.bn_out.running_var.copy(),
      momentum: net.bn_out.momentum,
      eps: net.bn_out.eps,
      training: net.bn_out.training,
    },
    cls_w: cls_w,
    cls_b: cls_b,
    in_chan: net.in_chan,
    bn_chan: net.bn_chan,
    hid_chan: net.hid_chan,
    fuse_out: net.fuse_out,
    emb_dim: net.emb_dim,
    n_classes: net.n_classes,
    t_frames: net.t_frames,
  }
}

///|
/// Caches one block needs for its backward. The plain forward discarded the
/// two gLN caches and the SE cache, which makes the backward impossible.
pub(all) struct TarnetBlockFullCache {
  x : Array[Float]
  pw1_out : Array[Float]
  pw1_post : Array[Float]
  ln1_out : Array[Float]
  ln1_cache : LayerNormCache
  dw_out : Array[Float]
  dw_post : Array[Float]
  ln2_out : Array[Float]
  ln2_cache : LayerNormCache
  pw2_out : Array[Float]
  se_cache : CtseCache
}

///|
/// Block forward, keeping everything the backward needs.
pub fn tarnet_block_forward_full(
  b : TarnetBlock,
  x : Array[Float],
  n : Int,
) -> (Array[Float], TarnetBlockFullCache) {
  let t = x.length() / (n * b.chan)
  let pw1_out = tn_conv1x1_forward(x, n, b.chan, b.hid, b.pw1, b.pb1)
  let pw1_post = prelu_forward(pw1_out, b.prelu1)
  let (ln1_out, ln1_cache) = layer_norm_forward(
    pw1_post, n, b.hid, 1, t, b.ln1,
  )
  let dw_out = conv1d_depthwise_forward(ln1_out, n, b.dw)
  let dw_post = prelu_forward(dw_out, b.prelu1)
  let (ln2_out, ln2_cache) = layer_norm_forward(
    dw_post, n, b.hid, 1, t, b.ln2,
  )
  let pw2_out = tn_conv1x1_forward(
    ln2_out, n, b.hid, b.chan, b.pw2, b.pb2,
  )
  let (r, se_cache) = ctse_forward(pw2_out, n, b.se)
  let out : Array[Float] = Array::make(n * b.chan * t, 0.0F)
  let mut i = 0
  while i < n * b.chan * t {
    out[i] = x[i] + r[i]
    i = i + 1
  }
  (
    out,
    {
      x: x,
      pw1_out: pw1_out,
      pw1_post: pw1_post,
      ln1_out: ln1_out,
      ln1_cache: ln1_cache,
      dw_out: dw_out,
      dw_post: dw_post,
      ln2_out: ln2_out,
      ln2_cache: ln2_cache,
      pw2_out: pw2_out,
      se_cache: se_cache,
    },
  )
}

///|
/// Gradients of one block, in the same order as `tarnet_flatten`.
pub(all) struct TarnetBlockGrad {
  pw1 : DnLayerGrad
  ln1_g : Array[Float]
  ln1_b : Array[Float]
  dw : DnLayerGrad
  prelu : Float
  ln2_g : Array[Float]
  ln2_b : Array[Float]
  pw2 : DnLayerGrad
  se : CtseGrad
}

///|
/// Block backward. Returns `(d_x, gradients)`.
///
/// The two PReLUs SHARE one slope here, where the reference declares two
/// separate `nn.PReLU()` layers. That is a documented deviation, so the
/// slope's gradient is the sum of both sites'.
pub fn tarnet_block_backward(
  b : TarnetBlock,
  n : Int,
  cache : TarnetBlockFullCache,
  d_out : Array[Float],
  l2 : Float,
) -> (Array[Float], TarnetBlockGrad) {
  // `out = x + r`, so the incoming gradient reaches BOTH branches: r's branch
  // below and x's, which is added at the end.
  let t = cache.x.length() / (n * b.chan)
  // The CTSE is a nonlinearity: the gradient arriving at `pw2_out` is the SE's
  // own d_x, NOT d_out. Passing d_out treats the gate as the identity.
  let se_g = ctse_backward(d_out, n, b.se, cache.se_cache)
  let (d_ln2_out, dw2, db2) = tn_conv1x1_backward(
    cache.ln2_out, se_g.d_x, n, b.hid, b.chan, b.pw2,
  )
  let (d_dw_post, ln2_g, ln2_b) = layer_norm_backward(
    d_ln2_out, cache.ln2_cache, b.ln2,
  )
  let (d_dw_out, d_prelu2) = prelu_backward(
    cache.dw_out, d_dw_post, b.prelu1,
  )
  let (d_ln1_out, dww, dwb) = conv1d_depthwise_backward(
    cache.ln1_out, d_dw_out, n, b.dw,
  )
  let (d_pw1_post, ln1_g, ln1_b) = layer_norm_backward(
    d_ln1_out, cache.ln1_cache, b.ln1,
  )
  let (d_pw1_out, d_prelu1) = prelu_backward(
    cache.pw1_out, d_pw1_post, b.prelu1,
  )
  let (d_x, dw1, db1) = tn_conv1x1_backward(
    cache.x, d_pw1_out, n, b.chan, b.hid, b.pw1,
  )
  let d_total : Array[Float] = Array::make(d_x.length(), 0.0F)
  let mut i = 0
  while i < d_x.length() {
    d_total[i] = d_x[i] + d_out[i]
    i = i + 1
  }
  // `dn_l2` returns the `(d_weight, d_bias)` tuple as a whole; destructuring
  // it with `_` is not accepted here.
  let pw1g = dn_l2(dw1, db1, b.pw1, l2)
  let pw2g = dn_l2(dw2, db2, b.pw2, l2)
  let _ = t
  (
    d_total,
    {
      pw1: pw1g,
      ln1_g: ln1_g,
      ln1_b: ln1_b,
      dw: (dww, dwb),
      prelu: d_prelu1 + d_prelu2,
      ln2_g: ln2_g,
      ln2_b: ln2_b,
      pw2: pw2g,
      se: se_g,
    },
  )
}

///|
/// The channel-time SE backward, hoisted here so the block's gradient record

///|
/// Flatten one `TarnetBlock`, in the same order the block region of
/// `tarnet_flatten` uses:
///
///   pw1, pb1, ln1.gamma, ln1.beta, dw.weight, dw.bias, prelu1,
///   ln2.gamma, ln2.beta, pw2, pb2, then ctse_flatten of the block's se
pub fn tarnet_block_flatten(b : TarnetBlock) -> Array[Float] {
  // the frame count is the layer norm's `w`; taking it from there rather than
  // from the caller means the two cannot disagree
  let t = b.ln1.w
  let flat : Array[Float] = Array::make(tn_block_len(b, t), 0.0F)
  let mut o = 0
  o = tn_put(flat, o, b.pw1)
  o = tn_put(flat, o, b.pb1)
  o = tn_put(flat, o, b.ln1.gamma)
  o = tn_put(flat, o, b.ln1.beta)
  o = tn_put(flat, o, b.dw.weight)
  o = tn_put(flat, o, b.dw.bias)
  o = tn_put(flat, o, [b.prelu1])
  o = tn_put(flat, o, b.ln2.gamma)
  o = tn_put(flat, o, b.ln2.beta)
  o = tn_put(flat, o, b.pw2)
  o = tn_put(flat, o, b.pb2)
  o = tn_put(flat, o, ctse_flatten(b.se))
  flat
}

///|
/// Rebuild one block around a new parameter vector, reusing the existing
/// `tn_block_unflatten` so the two cannot disagree about the layout.
pub fn tarnet_block_unflatten(
  proto : TarnetBlock,
  flat : Array[Float],
) -> TarnetBlock {
  let (blk, _o) = tn_block_unflatten(proto, flat, 0, proto.ln1.w)
  blk
}

///|
/// Flatten a `TarnetBlockGrad` in the same order as `tarnet_block_flatten`.
pub fn tarnet_block_grad_flatten(
  g : TarnetBlockGrad,
  b : TarnetBlock,
) -> Array[Float] {
  let flat : Array[Float] = Array::make(tn_block_len(b, b.ln1.w), 0.0F)
  let mut o = 0
  o = tn_put(flat, o, g.pw1.0)
  o = tn_put(flat, o, g.pw1.1)
  o = tn_put(flat, o, g.ln1_g)
  o = tn_put(flat, o, g.ln1_b)
  o = tn_put(flat, o, g.dw.0)
  o = tn_put(flat, o, g.dw.1)
  o = tn_put(flat, o, [g.prelu])
  o = tn_put(flat, o, g.ln2_g)
  o = tn_put(flat, o, g.ln2_b)
  o = tn_put(flat, o, g.pw2.0)
  o = tn_put(flat, o, g.pw2.1)
  o = tn_put(flat, o, ctse_grad_flatten(g.se))
  flat
}

///|
/// 1x1 convolution over CHANNELS, in `[n, c, t]` layout.
///
/// This exists because `linear_forward(x, n_batch, LP)` assumes `[n_batch,
/// features]` -- index `batch*C + c` -- while the layer norms in this package
/// assume `[n, C, T]` -- index `(b*C + c)*t + ti`. Those are different
/// permutations of the same memory, so routing a layer-norm output into a
/// `linear_*` call silently reinterprets it. Inside a block every tensor is
/// `[n, C, T]`, so this pair is the layout-correct one to use.
pub fn tn_conv1x1_forward(
  x : Array[Float],
  n : Int,
  in_ch : Int,
  out_ch : Int,
  weight : Array[Float],
  bias : Array[Float],
) -> Array[Float] {
  let t = x.length() / (n * in_ch)
  let out : Array[Float] = Array::make(n * out_ch * t, 0.0F)
  for b in 0..<n {
    for ti in 0..<t {
      for o in 0..<out_ch {
        let mut acc = bias[o]
        let wo = o * in_ch
        for i in 0..<in_ch {
          acc = acc + weight[wo + i] * x[(b * in_ch + i) * t + ti]
        }
        out[(b * out_ch + o) * t + ti] = acc
      }
    }
  }
  out
}

///|
/// Backward of `tn_conv1x1_forward`: `(d_input, d_weight, d_bias)`, all in
/// `[n, C, T]` layout.
pub fn tn_conv1x1_backward(
  x : Array[Float],
  d_out : Array[Float],
  n : Int,
  in_ch : Int,
  out_ch : Int,
  weight : Array[Float],
) -> (Array[Float], Array[Float], Array[Float]) {
  let t = x.length() / (n * in_ch)
  let d_x : Array[Float] = Array::make(n * in_ch * t, 0.0F)
  let d_w : Array[Float] = Array::make(out_ch * in_ch, 0.0F)
  let d_b : Array[Float] = Array::make(out_ch, 0.0F)
  for b in 0..<n {
    for ti in 0..<t {
      for o in 0..<out_ch {
        let g = d_out[(b * out_ch + o) * t + ti]
        d_b[o] = d_b[o] + g
        let wo = o * in_ch
        for i in 0..<in_ch {
          let xv = x[(b * in_ch + i) * t + ti]
          d_w[wo + i] = d_w[wo + i] + g * xv
          d_x[(b * in_ch + i) * t + ti] = d_x[(b * in_ch + i) * t + ti] +
            g * weight[wo + i]
        }
      }
    }
  }
  (d_x, d_w, d_b)
}

///|
/// Caches for one parallel stack: the stack input plus every block's cache.
pub(all) struct TarnetStackCache {
  x0 : Array[Float]
  caches : Array[TarnetBlockFullCache]
}

///|
/// Run a stack forward, keeping every block's cache.
pub fn tn_stack_forward_full(
  stack : Array[TarnetBlock],
  x : Array[Float],
  n : Int,
) -> (Array[Float], TarnetStackCache) {
  let caches : Array[TarnetBlockFullCache] = []
  let mut cur = x
  let mut i = 0
  while i < stack.length() {
    let (next, c) = tarnet_block_forward_full(stack[i], cur, n)
    caches.push(c)
    cur = next
    i = i + 1
  }
  (cur, { x0: x, caches: caches })
}

///|
/// Run a stack backward: reverse walk, accumulating the gradient that reaches
/// the stack input and returning one `TarnetBlockGrad` per block IN FORWARD
/// ORDER (the reverse walk naturally produces them backwards).
pub fn tn_stack_backward(
  stack : Array[TarnetBlock],
  n : Int,
  cache : TarnetStackCache,
  d_out : Array[Float],
  l2 : Float,
) -> (Array[Float], Array[TarnetBlockGrad]) {
  let rev : Array[TarnetBlockGrad] = []
  let mut d = d_out
  let mut i = stack.length() - 1
  while i >= 0 {
    let (d_in, g) = tarnet_block_backward(stack[i], n, cache.caches[i], d, l2)
    rev.push(g)
    d = d_in
    i = i - 1
  }
  let ordered : Array[TarnetBlockGrad] = []
  let mut k = rev.length() - 1
  while k >= 0 {
    ordered.push(rev[k])
    k = k - 1
  }
  (d, ordered)
}

///|
/// Bump every block of `stack` by `delta * dir`, regenerating `dir` from
/// `seed + block_index` so the SAME direction is used for the up and the down
/// perturbation -- a gate needs both sides of one direction, not two
/// unrelated ones.
pub fn tn_stack_bump(
  stack : Array[TarnetBlock],
  delta : Float,
  seed : UInt64,
) -> Array[TarnetBlock] {
  let out : Array[TarnetBlock] = []
  let mut i = 0
  while i < stack.length() {
    let fb = tarnet_block_flatten(stack[i])
    let dir = tn_randn(
      fb.length(),
      1.0F,
      Xoshiro::new(seed + i.to_uint64() * 7919UL),
    )
    let bumped = fb.copy()
    let mut k = 0
    while k < bumped.length() {
      bumped[k] = bumped[k] + delta * dir[k]
      k = k + 1
    }
    out.push(tarnet_block_unflatten(stack[i], bumped))
    i = i + 1
  }
  out
}
