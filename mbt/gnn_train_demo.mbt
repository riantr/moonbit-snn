// gnn_train_demo.mbt -- end-to-end node-classification training
// (v0.155.0).
//
// Batches Y through AB shipped five backward passes and spent the last
// of them proving they were CORRECT. Correctness is not the same
// property as usefulness, though: a gradient can agree with a finite
// difference to seven digits and still leave a model that does not
// learn, because the composition can be right while the SIGNALS are
// wrong -- a head wired to the wrong half of a layer, a loss with the
// wrong sign, a learning rate that scales the update wrongly, a label
// convention inverted against the features. No gradient check sees any
// of that. This file is the check that does.
//
// WHAT IT FOUND ON ITS FIRST RUN. `graph_cross_entropy` had an
// inverted sign on its `logf(sum_exp)` term, so it returned a NEGATIVE
// loss for every confident and correct prediction. Its gradient was
// correct, so training worked and accuracy rose -- only the reported
// loss moved the wrong way. The gradient gate could not have caught it,
// because the gate deliberately checks a bounded quadratic objective
// instead of cross-entropy. A gate that exercises one objective cannot
// see a bug in another; this file is the second objective, and
// `gradcheck_ce_consistency` in gnn_gradcheck.mbt now pins the loss
// against its own gradient so the pair cannot drift apart again.
//
// The composite step is backbone -> node embeddings -> GraphLinear head
// -> node cross-entropy, not "backbone output is the logits". The
// reason is in `graph_net_train_step`'s own doc: GCN/GIN/MPNN apply
// their ReLU on EVERY layer including the last, so their raw output is
// non-negative by construction. Reading ReLU output as logits gives a
// classifier that can only ever push a class UP and never down, which
// still trains and still reports a falling loss -- so the demo would
// look fine while measuring a handicapped model. GAT is the opposite
// (its final layer is linear), which is exactly the kind of
// inconsistency a reader would otherwise have to discover by comparing
// five result rows. A head makes all five comparable, and it is also
// what you would actually put on a GNN.
//
// THE FIXTURE IS NOT THE GRADIENT-CHECK FIXTURE, and that is
// deliberate. `gradcheck_toy_graph` splits its labels by ring parity so
// that NEIGHBOURING NODES DISAGREE -- an anti-homophilous graph is
// exactly right for a gradient check, because a wrong mean aggregator
// then looks wrong instead of accidentally right. A training demo needs
// the opposite property: message passing can only work on a homophilous
// graph, so on the gradient-check fixture no amount of correct gradient
// would make any of these nets learn. One fixture cannot be both.
//
// WHY THERE ARE CONTROLS, AND WHY THEY USE HELD-OUT NODES. "Loss fell
// and accuracy rose" is a claim about a metric, and a metric that rises
// for free proves nothing. Two things make such a claim free:
//
//   1. MEMORISATION. Twenty-four training points are memorisable by a
//      network with thousands of parameters, so a training-set accuracy
//      of 100% distinguishes nothing. Every headline number here is a
//      HELD-OUT accuracy, and the control below judges held-out
//      accuracy too.
//   2. A TRIVIALLY SEPARABLE FIXTURE. The first version of this file
//      used per-node noise of half-width 0.15 against a signal of 0.15,
//      which puts them at exactly the threshold where the sign of the
//      class-carrying feature is NEVER wrong. Every architecture, and a
//      bare linear probe, hit 100% before a single training step. Half-
//      width 0.60 against signal 0.30 puts the noise at twice the
//      signal, which is the regime homophily actually helps in, and
//      `gnn_train_fixture_separability` measures the resulting gap
//      with NO TRAINING AT ALL so the claim rests on the data rather
//      than on any model's good behaviour.

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

