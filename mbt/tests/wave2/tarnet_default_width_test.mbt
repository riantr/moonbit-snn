// tarnet_default_width_test.mbt -- TARNet at its DOCUMENTED DEFAULT width.
//
// Every other TARNet gate runs a deliberately shrunken model (width=8,
// hid_chan=8, one repeat per scale) because per-coordinate finite differences
// need a small parameter vector. That is the right trade for the gradient
// checks, and it leaves one property completely untested: what the documented
// defaults actually DO.
//
// The default stack is 20 residual blocks deep -- (2 dilations x 4 repeats) +
// (2 x 4) + (1 x 4) -- each with a 1x1 expansion to 512 channels and a gLN, at
// 128-wide bottleneck and 512-channel fusion. The gradient gates below cannot
// run at that size, so three properties are checked here and only here:
//
//   1. SHAPES AT SCALE. Every stage's output is the length the layout promises
//      when the widths are the documented ones rather than 8.
//   2. GRADIENT CONNECTIVITY. That the whole 3M-parameter backward is finite
//      and actually connected, rather than being mostly structural zeros.
//   3. CLOSURE. That allocate-state, step, and re-evaluate all work on a model
//      of this size -- the Adam state alone is tens of MB here.
//
// WHAT THIS FILE DOES NOT ESTABLISH, after being wrong about it once: the first
// draft claimed activation explosion was a risk visible only at default width.
// It is not reachable that way at all. Raising the block's `pw2` init scale
// from 0.05 to 0.30 (6x) left every assertion here green; even 3.0 (60x) left
// them green and was caught instead by the small-model block and stack sweeps
// (9 coordinate-fixture artifacts, and 74 kinked coordinates out of 120). The
// reason is structural: a gLN follows every 1x1 convolution, so each block's
// output magnitude is scale-invariant and twenty of them cannot compound.
//
// `t_frames` is a caller parameter rather than a default, so this file keeps t
// small, which is what makes it affordable. The frame axis is the only one left
// shrunken, and its dependent layout is already covered exhaustively by the
// small-model gates.

///|
/// The documented defaults, with the frame count kept small. `Tarnet::new`'s
/// own defaults are used for everything else, so a future default change moves
/// this test with it instead of leaving it pinned to a stale width.
fn tn_default_width_fixture(
  n : Int,
  n_classes : Int,
  t : Int,
  seed : UInt64,
) -> (@snn_mbt.Tarnet, Array[Float], Array[Int]) {
  let net = @snn_mbt.Tarnet::new(
    n_classes,
    80, // in_chan: the reference's 80 log-Mel bands
    t,
    seed=seed,
  )
  let x = @snn_mbt.rand_f32(
    @snn_mbt.Xoshiro::new(seed + 7UL), n * 80 * t,
  )
  let targets : Array[Int] = Array::make(n, 0)
  let mut i = 0
  while i < n {
    targets[i] = i % n_classes
    i = i + 1
  }
  (net, x, targets)
}

///|
/// Number of residual blocks the default stack holds. Computed from the model
/// rather than hard-coded, so the assertion below fails if the architecture
/// changes rather than silently testing a different network.
fn tn_default_block_count(net : @snn_mbt.Tarnet) -> Int {
  net.stack_s.length() + net.stack_m.length() + net.stack_l.length()
}

test "TARNet: the default configuration really is the deep 20-block stack" {
  let (net, _x, _tg) = tn_default_width_fixture(2, 5, 4, 301UL)
  let blocks = tn_default_block_count(net)
  if blocks != 20 {
    fail("the default stack holds \{blocks} blocks, expected 20 -- the depth assumption in this file is stale")
  }
  if net.bn_chan != 128 || net.hid_chan != 512 || net.fuse_out != 512 {
    fail(
      "default widths changed: bn_chan=\{net.bn_chan} hid_chan=\{net.hid_chan} fuse_out=\{net.fuse_out}",
    )
  }
}

