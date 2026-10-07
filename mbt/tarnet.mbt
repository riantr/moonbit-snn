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
  let pw1_out = linear_forward(
    x, n * t, LinearParam::new(b.pw1, b.pb1, b.chan, b.hid),
  )
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
  let pw2_out = linear_forward(
    ln2_out, n * t, LinearParam::new(b.pw2, b.pb2, b.hid, b.chan),
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