///|
/// Fraction of SELECTED nodes whose argmax logit equals their label.
/// `train` is a per-node mask; a node with `train[v] == false` is
/// skipped entirely. Ties break to the lowest class index, so this is
/// deterministic.
pub fn node_mask_accuracy(
  logits : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  n_nodes : Int,
  num_classes : Int,
) -> Float {
  if n_nodes == 0 || num_classes == 0 {
    return 0.0F
  }
  let mut hit = 0
  let mut seen = 0
  for v in 0..<n_nodes {
    if v < train.length() && !train[v] {
      continue
    }
    let off = v * num_classes
    let mut best = 0
    let mut best_val = if logits.length() > off { logits[off] } else { 0.0F }
    for k in 1..<num_classes {
      let idx = off + k
      let val = if logits.length() > idx { logits[idx] } else { 0.0F }
      if val > best_val {
        best_val = val
        best = k
      }
    }
    let y = if v < labels.length() { labels[v] } else { 0 }
    if best == y {
      hit = hit + 1
    }
    seen = seen + 1
  }
  if seen == 0 {
    return 0.0F
  }
  Float::from_int(hit) / Float::from_int(seen)
}

///|
/// Fraction of ALL nodes whose argmax logit equals their label.
pub fn graph_class_accuracy(
  logits : Array[Float],
  labels : Array[Int],
  n_nodes : Int,
  num_classes : Int,
) -> Float {
  let all : Array[Bool] = Array::make(n_nodes, true)
  node_mask_accuracy(logits, labels, all, n_nodes, num_classes)
}

///|
/// Mean cross-entropy over the SELECTED nodes, matching
/// `node_mask_accuracy`'s mask convention.
pub fn node_mask_ce_loss(
  logits : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  n_nodes : Int,
  num_classes : Int,
) -> Float {
  if n_nodes == 0 {
    return 0.0F
  }
  let mut total = 0.0F
  let mut seen = 0
  for v in 0..<n_nodes {
    if v < train.length() && !train[v] {
      continue
    }
    let off = v * num_classes
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    for k in 0..<num_classes {
      row[k] = logits[off + k]
    }
    let y = if v < labels.length() { labels[v] } else { 0 }
    total = total + graph_cross_entropy(row, y)
    seen = seen + 1
  }
  if seen == 0 {
    return 0.0F
  }
  total / Float::from_int(seen)
}

///|
/// d/d(logits) of `node_mask_ce_loss`. Exactly zero on unselected nodes:
/// they receive no loss, so they must receive no gradient, and anything
/// else would train the backbone on held-out nodes through the back
/// door -- which would quietly invalidate the whole held-out claim.
pub fn node_mask_ce_grad(
  logits : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  n_nodes : Int,
  num_classes : Int,
) -> Array[Float] {
  let out : Array[Float] = Array::make(n_nodes * num_classes, 0.0F)
  if n_nodes == 0 {
    return out
  }
  let mut seen = 0
  for v in 0..<n_nodes {
    if v < train.length() && train[v] {
      seen = seen + 1
    }
  }
  if seen == 0 {
    return out
  }
  let inv = 1.0F / Float::from_int(seen)
  for v in 0..<n_nodes {
    if v < train.length() && !train[v] {
      continue
    }
    let off = v * num_classes
    let row : Array[Float] = Array::make(num_classes, 0.0F)
    for k in 0..<num_classes {
      row[k] = logits[off + k]
    }
    let y = if v < labels.length() { labels[v] } else { 0 }
    let grow = graph_cross_entropy_grad(row, y)
    for k in 0..<num_classes {
      out[off + k] = grow[k] * inv
    }
  }
  out
}

// ---------------------------------------------------------------------------
// The training fixture: two communities, weak features, held-out split
// ---------------------------------------------------------------------------

///|
/// Nodes in the demo fixture.
pub fn gnn_train_n() -> Int {
  60
}

///|
/// Nodes per community.
pub fn gnn_train_per_community() -> Int {
  30
}