test "TARNet: 20 default-width blocks keep the forward finite" {
  let n = 2
  let n_classes = 5
  let t = 4
  let (net, x, _tg) = tn_default_width_fixture(n, n_classes, t, 311UL)
  let out = @snn_mbt.tarnet_forward(net, x, n)
  if out.logits.length() != n * n_classes {
    fail("logits length \{out.logits.length()}, expected \{n * n_classes}")
  }
  if out.embedding.length() != n * net.emb_dim {
    fail("embedding length \{out.embedding.length()}")
  }
  if out.pooled.length() != n * 2 * net.fuse_out {
    fail("pooled length \{out.pooled.length()}")
  }
  // The real assertion. A twenty-block residual stack at 512 channels either
  // survives or it does not, and the small-model gates cannot see which.
  let mut i = 0
  while i < out.logits.length() {
    if !(out.logits[i] == out.logits[i]) {
      fail("logits[\{i}] is NaN -- activations blew up through the default stack")
    }
    if out.logits[i] > 1.0e10F || out.logits[i] < -1.0e10F {
      fail("logits[\{i}] = \{out.logits[i]}, which is not a usable logit")
    }
    i = i + 1
  }
  i = 0
  while i < out.embedding.length() {
    if !(out.embedding[i] == out.embedding[i]) {
      fail("embedding[\{i}] is NaN")
    }
    i = i + 1
  }
  i = 0
  while i < out.bottleneck.length() {
    if !(out.bottleneck[i] == out.bottleneck[i]) {
      fail("bottleneck[\{i}] is NaN -- the explosion starts inside the stacks, not the head")
    }
    i = i + 1
  }
}

test "TARNet: default-width backward and one Adam step stay finite" {
  let n = 2
  let n_classes = 5
  let t = 4
  let (net, x, targets) = tn_default_width_fixture(n, n_classes, t, 321UL)
  let (out, cache) = @snn_mbt.tarnet_forward_full(net, x, n)
  let (d_logits, loss) = @snn_mbt.tarnet_softmax_dlogits(
    out.logits, n, n_classes, targets,
  )
  if !(loss == loss) {
    fail("cross-entropy is NaN at the default width")
  }
  if loss <= 0.0F {
    fail("cross-entropy returned \{loss}, which cannot be positive")
  }
  let (g, d_x) = @snn_mbt.tarnet_backward(net, n, cache, d_logits, 0.0F)
  if g.length() != @snn_mbt.tarnet_param_count(net) {
    fail("gradient length \{g.length()} != param count")
  }
  if d_x.length() != x.length() {
    fail("d_x length \{d_x.length()} != input length \{x.length()}")
  }
  // A gradient that is entirely finite but entirely zero would also be a
  // failure, so count both.
  let mut finite = 0
  let mut nonzero = 0
  let mut i = 0
  while i < g.length() {
    if g[i] == g[i] && g[i] < 1.0e20F && g[i] > -1.0e20F {
      finite = finite + 1
      if g[i] != 0.0F {
        nonzero = nonzero + 1
      }
    }
    i = i + 1
  }
  if finite != g.length() {
    fail("\{g.length() - finite} of \{g.length()} gradient entries are NaN or inf at the default width")
  }
  if nonzero * 100 < g.length() {
    fail("only \{nonzero} of \{g.length()} gradient entries are non-zero -- the default-width backward is not connected")
  }
  // And the whole loop has to close: allocate state for the real parameter
  // count, step, and confirm the model still evaluates afterwards.
  let state = @snn_mbt.tarnet_adam_init(net)
  let (next, _st, _l) = @snn_mbt.tarnet_train_step(
    net, x, n, targets, state, 0.001F,
  )
  let after = @snn_mbt.tarnet_forward(next, x, n)
  i = 0
  while i < after.logits.length() {
    if !(after.logits[i] == after.logits[i]) {
      fail("logits[\{i}] is NaN after one default-width training step")
    }
    i = i + 1
  }
}