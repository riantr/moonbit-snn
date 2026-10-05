// zz_gnn_train_runner.mbt -- end-to-end training demo (v0.155.0,
// depth study added in v0.156.0).
//
// WHY IT LIVES HERE: see the header of ../verify_gnn_grads.ps1. A fresh
// sub-package cannot resolve any symbol from the parent module in moon
// 0.1.20260920, and `moon test` cannot link with 998 .mbt files on a
// 32K Windows command line, so the demo runs inside the library package
// behind the same temporary `is-main` flip the gradient gate uses.
//
// Every headline number is a HELD-OUT accuracy and is printed next to
// the control that would have caught its being fake:
//
//   real labels     -> held-out accuracy must beat the no-message-
//                       passing bar
//   shuffled labels -> held-out accuracy must fall far below the real
//                       run, by a stated margin
//   no message pass -> the bar itself, trained on raw features
//
// The fixture separability row is the one that cannot be argued with: it
// involves no model, only the sign of the single class-carrying feature
// before and after a neighbourhood average.

///| One line per checkpoint of a training run.
fn zz_curve_row(
  name : String,
  losses : Array[Float],
  train_accs : Array[Float],
  test_accs : Array[Float],
  marks : Array[Int],
) -> Unit {
  let mut line = "  " + name + "  "
  for i in 0..<marks.length() {
    let k = marks[i]
    line = line + "[" + k.to_string() + "] L=" +
      Float::to_string(losses[k]) +
      " tr=" + Float::to_string(train_accs[k]) +
      " te=" + Float::to_string(test_accs[k]) + "  "
  }
  println(line)
}

///| Did the loss fall AND did the model beat the no-message-passing bar?
///
/// The bar is the held-out accuracy of a trained `GraphLinear` on the
/// RAW features, plus a margin. Three earlier versions of this criterion
/// were wrong in instructive ways, and all three would have been easy to
/// ship:
///
///   * "held-out accuracy must RISE" is UNSATISFIABLE for GCN. GCN's
///     forward pass IS a neighbourhood average, so an untrained GCN
///     already sits on smoothed features: it started at 83% held-out and
///     stayed there for 300 steps while its training loss fell 16x. The
///     model is fine; the untrained forward had already done the work
///     training would otherwise have done.
///   * "held-out accuracy must reach the neighbourhood-averaged oracle
///     - 0.10" is unreachable BY CONSTRUCTION. That oracle is a sign
///     rule on one hand-averaged feature -- a 1-parameter decision on a
///     1-dimensional projection with no ReLU anywhere. A single-layer
///     backbone applies a ReLU to its output BEFORE the readout, which
///     discards half the embedding, so it is strictly weaker than that
///     oracle no matter how long it trains.
///   * "held-out accuracy must not move" flagged 4 of 5 architectures on
///     shuffled labels, all of them merely CONVERGING TO CHANCE.
///
/// So the bar is a comparison between two things this demo measured and
/// trained itself, with a margin wide enough to mean something (0.10 on
/// 24 held-out nodes is 2.4 nodes). The oracle is still reported, as the
/// reason the fixture is in this regime -- explicitly labelled an
/// oracle, not a target.
fn zz_learned(
  losses : Array[Float],
  test_final : Float,
  bar : Float,
) -> Bool {
  let n = losses.length()
  if n < 2 {
    return false
  }
  losses[n - 1] < losses[0] && test_final > bar
}

///| Is a held-out accuracy meaningfully ABOVE chance for this fixture?
///
/// Chance is 0.5 because the classes are balanced. This is a sanity
/// marker on the control, not the control: the control's assertion is the
/// gap against the real-label run, which needs no binomial cut.
fn zz_above_chance(acc : Float) -> Bool {
  acc > 0.75F
}

