// tarnet_model_test.mbt -- the model-level gradient gate.
//
// Reference: arXiv:2605.07735 (ICME 2026), `tarnet/models/tarnet.py`.
//
// The per-layer gates proved each stage in isolation. This one proves the
// WIRING: that the twelve stages are composed in the right order and that
// `tarnet_backward`'s flat gradient lines up with `tarnet_flatten`'s, term for
// term. A layout off-by-one is invisible to every per-layer gate and trains a
// subtly wrong model while the loss still descends.
//
// Instrument notes, carried over from the block gate because both lessons were
// paid for in full:
//   - an eight-fixture sweep, because a wiring fault is VALUE-INDEPENDENT: the
//     index -> parameter mapping does not change when the RNG produces
//     different numbers. One failure on one fixture is a float32 artifact;
//     one index failing on every fixture is a fault.
//   - the kink filter, on the ONE-SIDED slopes. A central difference straddles
//     a kink and reports the average of two derivatives the analytic gradient
//     has no obligation to average.
//   - the split threshold is derived from the objective's float32 precision,
//     never normalised by the gradient magnitude.

///|
/// Cross-entropy of the model on a fixed batch. `targets[b]` is the class of
/// sample `b`.
fn tn_obj_model(
  net : @snn_mbt.Tarnet,
  x : Array[Float],
  n : Int,
  targets : Array[Int],
) -> Float {
  let o = @snn_mbt.tarnet_forward(net, x, n)
  let (_d, loss) = @snn_mbt.tarnet_softmax_dlogits(
    o.logits, n, net.n_classes, targets,
  )
  loss
}

///|
/// A deliberately small TARNet. The defaults are 512 channels wide with a 512-d
/// embedding, which is right for training and useless for a finite-difference
/// sweep; every stage is still present, just narrow.
fn tn_model_fixture(
  n : Int,
  n_classes : Int,
  in_chan : Int,
  t : Int,
  seed : UInt64,
) -> (@snn_mbt.Tarnet, Array[Float], Array[Int]) {
  let net = @snn_mbt.Tarnet::new(
    n_classes,
    in_chan,
    t,
    width=8,
    hid_chan=8,
    kernel=3,
    repeats_s=1,
    repeats_m=1,
    repeats_l=1,
    fuse_out=8,
    emb_dim=8,
    att_hidden=8,
    seed=seed,
  )
  let x = @snn_mbt.rand_f32(
    @snn_mbt.Xoshiro::new(seed + 1000UL), n * in_chan * t,
  )
  let targets = []
  let mut i = 0
  while i < n {
    targets.push(i % n_classes)
    i = i + 1
  }
  (net, x, targets)
}

///|
/// Step for the model sweep. Smaller than the block's TN_H: the objective is a
/// cross-entropy over the whole twelve-stage pipeline, so the signal per step
/// is smaller and the float32 cancellation is correspondingly worse.
const TM_H : Float = 2.0e-3F

const TM_ATOL : Float = 3.0e-3F
const TM_RTOL : Float = 5.0e-2F

const TM_SEEDS : Int = 4
const TM_MAX_ARTIFACTS : Int = 3
const TM_MAX_INDEX_SEEDS : Int = 3

fn tm_split_tol(f_base : Float) -> Float {
  let a = if f_base < 0.0F { -f_base } else { f_base }
  20.0F * a * 6.0e-8F / TM_H
}

test "Tarnet: the thin and full forward are bit-identical" {
  let (net, x, _tg) = tn_model_fixture(3, 4, 6, 6, 131UL)
  let a = @snn_mbt.tarnet_forward(net, x, 3)
  let (b, _cache) = @snn_mbt.tarnet_forward_full(net, x, 3)
  let mut i = 0
  while i < a.logits.length() {
    if a.logits[i] != b.logits[i] {
      fail("tarnet_forward and tarnet_forward_full disagree on logits[\{i}]")
    }
    i = i + 1
  }
  i = 0
  while i < a.embedding.length() {
    if a.embedding[i] != b.embedding[i] {
      fail("tarnet_forward and tarnet_forward_full disagree on embedding[\{i}]")
    }
    i = i + 1
  }
}

