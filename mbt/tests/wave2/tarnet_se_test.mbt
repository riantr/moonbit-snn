// tarnet_se_test.mbt -- gradient gate for the channel-time squeeze-excitation
// gate, the paper's actual novelty.
//
// Reference: arXiv:2605.07735 (ICME 2026); `ChannelTimeSenseSELayer` in
// github.com/YassinTERRAF/TARNet, `tarnet/models/tarnet.py`.
//
// This gate exists because ASP's gradient was found wrong and CTSE lives in
// the same file with the same `[n, c, t]` layout convention, where two
// separate indexing bugs already hid. Every parameter class is probed: the
// three depthwise scale paths (weight and bias each), the Linear(3,1) over the
// scales, both fc pairs, and the input.

///|
/// Objective: the CTSE output weighted by a fixed vector, so every parameter
/// and every input coordinate has a non-zero gradient.
fn tn_obj_ctse(
  x : Array[Float],
  n : Int,
  p : @snn_mbt.CtseParam,
  w : Array[Float],
) -> Float {
  let (y, _c) = @snn_mbt.ctse_forward(x, n, p)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
///|
/// Lift `CtseParam`'s fc1 bias so the ReLU downstream of it is active.
/// Without this the gate is green precisely because it measured nothing: with
/// `b1 = 0` and tn_randn's small scale, `W1 * squeeze` is negative for every
/// (b, j) on the fixture, `h == 0` everywhere, `d_w1` and `d_b1` are
/// legitimately zero, and the kink filter skips every fc1 coordinate.
fn tn_ctse_awake(p : @snn_mbt.CtseParam, lift : Float) -> @snn_mbt.CtseParam {
  let b1 = p.b1.copy()
  let mut i = 0
  while i < b1.length() {
    b1[i] = b1[i] + lift
    i = i + 1
  }
  {
    small: p.small,
    mid: p.mid,
    large: p.large,
    fw: p.fw,
    fb: p.fb,
    w1: p.w1,
    b1: b1,
    w2: p.w2,
    b2: p.b2,
    channels: p.channels,
    c_red: p.c_red,
  }
}

///|
/// Rebuild `CtseParam` around one perturbed entry. `which` selects the slot:
///
///   0 small.weight   1 small.bias    2 mid.weight    3 mid.bias
///   4 large.weight  5 large.bias    6 fw[3]         7 fb
///   8 w1            9 b1            10 w2           11 b2
fn tn_ctse_bump(
  p : @snn_mbt.CtseParam,
  which : Int,
  idx : Int,
  delta : Float,
) -> @snn_mbt.CtseParam {
  let sw = p.small.weight.copy()
  let sb = p.small.bias.copy()
  let mw = p.mid.weight.copy()
  let mb = p.mid.bias.copy()
  let lw = p.large.weight.copy()
  let lb = p.large.bias.copy()
  let fw = p.fw.copy()
  let mut fb = p.fb
  let w1 = p.w1.copy()
  let b1 = p.b1.copy()
  let w2 = p.w2.copy()
  let b2 = p.b2.copy()
  if which == 0 {
    sw[idx] = sw[idx] + delta
  } else if which == 1 {
    sb[idx] = sb[idx] + delta
  } else if which == 2 {
    mw[idx] = mw[idx] + delta
  } else if which == 3 {
    mb[idx] = mb[idx] + delta
  } else if which == 4 {
    lw[idx] = lw[idx] + delta
  } else if which == 5 {
    lb[idx] = lb[idx] + delta
  } else if which == 6 {
    fw[idx] = fw[idx] + delta
  } else if which == 7 {
    fb = fb + delta
  } else if which == 8 {
    w1[idx] = w1[idx] + delta
  } else if which == 9 {
    b1[idx] = b1[idx] + delta
  } else if which == 10 {
    w2[idx] = w2[idx] + delta
  } else {
    b2[idx] = b2[idx] + delta
  }
  {
    small: tn_dw_with(p.small, sw, sb),
    mid: tn_dw_with(p.mid, mw, mb),
    large: tn_dw_with(p.large, lw, lb),
    fw: fw,
    fb: fb,
    w1: w1,
    b1: b1,
    w2: w2,
    b2: b2,
    channels: p.channels,
    c_red: p.c_red,
  }
}

///|
/// The analytic gradient for a `which` / `idx` pair, read out of `CtseGrad`.
fn tn_ctse_analytic(
  g : @snn_mbt.CtseGrad,
  which : Int,
  idx : Int,
) -> Float {
  if which == 0 {
    g.d_small[idx]
  } else if which == 1 {
    g.db_small[idx]
  } else if which == 2 {
    g.d_mid[idx]
  } else if which == 3 {
    g.db_mid[idx]
  } else if which == 4 {
    g.d_large[idx]
  } else if which == 5 {
    g.db_large[idx]
  } else if which == 6 {
    g.d_fw[idx]
  } else if which == 7 {
    g.d_fb
  } else if which == 8 {
    g.d_w1[idx]
  } else if which == 9 {
    g.d_b1[idx]
  } else if which == 10 {
    g.d_w2[idx]
  } else {
    g.d_b2[idx]
  }
}

///|
/// Length of the array slot `which` selects.
fn tn_ctse_len(p : @snn_mbt.CtseParam, which : Int) -> Int {
  if which == 0 {
    p.small.weight.length()
  } else if which == 1 {
    p.small.bias.length()
  } else if which == 2 {
    p.mid.weight.length()
  } else if which == 3 {
    p.mid.bias.length()
  } else if which == 4 {
    p.large.weight.length()
  } else if which == 5 {
    p.large.bias.length()
  } else if which == 6 {
    p.fw.length()
  } else if which == 7 {
    1
  } else if which == 8 {
    p.w1.length()
  } else if which == 9 {
    p.b1.length()
  } else if which == 10 {
    p.w2.length()
  } else {
    p.b2.length()
  }
}

///|
test "channel-time SE: every parameter class matches central differences" {
  let n = 3
  let c = 4
  let t = 6
  let x = tn_fixture(n, c, t, 67UL)
  let p = tn_ctse_awake(@snn_mbt.CtseParam::new(c, 71UL), 0.5F)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(73UL), n * c * t)
  // forward sanity: the gate is a per-channel multiplier, so the output keeps
  // the input's shape
  let (y, cache) = @snn_mbt.ctse_forward(x, n, p)
  if y.length() != x.length() {
    fail("ctse changed the length: \{x.length()} -> \{y.length()}")
  }
  let g = @snn_mbt.ctse_backward(w, n, p, cache)
  // ---- input gradient, every coordinate ----
  let mut i = 0
  while i < x.length() {
    let xu = x.copy()
    let xd = x.copy()
    xu[i] = x[i] + TN_H
    xd[i] = x[i] - TN_H
    let fd = (tn_obj_ctse(xu, n, p, w) - tn_obj_ctse(xd, n, p, w)) /
      (2.0F * TN_H)
    let gap = tn_gap(fd, g.d_x[i])
    if gap > 0.0F {
      fail("ctse d_x[\{i}]: fd=\{fd} analytic=\{g.d_x[i]} gap=\{gap}")
    }
    i = i + 1
  }
  // ---- every parameter class ----
  let mut kinks = 0
  let mut checked = 0
  let mut which = 0
  while which < 12 {
    let cnt = tn_ctse_len(p, which)
    // a spread of entries, not all of them
    let stride = (cnt / 5) + 1
    let mut idx = 0
    while idx < cnt {
      // One-sided slopes, not a central difference. A central difference at a
      // kink returns the average of the two branches, and when the kink sits
      // exactly at the base point that average is the SAME for every step size
      // -- so comparing two step sizes does not reveal it. The one-sided pair
      // does: at a smooth point both tend to f', at a kink they differ.
      let f_base = tn_obj_ctse(x, n, p, w)
      let f_up = tn_obj_ctse(x, n, tn_ctse_bump(p, which, idx, TN_H), w)
      let f_dn = tn_obj_ctse(x, n, tn_ctse_bump(p, which, idx, -TN_H), w)
      let d_pos = (f_up - f_base) / TN_H
      let d_neg = (f_base - f_dn) / TN_H
      let split = if d_pos > d_neg { d_pos - d_neg } else { d_neg - d_pos }
      if split > 5.0e-3F {
        kinks = kinks + 1
        idx = idx + stride
        continue
      }
      let fd = (f_up - f_dn) / (2.0F * TN_H)
      checked = checked + 1
      let an = tn_ctse_analytic(g, which, idx)
      let gap = tn_gap(fd, an)
      if gap > 0.0F {
        fail("ctse slot=\{which} idx=\{idx}: fd=\{fd} analytic=\{an} gap=\{gap}")
      }
      idx = idx + stride
    }
    which = which + 1
  }
  if checked < 5 {
    fail("only \{checked} smooth coordinates checked and \{kinks} kinks skipped -- the filter is not discriminating")
  }
}