///| Train one configuration of five backbones and report all three
/// criteria. Returns `(learned, final_test_accuracies)`.
fn zz_run_set(
  label : String,
  names : Array[String],
  nets : Array[TrainableGraphNet],
  graphs : Array[Graph],
  h : Array[Float],
  train : Array[Bool],
  classes : Int,
  steps : Int,
  lr : Float,
  bar : Float,
  shuffled : Array[Int],
  marks : Array[Int],
  out : Array[Float],
) -> Int {
  let n = names.length()
  println("--- " + label + ": loss must fall, HELD-OUT acc must beat " +
    Float::to_string(bar) + " ---")
  let mut learned = 0
  for i in 0..<n {
    let net = nets[i]
    let g = graphs[i]
    let head = gnn_train_head(net, classes, 2024UL)
    let (_, _, losses, tr_accs, te_accs) = gnn_train_curve(
      net, head, g, h, gnn_train_labels(), train, classes, steps, lr,
    )
    out[i] = te_accs[te_accs.length() - 1]
    zz_curve_row(names[i], losses, tr_accs, te_accs, marks)
    if zz_learned(losses, out[i], bar) {
      learned = learned + 1
    } else {
      println(
        "  ^^ " + names[i] + " held out " + Float::to_string(out[i]) +
        " vs bar " + Float::to_string(bar),
      )
    }
  }
  println("  learned: " + learned.to_string() + "/" + n.to_string())
  println("--- " + label +
    ": negative control, shuffled labels must fall far below ---")
  let mut fake = 0
  for i in 0..<n {
    let net = nets[i]
    let g = graphs[i]
    let head = gnn_train_head(net, classes, 2024UL)
    let (_, _, sl, tr, te) = gnn_train_curve(
      net, head, g, h, shuffled, train, classes, steps, lr,
    )
    zz_curve_row(names[i] + "/shuf", sl, tr, te, marks)
    // Training loss falling here is EXPECTED and is memorisation of the
    // training points, not a bug. What must not happen is the held-out
    // number coming near the real-label run.
    let shuf_final = te[te.length() - 1]
    if shuf_final >= out[i] - 0.2F {
      fake = fake + 1
      println(
        "  ^^ " + names[i] + " held-out " + Float::to_string(shuf_final) +
        " vs real " + Float::to_string(out[i]) +
        " -- gap too small to distinguish a signal from noise",
      )
    } else {
      println(
        "    " + names[i] + " gap=" + Float::to_string(out[i] - shuf_final) +
        "  (real " + Float::to_string(out[i]) +
        " vs shuffled " + Float::to_string(shuf_final) + ")",
      )
    }
    if zz_above_chance(shuf_final) {
      println(
        "  !! " + names[i] + " shuffled held-out " +
        Float::to_string(shuf_final) + " is above the 0.75 chance band",
      )
    }
  }
  println(
    "  indistinguishable from noise: " + fake.to_string() +
    "/" + n.to_string() + " (must be 0)",
  )
  learned - fake
}