///|
/// In-degree WITHIN a community: a circulant graph, each node joined to
/// the next four. This is the one fixture parameter that was not free.
///
/// The first version used two 20-cliques, i.e. in-degree 19. That is
/// not a realistic message-passing graph (Cora averages about 3) and it
/// broke GIN specifically: GIN's aggregator is an UNNORMALISED SUM, so
/// an embedding is ~19 times a single node's projection, the head
/// saturates, the cross-entropy underflows to ~5e-4 within five steps and
/// the gradient vanishes. GIN reached 75% held-out while the homophily
/// ceiling was 95%, and no learning rate fixes it -- the scale is set by
/// the degree. Degree 4 keeps every architecture in a regime where its
/// own aggregation is the thing being tested.
///
/// The circulant is generated by stride, not by an RNG, so the fixture's
/// STRUCTURE is byte-identical too and only the features carry noise.
pub fn gnn_train_in_degree() -> Int {
  4
}

///|
/// Training nodes per community (the rest are held out).
pub fn gnn_train_per_community_train() -> Int {
  18
}

///|
/// The training mask for `gnn_train_graph`. Transductive node
/// classification, the Cora setup: the held-out nodes stay IN the graph
/// and keep their edges and features, because a message-passing model
/// is supposed to use a node's neighbours whatever their labels are.
/// Only the LABELS are held out.
pub fn gnn_train_mask() -> Array[Bool] {
  let n = gnn_train_n()
  let per = gnn_train_per_community()
  let n_train = gnn_train_per_community_train()
  let mask : Array[Bool] = Array::make(n, false)
  for v in 0..<n {
    let pos = v % per
    mask[v] = pos < n_train
  }
  mask
}

///|
/// The demo graph: two 30-node communities, each a degree-4 circulant,
/// joined by two cross edges; 4 input features per node, UNWEIGHTED.
/// GCN needs the normalised version -- `gnn_train_backbones` returns
/// the right graph for each.
///
/// Only feature dimension 0 carries any class information (a community
/// signal of +/-0.90); the other three are pure noise, so nothing can be
/// won by keying on them. The per-node noise is uniform with a total
/// width of 4.0, i.e. 4.4 times the signal, which puts the raw features
/// just above chance: a sign test on dimension 0 alone lands near 0.72.
/// Averaging over a node and its 4 same-community neighbours keeps the
/// signal and shrinks the noise by sqrt(5), which puts the neighbourhood
/// average near 1.0. That gap is the homophily the demo is built on.
///
/// That ratio took three attempts, and both failures were instructive:
/// noise width 0.30 against signal 0.15 left the sign of the
/// class-carrying feature NEVER wrong, so every architecture and a bare
/// linear probe hit 100% before training started. Width 1.2 against
/// 0.30 fixed that but only reached 0.825 raw, and an UNTRAINED GCN
/// then started at 93.75% held-out -- "accuracy must rise" became
/// unsatisfiable, because the model had nothing left to learn.
/// `gnn_train_fixture_separability` reports both numbers on every run
/// precisely so this is visible rather than guessed.
///
/// The noise comes from a fixed-seed Xoshiro, so the fixture is
/// byte-identical on every run and every platform. A demo whose
/// baseline moves between runs is not a demo.
pub fn gnn_train_graph() -> Graph {
  let n = gnn_train_n()
  let per = gnn_train_per_community()
  let deg = gnn_train_in_degree()
  let feat_dim = 4
  // Each community contributes per*deg edges; 2 cross edges on top.
  let n_edges = 2 * per * deg + 2
  let s : Array[Int] = Array::make(n_edges, 0)
  let d : Array[Int] = Array::make(n_edges, 0)
  let mut k = 0
  for base in 0..<2 {
    let off = base * per
    for i in 0..<per {
      for j in 1..<(deg + 1) {
        s[k] = off + i
        d[k] = off + (i + j) % per
        k = k + 1
      }
    }
  }
  // Two weak bridges. Without them the task is disconnected and a
  // one-hop net would never have to propagate anything, which is the
  // part being demonstrated.
  s[k] = 0
  d[k] = per
  k = k + 1
  s[k] = 1
  d[k] = per + 1
  k = k + 1
  let rng = Xoshiro::from_state(11UL, 22UL, 33UL, 44UL)
  let feats : Array[Float] = Array::make(n * feat_dim, 0.0F)
  for v in 0..<n {
    let sign = if v < per { 1.0F } else { 0.0F - 1.0F }
    for c in 0..<feat_dim {
      let signal = if c == 0 { 0.90F * sign } else { 0.0F }
      feats[v * feat_dim + c] = signal + (next_f32(rng) - 0.5F) * 4.0F
    }
  }
  Graph::new(n, feat_dim, feats, s, d, Array::make(0, 0.0F))
}

