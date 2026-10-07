// The post-MFA SE gate lives in the same file as CTSE and follows the same
// `[n, c, t]` convention, so it gets its own gradient gate rather than being
// assumed correct because the CTSE next to it is.
//
// Two failure modes are guarded explicitly:
//   1. `se_forward` is now a thin wrapper over `se_forward_full`, so the
//      wrapper must stay bit-identical to the full form.
//   2. `SeParam::new` leaves b1 at zero and tn_randn's scale is 0.05, so on a
//      small fixture `W1 * squeeze` is negative for every (b, j), `h == 0`
//      everywhere, and `d_w1`/`d_b1` are legitimately zero. The gate would be
//      green precisely because it measured nothing. `tn_se_awake` lifts b1.

///|
/// Objective: the SE output weighted by a fixed vector, so every parameter and
/// every input coordinate has a non-zero gradient.
fn tn_obj_se(
  x : Array[Float],
  n : Int,
  p : @snn_mbt.SeParam,
  w : Array[Float],
) -> Float {
  let y = @snn_mbt.se_forward(x, n, p)
  let mut s = 0.0F
  let mut i = 0
  while i < y.length() {
    s = s + y[i] * w[i]
    i = i + 1
  }
  s
}

///|
/// Step for the SE sweep. Same reasoning as the block's TN_H: the objective is
/// O(1..10) and the signal per step must stay well above `ulp(f)`.
const SE_H : Float = 4.0e-3F

///|
/// One-sided-slope disagreement above which the coordinate is treated as
/// straddling a kink. Derived from the objective's own float32 precision, NOT
/// normalised by the gradient magnitude -- that would make the criterion
/// permissive exactly when the gradient is large.
fn tn_se_split_tol(f_base : Float) -> Float {
  let a = if f_base < 0.0F { -f_base } else { f_base }
  20.0F * a * 6.0e-8F / SE_H
}

fn tn_se_atol() -> Float {
  2.0e-3F
}

fn tn_se_rtol() -> Float {
  3.0e-2F
}

///|
/// Rebuild an `SeParam` whose gate is actually LOADED, so the squeeze path is
/// measurable at all.
///
/// `SeParam::new` leaves b1 at zero and scales w1/w2 by 0.05. On the untouched
/// fixture that makes the module near-inert, and the gate is green because it
/// measured nothing:
///   - with `b1 = 0`, `W1 * squeeze` is negative for every (b, j), `h == 0`,
///     and `d_w1`/`d_b1` are legitimately zero;
///   - and the squeeze's own share of `d_x` is ~0.25% of the direct
///     `d_out * gate` path, which is under the absolute tolerance. Verified:
///     deleting the pooling path entirely left the `d_x` gate green at 90/90.
///
/// `lift` wakes the bottleneck ReLU; `gain` raises the gate's authority without
/// saturating it -- b2 stays 0 and gain is small enough that the pre-sigmoid
/// stays near 0, so `gate` sits around 0.5-0.7 and `gate*(1-gate)` does not
/// collapse. Measured share of the squeeze path after this fixture: ~13% of
/// each `d_x` coordinate, against a 3% relative tolerance.
fn tn_se_awake(p : @snn_mbt.SeParam, lift : Float, gain : Float) -> @snn_mbt.SeParam {
  let b1 = p.b1.copy()
  let mut i = 0
  while i < b1.length() {
    b1[i] = b1[i] + lift
    i = i + 1
  }
  let w1 = p.w1.copy()
  let w2 = p.w2.copy()
  i = 0
  while i < w1.length() {
    w1[i] = w1[i] * gain
    i = i + 1
  }
  i = 0
  while i < w2.length() {
    w2[i] = w2[i] * gain
    i = i + 1
  }
  { w1, b1, w2, b2: p.b2, channels: p.channels, bottleneck: p.bottleneck }
}