///|
/// Objective over the FLATTENED parameters, so a whole direction can be
/// perturbed at once.
fn tn_obj_ctse_flat(
  x : Array[Float],
  n : Int,
  p : @snn_mbt.CtseParam,
  flat : Array[Float],
  w : Array[Float],
) -> Float {
  tn_obj_ctse(x, n, @snn_mbt.ctse_unflatten(p, flat), w)
}

///|
/// Directional derivative check over the whole parameter vector.
///
/// This is the instrument the per-coordinate sweep could not be. It averages
/// over every coordinate, so it is not defeated by individually small
/// gradients -- `d_w1` and `d_b1` included, which on a small fixture are
/// individually near zero but collectively carry signal.
test "channel-time SE: directional derivative over all parameters" {
  let n = 3
  let c = 4
  let t = 6
  let x = tn_fixture(n, c, t, 67UL)
  let p = tn_ctse_awake(@snn_mbt.CtseParam::new(c, 71UL), 0.5F)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(73UL), n * c * t)
  let (_y, cache) = @snn_mbt.ctse_forward(x, n, p)
  let g = @snn_mbt.ctse_backward(w, n, p, cache)
  let flat = @snn_mbt.ctse_flatten(p)
  let gf = @snn_mbt.ctse_grad_flatten(g)
  if flat.length() != gf.length() {
    fail("flat param length \{flat.length()} != flat grad length \{gf.length()}")
  }
  // three independent directions
  let mut d = 0
  while d < 3 {
    let u = @snn_mbt.tn_randn(gf.length(), 1.0F, @snn_mbt.Xoshiro::new(101UL + d.to_uint64()))
    let up = flat.copy()
    let dn = flat.copy()
    let mut an = 0.0F
    let mut i = 0
    while i < flat.length() {
      up[i] = flat[i] + TN_H * u[i]
      dn[i] = flat[i] - TN_H * u[i]
      an = an + gf[i] * u[i]
      i = i + 1
    }
    let fd = (tn_obj_ctse_flat(x, n, p, up, w) -
      tn_obj_ctse_flat(x, n, p, dn, w)) / (2.0F * TN_H)
    let rel = if an < 0.0F { -an } else { an }
    let err = if fd > an { fd - an } else { an - fd }
    let scale = if rel < 1.0e-4F { 1.0e-4F } else { rel }
    if err / scale > 3.0e-2F {
      fail("directional fd=\{fd} analytic=\{an} (direction \{d})")
    }
    d = d + 1
  }
}