///|
/// Community labels for `gnn_train_graph`: 0 for the first community,
/// 1 for the second.
pub fn gnn_train_labels() -> Array[Int] {
  let n = gnn_train_n()
  let per = gnn_train_per_community()
  let out : Array[Int] = Array::make(n, 0)
  for v in 0..<n {
    out[v] = if v < per { 0 } else { 1 }
  }
  out
}

///|
/// The negative control's labels: the true labels under a FIXED
/// permutation that preserves the class balance while making every
/// clique internally MIXED.
///
/// The permutation walks one community at double stride and then the
/// other, so consecutive nodes -- which is exactly how the cliques are
/// built -- get alternating labels. Class counts are untouched, so the
/// control cannot be dismissed as "the classes were unbalanced".
///
/// On this fixture the control is only meaningful on the HELD-OUT
/// nodes. A network with thousands of parameters will drive its
/// TRAINING loss to zero on 24 memorisable points, and treating that as
/// "it fitted noise" would flag correct behaviour. What must not happen
/// is held-out accuracy rising, because once the labels are scrambled
/// there is nothing in the graph that identifies a node's community.
pub fn gnn_train_shuffled_labels() -> Array[Int] {
  let n = gnn_train_n()
  let per = gnn_train_per_community()
  let truth = gnn_train_labels()
  let out : Array[Int] = Array::make(n, 0)
  for v in 0..<n {
    let pos = v % per
    // 2*pos walks community 0 then community 1; skipping by 2 lands
    // the two classes on alternating nodes of each clique.
    let src = if v < per { 2 * pos } else { per + 2 * pos + 1 }
    out[v] = truth[src % n]
  }
  out
}

///|
/// How separable the fixture is WITHOUT any message passing, as a
/// property of the DATA rather than of a trained model.
///
/// Returns `(feature_only, neighbourhood_averaged)`: the accuracy of the
/// sign of the one class-carrying feature, and the accuracy of the sign
/// of that feature averaged over the node and its in-neighbours.
///
/// This is the control that cannot be gamed. A trained probe on 24
/// points memorises them whatever they look like, so its accuracy says
/// nothing about whether the graph helps; these two numbers involve no
/// training at all, and the gap between them IS the homophily the demo
/// is built on. The trained probe is still reported, but only as the
/// thing this pair predicts should be beaten.
pub fn gnn_train_fixture_separability() -> (Float, Float) {
  let g = gnn_train_graph()
  let labels = gnn_train_labels()
  let n = g.n_nodes
  let mut raw_hit = 0
  let mut smooth_hit = 0
  for v in 0..<n {
    let y = labels[v]
    let x0 = g.features[v * 4]
    let pred_raw = if x0 >= 0.0F { 0 } else { 1 }
    // Mean over in-neighbours plus the node itself: what a mean
    // aggregator computes, on the one feature that carries the class.
    let mut acc = x0
    let mut cnt = 1.0F
    for e in 0..<g.n_edges {
      if g.edge_dst[e] != v {
        continue
      }
      acc = acc + g.features[g.edge_src[e] * 4]
      cnt = cnt + 1.0F
    }
    let mean = acc / cnt
    let pred_smooth = if mean >= 0.0F { 0 } else { 1 }
    if pred_raw == y {
      raw_hit = raw_hit + 1
    }
    if pred_smooth == y {
      smooth_hit = smooth_hit + 1
    }
  }
  (
    Float::from_int(raw_hit) / Float::from_int(n),
    Float::from_int(smooth_hit) / Float::from_int(n),
  )
}