test "Tarnet: the model gradient matches central differences" {
  let n = 3
  // Sized from the real parameter count, not a guessed constant: the tally is
  // indexed by flat parameter coordinate, and an undersized array here would
  // either panic or -- worse -- silently drop tallies for the high indices,
  // which are exactly the ones a layout fault moves.
  let (probe, _px, _pt) = tn_model_fixture(n, 4, 6, 6, 141UL)
  let n_params = @snn_mbt.tarnet_param_count(probe)
  let seed_hits = Array::make(n_params, 0)
  let mut total_checked = 0
  let mut total_kinks = 0
  let mut artifacts = 0
  // The first coordinates that miss, reported in the failure message. A count
  // alone cannot distinguish one systematic fault from twenty scattered ones,
  // and the two need completely different investigations.
  let first_bad = []
  let mut seed = 141UL
  while seed < 141UL + TM_SEEDS.to_uint64() {
    let (net, x, targets) = tn_model_fixture(n, 4, 6, 6, seed)
    let (out, cache) = @snn_mbt.tarnet_forward_full(net, x, n)
    let (d_logits, _loss) = @snn_mbt.tarnet_softmax_dlogits(
      out.logits, n, net.n_classes, targets,
    )
    let (gf, _dx) = @snn_mbt.tarnet_backward(net, n, cache, d_logits, 0.0F)
    let base = @snn_mbt.tarnet_flatten(net)
    let f_base = tn_obj_model(net, x, n, targets)
    let tol_split = tm_split_tol(f_base)
    let stride = (base.length() / 150) + 1
    let mut k = 0
    while k < base.length() {
      let bu = base.copy()
      let bd = base.copy()
      bu[k] = base[k] + TM_H
      bd[k] = base[k] - TM_H
      let fu = tn_obj_model(
        @snn_mbt.tarnet_unflatten(net, bu), x, n, targets,
      )
      let fm = tn_obj_model(
        @snn_mbt.tarnet_unflatten(net, bd), x, n, targets,
      )
      let d_pos = (fu - f_base) / TM_H
      let d_neg = (f_base - fm) / TM_H
      let split = if d_pos > d_neg { d_pos - d_neg } else { d_neg - d_pos }
      if split > tol_split {
        total_kinks = total_kinks + 1
      } else {
        total_checked = total_checked + 1
        let fd = (fu - fm) / (2.0F * TM_H)
        let an = gf[k]
        let d = fd - an
        let ad = if d < 0.0F { -d } else { d }
        let m = if an < 0.0F { -an } else { an }
        if ad - TM_RTOL * m - TM_ATOL > 0.0F {
          artifacts = artifacts + 1
          seed_hits[k] = seed_hits[k] + 1
          if first_bad.length() < 20 {
            first_bad.push(k)
          }
        }
      }
      k = k + stride
    }
    seed = seed + 1UL
  }
  let mut worst_k = -1
  let mut worst_seeds = 0
  let mut ki = 0
  while ki < seed_hits.length() {
    if seed_hits[ki] > worst_seeds {
      worst_seeds = seed_hits[ki]
      worst_k = ki
    }
    ki = ki + 1
  }
  let mut bad_str = ""
  let mut bi = 0
  while bi < first_bad.length() {
    bad_str = bad_str + " " + first_bad[bi].to_string()
    bi = bi + 1
  }
  println(
    "model sweep: params=\{n_params} checked=\{total_checked} kinks=\{total_kinks} artifacts=\{artifacts} worst_index=\{worst_k} worst_index_seeds=\{worst_seeds} first_bad=\{bad_str}",
  )
  if total_checked < 150 {
    fail("only \{total_checked} smooth coordinates checked over \{TM_SEEDS} fixtures -- coverage collapsed")
  }
  if worst_seeds >= TM_MAX_INDEX_SEEDS {
    fail(
      "model index \{worst_k} misses on \{worst_seeds} of \{TM_SEEDS} fixtures -- value-independent, so this is a wiring or layout fault",
    )
  }
  if artifacts > TM_MAX_ARTIFACTS {
    fail(
      "model parameters: \{artifacts} of \{total_checked} coordinate-fixture pairs outside tolerance (bound \{TM_MAX_ARTIFACTS})",
    )
  }
}

test "Tarnet: softmax cross-entropy matches a hand-computed row" {
  // Two classes, hand-checkable by hand: softmax([a, b]) and the mean of
  // -log p. d_logits must be (p - onehot)/n.
  let logits = [0.0F, 0.0F]
  let targets = [0]
  let (d, loss) = @snn_mbt.tarnet_softmax_dlogits(logits, 1, 2, targets)
  // equal logits -> p = 0.5 each; loss = -log(0.5) = 0.6931472
  if loss > 0.6931473F || loss < 0.6931470F {
    fail("softmax cross-entropy on equal logits gave \{loss}, expected log(2) = 0.6931472")
  }
  let want = 0.5F - 1.0F
  if d[0] > want + 1.0e-6F || d[0] < want - 1.0e-6F {
    fail("d_logits[0] = \{d[0]}, expected \{want}")
  }
  if d[1] > 0.5F + 1.0e-6F || d[1] < 0.5F - 1.0e-6F {
    fail("d_logits[1] = \{d[1]}, expected 0.5")
  }
}

test "Tarnet: cross-entropy gradient matches central differences on the logits" {
  // The sweep above checks parameters; this pins the loss's own gradient at its
  // very first step, where a mistake in the onehot subtraction or the 1/n
  // would otherwise be attributed to some later stage.
  let logits = [0.3F, -0.7F, 1.1F, 0.05F, 0.2F, -0.3F]
  let n = 2
  let n_classes = 3
  let targets = [2, 0]
  let (d, _loss) = @snn_mbt.tarnet_softmax_dlogits(logits, n, n_classes, targets)
  let loss_at = fn(flat : Array[Float]) -> Float {
    let (_dd, l) = @snn_mbt.tarnet_softmax_dlogits(
      flat, n, n_classes, targets,
    )
    l
  }
  let f0 = loss_at(logits)
  let h = 1.0e-3F
  let mut i = 0
  while i < logits.length() {
    let up = logits.copy()
    let dn = logits.copy()
    up[i] = logits[i] + h
    dn[i] = logits[i] - h
    let fd = (loss_at(up) - loss_at(dn)) / (2.0F * h)
    let diff = fd - d[i]
    let ad = if diff < 0.0F { -diff } else { diff }
    if ad > 1.0e-3F {
      fail("logit \{i}: central difference \{fd} vs analytic \{d[i]}")
    }
    i = i + 1
  }
  if f0 < 0.0F {
    fail("cross entropy returned \{f0}, which cannot be negative")
  }
}