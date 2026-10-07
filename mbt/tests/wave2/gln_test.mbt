// gln_test.mbt -- gate for asteroid's GlobLN, the norm TARNet's reference uses.
//
// Reference: `asteroid/masknn/norms.py` (gLN = GlobLN), which the TARNet
// reference selects through `norms.get("gLN")`.
//
// This is a NEW type, not a retune of `LayerNorm`, and the difference is the
// whole point:
//
//   GlobLN    gamma/beta have shape (C,), broadcast over time.  C parameters.
//   LayerNorm gamma/beta have shape (c*h*w), one per element.  C*T parameters.
//
// So the gates that matter are: the normalising axis is (C, T) JOINTLY (one
// mean and one std per sample, not per channel), the affine is per CHANNEL,
// and the input gradient is correct for a group-normalised affine. A per-
// channel mean would pass a shape check and every "is it finite" check.

fn gln_obj(
  x : Array[Float],
  n : Int,
  c : Int,
  t : Int,
  p : @snn_mbt.GlobalLayerNorm,
  w : Array[Float],
) -> Float {
  let (y, _cache) = @snn_mbt.gln_forward(x, n, c, t, p)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
fn gln_fixture(
  c : Int,
  t : Int,
  lift : Float,
  seed : UInt64,
) -> (@snn_mbt.GlobalLayerNorm, Array[Float], Array[Float]) {
  let proto = @snn_mbt.GlobalLayerNorm::new(c)
  let n = 3
  let x = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(seed), n * c * t)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(seed + 1UL), n * c * t)
  // gamma = 1, beta = 0 is the identity-ish default and would leave d_gamma
  // and d_beta measuring the same thing as the pure-normalisation gradient.
  // Perturb both so all three outputs are actually exercised.
  let gamma = proto.gamma.copy()
  let beta = proto.beta.copy()
  let mut i = 0
  while i < c {
    gamma[i] = gamma[i] + lift * Float::from_int(i + 1)
    beta[i] = beta[i] - lift * Float::from_int(c - i)
    i = i + 1
  }
  ({ gamma, beta, eps: proto.eps, c }, x, w)
}

const GLN_H : Float = 3.0e-3F

test "gLN: one mean and one std per SAMPLE, over the (c, t) plane jointly" {
  // A per-CHANNEL mean would give a different answer here, and a per-frame
  // mean another. gamma/beta are at their defaults so the raw normalised
  // values are visible.
  let c = 3
  let t = 4
  let n = 2
  let p = @snn_mbt.GlobalLayerNorm::new(c)
  let x : Array[Float] = Array::make(n * c * t, 0.0F)
  // sample 0: 0..11, sample 1: 100..111
    let ct = c * t
  let mut i = 0
  while i < n {
    for k in 0..<ct {
      x[i * c * t + k] = Float::from_int(i * 100 + k)
    }
    i = i + 1
  }
  let (y, _cache) = @snn_mbt.gln_forward(x, n, c, t, p)
  // per-sample mean of 0..11 is 5.5; of 100..111 is 105.5
  let mean0 = 5.5F
  let mean1 = 105.5F
  // biased variance of 0..11 = 11.9166667
  let var0 = 11.916666666666666F
  let inv0 = 1.0F / @snn_mbt.sqrtf(var0 + p.eps)
  i = 0
  while i < ct {
    let want0 = (Float::from_int(i) - mean0) * inv0
    // sample 1 holds 100 + i, so its centred value is (100 + i) - 105.5, which
    // is EXACTLY i - 5.5 -- the same number as sample 0. That identity is the
    // "global" property, so it is asserted below rather than assumed here.
    let want1 = (Float::from_int(100 + i) - mean1) * inv0
    if (y[i] - want0).abs() > 1.0e-4F {
      fail("sample 0 element \{i}: \{y[i]}, expected \{want0} -- is the mean taken per channel instead of per sample?")
    }
    if (y[ct + i] - want1).abs() > 1.0e-4F {
      fail("sample 1 element \{i}: \{y[ct + i]}, expected \{want1}")
    }
    i = i + 1
  }
  // and the two samples are normalised identically despite their 100x offset:
  // that is exactly what a "global" norm means, and a per-channel mean
  // would not produce it.
  i = 0
  while i < ct {
    if (y[i] - y[ct + i]).abs() > 1.0e-4F {
      fail("samples differing by a constant 100 did not normalise identically at element \{i}")
    }
    i = i + 1
  }
}