fn main {
  let raw = gnn_train_graph()
  let train = gnn_train_mask()
  let shuffled = gnn_train_shuffled_labels()
  let h = raw.features
  let emb_dim = 8
  let hidden = 16
  let classes = 2
  let steps = 300
  let lr = 0.05F
  let marks = [0, 5, 50, 300]
  let names = ["GCN", "GIN", "MPNN", "PNA", "GAT"]
  let (nets1, graphs1) = gnn_train_backbones(emb_dim, 2024UL)
  let (nets2, graphs2) = gnn_train_backbones_2layer(emb_dim, hidden, 2024UL)
  println("=== end-to-end training demo: 2 communities x " +
    gnn_train_per_community().to_string() + " nodes, 4 feats ===")
  println("--- fixture ---")
  let mut n_train = 0
  let mut n_hold = 0
  for v in 0..<gnn_train_n() {
    if train[v] {
      n_train = n_train + 1
    } else {
      n_hold = n_hold + 1
    }
  }
  println(
    "  graph: " + raw.n_nodes.to_string() + " nodes, " +
    raw.n_edges.to_string() + " edges (2 communities of " +
    gnn_train_per_community().to_string() + ", degree " +
    gnn_train_in_degree().to_string() + ", 2 bridges)",
  )
  println(
    "  split: " + n_train.to_string() + " train / " +
    n_hold.to_string() + " held out (transductive: held-out nodes keep" +
    " their edges and features, only their labels are hidden)",
  )
  println(
    "  steps=" + steps.to_string() + "  lr=" + Float::to_string(lr) +
    "  embedding=" + emb_dim.to_string() + "  objective = mean" +
    " cross-entropy over TRAIN nodes, backbone + GraphLinear head",
  )
  println("--- 0. is the graph even necessary? (baselines, no GNN) ---")
  let (raw_sep, smooth_sep) = gnn_train_fixture_separability()
  let (probe_te, probe_tr) = gnn_linear_probe_curve(
    raw, h, gnn_train_labels(), train, classes, steps, lr, 2024UL,
  )
  let probe_final = probe_te[probe_te.length() - 1]
  let bar = probe_final + 0.10F
  println(
    "  [oracle] sign of the class feature alone        : " +
    Float::to_string(raw_sep),
  )
  println(
    "  [oracle] same sign after a neighbourhood average: " +
    Float::to_string(smooth_sep),
  )
  println(
    "    (oracle = a sign rule on one hand-averaged feature. It says" +
    " the fixture lives in the regime where homophily helps, which is" +
    " why the noise width is 4.4x the signal. It is NOT a target: a" +
    " single-layer backbone ReLUs its embedding before the readout," +
    " so it is strictly weaker than a 1-parameter sign rule.)",
  )
  let mut pline = "  [bar] LinearProbe on raw features, held out  "
  for i in 0..<marks.length() {
    let k = marks[i]
    pline = pline + "[" + k.to_string() + "] tr=" +
      Float::to_string(probe_tr[k]) +
      " te=" + Float::to_string(probe_te[k]) + "  "
  }
  println(pline)
  println(
    "  [bar] held-out " + Float::to_string(probe_te[0]) + " -> " +
    Float::to_string(probe_final) +
    "  =>  every net must beat " + Float::to_string(bar) + " held out",
  )
  let te1 : Array[Float] = Array::make(5, 0.0F)
  let score1 = zz_run_set(
    "1-layer", names, nets1, graphs1, h, train, classes, steps, lr,
    bar, shuffled, marks, te1,
  )
  let te2 : Array[Float] = Array::make(5, 0.0F)
  let score2 = zz_run_set(
    "2-layer (hidden " + hidden.to_string() + ")", names, nets2, graphs2, h,
    train, classes, steps, lr, bar, shuffled, marks, te2,
  )
  println("--- summary: does depth move the held-out number? ---")
  let mut deeper = 0
  let mut beat = 0
  let mut line = "  arch   1L held-out   2L held-out   delta"
  println(line)
  for i in 0..<5 {
    let d = te2[i] - te1[i]
    if d > 0.0F {
      deeper = deeper + 1
    }
    if te2[i] > probe_final {
      beat = beat + 1
    }
    line = "  " + names[i] + "     " + Float::to_string(te1[i]) +
      "        " + Float::to_string(te2[i]) + "      " +
      Float::to_string(d)
    println(line)
  }
  println(
    "  2L beat the probe: " + beat.to_string() +
    "/5   2L better than 1L: " + deeper.to_string() + "/5",
  )
  println("")
  let ok = score1 == 5 && score2 == 5 && beat == 5
  if ok {
    println(
      "VERDICT: PASS -- both depths learn 5/5, 0/5 generalise on noise," +
      " 5/5 beat the probe at each depth",
    )
  } else {
    println(
      "VERDICT: FAIL (1L score=" + score1.to_string() +
      "  2L score=" + score2.to_string() +
      "  2L-beat-probe=" + beat.to_string() + ")",
    )
  }
}