///|
/// Flatten / unflatten round-trips, and a perturbed vector really lands in
/// the parameters it claims to.
test "ctse flatten round-trips and the layout is the documented one" {
  let c = 4
  let p = @snn_mbt.CtseParam::new(c, 97UL)
  let flat = @snn_mbt.ctse_flatten(p)
  if flat.length() != @snn_mbt.ctse_param_count(p) {
    fail("flatten length does not match ctse_param_count")
  }
  let back = @snn_mbt.ctse_flatten(@snn_mbt.ctse_unflatten(p, flat))
  for i in 0..<flat.length() {
    if flat[i] != back[i] {
      fail("round trip differs at \{i}")
    }
  }
  // the layout is bias-free arrays then fb, so a bump at each documented
  // offset must reach that field
  let bumped = flat.copy()
  bumped[0] = bumped[0] + 1.0F
  let q = @snn_mbt.ctse_unflatten(p, bumped)
  if q.small.weight[0] != flat[0] + 1.0F {
    fail("offset 0 is not small.weight[0]")
  }
  let at_fb = p.fw.length() + p.small.bias.length() + p.mid.bias.length() +
    p.large.bias.length() + p.small.weight.length() + p.mid.weight.length() +
    p.large.weight.length()
  let fb = flat.copy()
  fb[at_fb] = fb[at_fb] + 1.0F
  let q2 = @snn_mbt.ctse_unflatten(p, fb)
  if q2.fb != p.fb + 1.0F {
    fail("the documented fb offset does not land on fb")
  }
}