///|
/// Build the classification head for a backbone: a `GraphLinear` from
/// the backbone's node-embedding width to `num_classes`.
///
/// The head's `in_dim` is read from `trainable_net_node_dim` rather
/// than assumed, because for GAT that value is `num_heads` times the
/// last per-head width. A head sized from anything else still builds,
/// still trains, and silently reads the wrong slice of every node row.
pub fn gnn_train_head(
  net : TrainableGraphNet,
  num_classes : Int,
  seed : UInt64,
) -> GraphLinear {
  GraphLinear::new(trainable_net_node_dim(net), num_classes, seed + 7777UL)
}

// ---------------------------------------------------------------------------
// The composite training step
// ---------------------------------------------------------------------------

///|
/// One full supervised step: backbone forward, head forward, mean
/// cross-entropy over the TRAINING nodes, backward through the head,
/// backward through the backbone, SGD on both. Returns
/// `(new_net, new_head, loss, train_acc, test_acc)`.
///
/// `loss` and the accuracies are the values at the CURRENT parameters,
/// i.e. BEFORE this step's update. Recording them before rather than
/// after is what makes "the loss fell" a measurement of the initial
/// model instead of a number that starts one step in.
pub fn gnn_train_step(
  net : TrainableGraphNet,
  head : GraphLinear,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  num_classes : Int,
  lr : Float,
) -> (TrainableGraphNet, GraphLinear, Float, Float, Float) {
  let n = g.n_nodes
  let emb = trainable_net_forward(net, g, h)
  let logits = graph_linear_forward(head, emb, n)
  let loss = node_mask_ce_loss(logits, labels, train, n, num_classes)
  let train_acc = node_mask_accuracy(logits, labels, train, n, num_classes)
  let held : Array[Bool] = Array::make(n, true)
  let test_acc = node_mask_accuracy(
    logits, labels, invert_mask(held, train), n, num_classes,
  )
  let d_logits = node_mask_ce_grad(logits, labels, train, n, num_classes)
  // Backward through the head first: the backbone's gradient IS the
  // head's input gradient, so this is the join between the two halves.
  let (d_emb, head_grad) = graph_linear_backward(head, d_logits, n, emb)
  let (net2, _) = trainable_net_sgd_step(net, g, h, d_emb, lr)
  let head2 = graph_linear_sgd_step(head, head_grad, lr)
  (net2, head2, loss, train_acc, test_acc)
}

///|
/// helper: the complement of a per-node mask, with the same length.
fn invert_mask(all : Array[Bool], train : Array[Bool]) -> Array[Bool] {
  let out : Array[Bool] = Array::make(all.length(), true)
  for v in 0..<all.length() {
    out[v] = if v < train.length() { !train[v] } else { true }
  }
  out
}

///|
/// Run `steps` supervised steps and record the loss, training accuracy
/// and HELD-OUT accuracy at every parameter setting, INCLUDING the
/// untrained one. All three arrays have length `steps + 1`, so index 0
/// is the baseline.
///
/// Returns `(new_net, new_head, losses, train_accs, test_accs)`.
pub fn gnn_train_curve(
  net : TrainableGraphNet,
  head : GraphLinear,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  num_classes : Int,
  steps : Int,
  lr : Float,
) -> (TrainableGraphNet, GraphLinear, Array[Float], Array[Float], Array[Float]) {
  let n = g.n_nodes
  let held = invert_mask(Array::make(n, true), train)
  let losses : Array[Float] = Array::make(steps + 1, 0.0F)
  let train_accs : Array[Float] = Array::make(steps + 1, 0.0F)
  let test_accs : Array[Float] = Array::make(steps + 1, 0.0F)
  let mut cur = net
  let mut cur_head = head
  let mut i = 0
  while i <= steps {
    let emb = trainable_net_forward(cur, g, h)
    let logits = graph_linear_forward(cur_head, emb, n)
    losses[i] = node_mask_ce_loss(logits, labels, train, n, num_classes)
    train_accs[i] = node_mask_accuracy(logits, labels, train, n, num_classes)
    test_accs[i] = node_mask_accuracy(logits, labels, held, n, num_classes)
    if i < steps {
      let (n2, h2, _, _, _) = gnn_train_step(
        cur, cur_head, g, h, labels, train, num_classes, lr,
      )
      cur = n2
      cur_head = h2
    }
    i = i + 1
  }
  (cur, cur_head, losses, train_accs, test_accs)
}