test "gLN: gamma and beta are per CHANNEL, broadcast over time" {
  let c = 3
  let t = 5
  let n = 2
  let (p, x, _w) = gln_fixture(c, t, 0.1F, 41UL)
  if p.gamma.length() != c {
    fail("gamma has length \{p.gamma.length()}, expected c = \{c} -- a per-ELEMENT affine would be length c*t")
  }
  if p.beta.length() != c {
    fail("beta has length \{p.beta.length()}, expected c = \{c}")
  }
  let (y, _cache) = @snn_mbt.gln_forward(x, n, c, t, p)
  // The broadcast property, checked by perturbing ONE channel's gamma: every
  // frame of that channel must move, and no frame of any other channel may.
  // With a per-element affine, only the single (c, t) coordinate would move.
  let bumped = p.gamma.copy()
  let ch = 1
  bumped[ch] = p.gamma[ch] + 0.5F
  let bumped_p : @snn_mbt.GlobalLayerNorm = { gamma: bumped, beta: p.beta, eps: p.eps, c }
  let (y2, _c2) = @snn_mbt.gln_forward(x, n, c, t, bumped_p)
  let mut moved = 0
  let mut foreign = 0
  let mut i = 0
  while i < y.length() {
    let ch_of = (i / t) % c
    let sample = i / (c * t)
    let in_sample = sample * c * t + ch_of * t
    let is_target = (i >= in_sample) && (i < in_sample + t)
    if y2[i] != y[i] {
      moved = moved + 1
      if !is_target {
        foreign = foreign + 1
      }
    }
    i = i + 1
  }
  // each of the n samples contributes t frames of channel `ch`
  if moved != n * t {
    fail("perturbing gamma[\{ch}] moved \{moved} outputs, expected exactly \{n * t} (all frames of that channel)")
  }
  if foreign > 0 {
    fail("perturbing gamma[\{ch}] also moved \{foreign} outputs in OTHER channels -- the affine is per element, not per channel")
  }
}

test "gLN: d_input, d_gamma and d_beta all match central differences" {
  let c = 3
  let t = 4
  let n = 3
  let (p, x, w) = gln_fixture(c, t, 0.2F, 43UL)
  let (_y, cache) = @snn_mbt.gln_forward(x, n, c, t, p)
  let (d_x, d_gamma, d_beta) = @snn_mbt.gln_backward(w, cache, p)
  if d_gamma.length() != c || d_beta.length() != c || d_x.length() != x.length() {
    fail("gLN backward returned wrong shapes")
  }
  let f0 = gln_obj(x, n, c, t, p, w)
  let mut bad = 0
  let mut first_bad = -1
  // every one of the C gamma and C beta coordinates
  let mut k = 0
  while k < c {
    let gp = p.gamma.copy()
    let gm = p.gamma.copy()
    gp[k] = p.gamma[k] + GLN_H
    gm[k] = p.gamma[k] - GLN_H
    let up : @snn_mbt.GlobalLayerNorm = { gamma: gp, beta: p.beta, eps: p.eps, c }
    let dn : @snn_mbt.GlobalLayerNorm = { gamma: gm, beta: p.beta, eps: p.eps, c }
    let fd = (gln_obj(x, n, c, t, up, w) - gln_obj(x, n, c, t, dn, w)) /
      (2.0F * GLN_H)
    if (d_gamma[k] - fd).abs() > 1.0e-3F {
      bad = bad + 1
      if first_bad < 0 {
        first_bad = k
      }
    }
    let bp = p.beta.copy()
    let bm = p.beta.copy()
    bp[k] = p.beta[k] + GLN_H
    bm[k] = p.beta[k] - GLN_H
    let up2 : @snn_mbt.GlobalLayerNorm = { gamma: p.gamma, beta: bp, eps: p.eps, c }
    let dn2 : @snn_mbt.GlobalLayerNorm = { gamma: p.gamma, beta: bm, eps: p.eps, c }
    let fd2 = (gln_obj(x, n, c, t, up2, w) - gln_obj(x, n, c, t, dn2, w)) /
      (2.0F * GLN_H)
    if (d_beta[k] - fd2).abs() > 1.0e-3F {
      bad = bad + 1
      if first_bad < 0 {
        first_bad = c + k
      }
    }
    k = k + 1
  }
  // and a stride sample of the input gradient
  let stride = (x.length() / 40) + 1
  let mut i = 0
  while i < x.length() {
    let xp = x.copy()
    let xm = x.copy()
    xp[i] = x[i] + GLN_H
    xm[i] = x[i] - GLN_H
    let fd = (gln_obj(xp, n, c, t, p, w) - gln_obj(xm, n, c, t, p, w)) /
      (2.0F * GLN_H)
    let m = if d_x[i] < 0.0F { -d_x[i] } else { d_x[i] }
    if (d_x[i] - fd).abs() - 3.0e-2F * m - 1.0e-3F > 0.0F {
      bad = bad + 1
      if first_bad < 0 {
        first_bad = 1000 + i
      }
    }
    i = i + stride
  }
  if f0 == 0.0F {
    fail("the objective is identically zero -- the gate measured nothing")
  }
  if bad > 0 {
    fail("gLN: \{bad} gradients outside tolerance, first at \{first_bad}")
  }
}

test "gLN: eps is the reference's 1e-8, not this package's LayerNorm 1e-5" {
  let p = @snn_mbt.GlobalLayerNorm::new(2)
  if p.eps != 0.00000001F {
    fail("gLN eps is \{p.eps}, expected 1e-8 (asteroid's GlobLN default)")
  }
}