///|
/// Flat parameter view in a fixed order: w1, b1, w2, b2. The rebuild uses
/// EXPLICIT offsets rather than a captured mutable cursor, because an
/// off-by-one in the cursor would silently mis-pair every gradient with the
/// wrong parameter -- the exact bug class this file already caught twice.
fn tn_se_flat(p : @snn_mbt.SeParam) -> Array[Float] {
  let c = p.channels
  let bk = p.bottleneck
  let n1 = bk * c
  let n2 = bk
  let n3 = c * bk
  let n4 = c
  let f : Array[Float] = Array::make(n1 + n2 + n3 + n4, 0.0F)
  let mut o = 0
  o = tn_se_put(f, o, p.w1)
  o = tn_se_put(f, o, p.b1)
  o = tn_se_put(f, o, p.w2)
  o = tn_se_put(f, o, p.b2)
  f
}

fn tn_se_put(dst : Array[Float], off : Int, src : Array[Float]) -> Int {
  let mut i = 0
  while i < src.length() {
    dst[off + i] = src[i]
    i = i + 1
  }
  off + src.length()
}

fn tn_se_unflat(p : @snn_mbt.SeParam, f : Array[Float]) -> @snn_mbt.SeParam {
  let c = p.channels
  let bk = p.bottleneck
  let w1 = tn_se_take(f, 0, bk * c)
  let b1 = tn_se_take(f, bk * c, bk)
  let w2 = tn_se_take(f, bk * c + bk, c * bk)
  let b2 = tn_se_take(f, bk * c + bk + c * bk, c)
  { w1, b1, w2, b2, channels: c, bottleneck: bk }
}

fn tn_se_take(f : Array[Float], off : Int, len : Int) -> Array[Float] {
  let a : Array[Float] = Array::make(len, 0.0F)
  let mut i = 0
  while i < len {
    a[i] = f[off + i]
    i = i + 1
  }
  a
}

///|
/// `SeGrad` -> flat, in the same order as `tn_se_flat`.
fn tn_se_grad_flat(g : @snn_mbt.SeGrad, p : @snn_mbt.SeParam) -> Array[Float] {
  let c = p.channels
  let bk = p.bottleneck
  let f : Array[Float] = Array::make(bk * c + bk + c * bk + c, 0.0F)
  let mut o = 0
  o = tn_se_put(f, o, g.w1)
  o = tn_se_put(f, o, g.b1)
  o = tn_se_put(f, o, g.w2)
  o = tn_se_put(f, o, g.b2)
  f
}

test "SE: the full and thin forward are bit-identical" {
  let n = 3
  let c = 6
  let t = 5
  let p = @snn_mbt.SeParam::new(c, 4, 91UL)
  let x = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(92UL), n * c * t)
  let a = @snn_mbt.se_forward(x, n, p)
  let (b, _cache) = @snn_mbt.se_forward_full(x, n, p)
  let mut i = 0
  while i < a.length() {
    if a[i] != b[i] {
      fail("se_forward and se_forward_full disagree at \{i}")
    }
    i = i + 1
  }
}