// ---------------------------------------------------------------------------
// Baseline: the same head with no message passing
// ---------------------------------------------------------------------------

///|
/// The no-message-passing baseline: a bare `GraphLinear` trained on the
/// RAW features with the same objective, learning rate and step count as
/// the GNNs, scored on the HELD-OUT nodes exactly as they are.
///
/// Returns `(held_out_accuracies, training_accuracies)`, both of length
/// `steps + 1`.
///
/// Scored held-out on purpose. Trained on 24 points this model
/// memorises them, so its TRAINING accuracy says nothing; the
/// held-out number is the one `gnn_train_fixture_separability` predicts
/// should be beaten, and on this fixture the prediction is that it
/// should NOT be.
pub fn gnn_linear_probe_curve(
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  train : Array[Bool],
  num_classes : Int,
  steps : Int,
  lr : Float,
  seed : UInt64,
) -> (Array[Float], Array[Float]) {
  let n = g.n_nodes
  let held = invert_mask(Array::make(n, true), train)
  let head = GraphLinear::new(g.feat_dim, num_classes, seed + 9999UL)
  let test_accs : Array[Float] = Array::make(steps + 1, 0.0F)
  let train_accs : Array[Float] = Array::make(steps + 1, 0.0F)
  let mut cur = head
  let mut i = 0
  while i <= steps {
    let logits = graph_linear_forward(cur, h, n)
    test_accs[i] = node_mask_accuracy(logits, labels, held, n, num_classes)
    train_accs[i] = node_mask_accuracy(logits, labels, train, n, num_classes)
    if i < steps {
      let d = node_mask_ce_grad(logits, labels, train, n, num_classes)
      let (_, grad) = graph_linear_backward(cur, d, n, h)
      cur = graph_linear_sgd_step(cur, grad, lr)
    }
    i = i + 1
  }
  (test_accs, train_accs)
}

///|
/// Convenience: the five trainable backbones at a given embedding
/// width, with the graph each one needs. GAT uses TWO heads of width
/// `emb_dim / 2` so the multi-head stack -- concat, shared-input sum,
/// ELU mask -- is what actually trains here, not a degenerate one-head
/// configuration.
///
/// The GCN entry is built on the NORMALISED graph; the rest take the raw
/// graph, since `gcn_sgd_step` reads `edge_weight` and an unweighted
/// edge list is a legal but different model.
pub fn gnn_train_backbones(
  emb_dim : Int,
  seed : UInt64,
) -> (Array[TrainableGraphNet], Array[Graph]) {
  let raw = gnn_train_graph()
  let norm = normalise_adjacency(raw)
  let gcn = GCN::new([4, emb_dim], 0.0F, seed)
  let gin = GIN::new([4, emb_dim], 0.0F, seed)
  let mpnn = MPnn::new([4, emb_dim], seed)
  let pna = PNA::new(
    [4, emb_dim], emb_dim, 0.001F, PNAScaler::Amplification, seed,
  )
  let heads = 2
  let per_head = emb_dim / heads
  let gat = GraphAttention::new([4, per_head], heads, seed)
  let nets : Array[TrainableGraphNet] = Array::make(5, TrainableGraphNet::Gin(gin))
  nets[0] = TrainableGraphNet::Gcn(gcn)
  nets[1] = TrainableGraphNet::Gin(gin)
  nets[2] = TrainableGraphNet::Mpnn(mpnn)
  nets[3] = TrainableGraphNet::Pna(pna)
  nets[4] = TrainableGraphNet::Gat(gat)
  let graphs : Array[Graph] = Array::make(5, raw)
  graphs[0] = norm
  (nets, graphs)
}