test "SE: every parameter class matches central differences" {
  let n = 3
  let c = 6
  let t = 5
  let proto = tn_se_awake(@snn_mbt.SeParam::new(c, 4, 93UL), 0.5F, 4.0F)
  let x = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(94UL), n * c * t)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(95UL), n * c * t)
  let (_y, cache) = @snn_mbt.se_forward_full(x, n, proto)
  let g = @snn_mbt.se_backward(x, w, n, proto, cache)
  let gf = tn_se_grad_flat(g, proto)
  let base = tn_se_flat(proto)
  let f_base = tn_obj_se(x, n, proto, w)
  let tol_split = tn_se_split_tol(f_base)
  // guard against the exact degenerate case this gate exists to avoid: if the
  // bottleneck never woke up, d_w1 and d_b1 are zero and half the sweep is a
  // no-op that reports success.
  let mut awake = 0
  let bk = proto.bottleneck
  let mut j = 0
  while j < bk {
    if proto.b1[j] > 0.0F {
      awake = awake + 1
    }
    j = j + 1
  }
  if awake != bk {
    fail("fc1 bias was not lifted on all \{bk} units -- d_w1/d_b1 would be measured as zero")
  }
  let mut checked = 0
  let mut kinks = 0
  let mut bad = 0
  let mut worst_k = -1
  let mut worst = 0.0F
  let mut k = 0
  while k < base.length() {
    let bu = base.copy()
    let bd = base.copy()
    bu[k] = base[k] + SE_H
    bd[k] = base[k] - SE_H
    let fu = tn_obj_se(x, n, tn_se_unflat(proto, bu), w)
    let fm = tn_obj_se(x, n, tn_se_unflat(proto, bd), w)
    let d_pos = (fu - f_base) / SE_H
    let d_neg = (f_base - fm) / SE_H
    let split = if d_pos > d_neg { d_pos - d_neg } else { d_neg - d_pos }
    if split > tol_split {
      kinks = kinks + 1
    } else {
      checked = checked + 1
      let fd = (fu - fm) / (2.0F * SE_H)
      let an = gf[k]
      let d = fd - an
      let ad = if d < 0.0F { -d } else { d }
      let m = if an < 0.0F { -an } else { an }
      if ad - tn_se_rtol() * m - tn_se_atol() > 0.0F {
        bad = bad + 1
        if ad > worst {
          worst = ad
          worst_k = k
        }
      }
    }
    k = k + 1
  }
  println("SE sweep: checked=\{checked} kinks=\{kinks} bad=\{bad}")
  if checked < 20 {
    fail("only \{checked} of \{base.length()} coordinates were smooth enough to check")
  }
  if bad > 0 {
    fail("SE parameters: \{bad} of \{checked} outside tolerance, worst \{worst} at \{worst_k}")
  }
}

test "SE: the input gradient matches central differences" {
  let n = 3
  let c = 6
  let t = 5
  let p = tn_se_awake(@snn_mbt.SeParam::new(c, 4, 97UL), 0.5F, 4.0F)
  let x = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(98UL), n * c * t)
  let w = @snn_mbt.rand_f32(@snn_mbt.Xoshiro::new(99UL), n * c * t)
  let (_y, cache) = @snn_mbt.se_forward_full(x, n, p)
  let g = @snn_mbt.se_backward(x, w, n, p, cache)
  let f_base = tn_obj_se(x, n, p, w)
  let tol_split = tn_se_split_tol(f_base)
  let mut checked = 0
  let mut kinks = 0
  let mut bad = 0
  let mut worst_k = -1
  let mut worst = 0.0F
  let mut i = 0
  while i < x.length() {
    let xu = x.copy()
    let xd = x.copy()
    xu[i] = x[i] + SE_H
    xd[i] = x[i] - SE_H
    let fu = tn_obj_se(xu, n, p, w)
    let fm = tn_obj_se(xd, n, p, w)
    let d_pos = (fu - f_base) / SE_H
    let d_neg = (f_base - fm) / SE_H
    let split = if d_pos > d_neg { d_pos - d_neg } else { d_neg - d_pos }
    if split > tol_split {
      kinks = kinks + 1
    } else {
      checked = checked + 1
      let fd = (fu - fm) / (2.0F * SE_H)
      let an = g.d_x[i]
      let d = fd - an
      let ad = if d < 0.0F { -d } else { d }
      let m = if an < 0.0F { -an } else { an }
      if ad - tn_se_rtol() * m - tn_se_atol() > 0.0F {
        bad = bad + 1
        if ad > worst {
          worst = ad
          worst_k = i
        }
      }
    }
    i = i + 1
  }
  println("SE d_x sweep: checked=\{checked} kinks=\{kinks} bad=\{bad}")
  if checked < 20 {
    fail("only \{checked} of \{x.length()} input coordinates were smooth enough to check")
  }
  if bad > 0 {
    fail("SE input gradient: \{bad} of \{checked} outside tolerance, worst \{worst} at \{worst_k}")
  }
}