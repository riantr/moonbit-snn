// graph_classifier.mbt -- Graph-level readout and classification (v0.140.0).
//
// Node-level GNNs produce one embedding per node, but graph-level
// tasks (molecule property prediction, graph classification) need a
// single vector per graph. A READOUT function pools the node
// embeddings into a graph embedding.
//
// Three readouts ship here (all from the GIN / GraphCL line of work):
//
//   mean_pool   h_g = (1/n) sum_v h_v
//   sum_pool    h_g = sum_v h_v
//   max_pool    h_g = max_v h_v
//
// plus an MLP classification head on top.
//
// Scope of v0.140.0:
//   - mean_pool / sum_pool / max_pool graph readouts.
//   - GraphClassifier: a GCN or MPNN backbone + readout + MLP head.
//   - graph_classifier_forward: graph -> class logits.
//   - graph_classifier_predict: argmax.
//   - cross_entropy_on_graphs + accuracy helper.
//
// Reference: Xu et al. 2019 (GIN); You et al. 2020 (GraphCL).

///|
/// Mean readout: average of all node embeddings.
pub fn mean_pool(h : Array[Float], n_nodes : Int, dim : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(dim, 0.0F)
  if n_nodes == 0 {
    return out
  }
  for v in 0..<n_nodes {
    let off = v * dim
    for k in 0..<dim {
      out[k] = out[k] + h[off + k]
    }
  }
  let inv = 1.0F / Float::from_int(n_nodes)
  for k in 0..<dim {
    out[k] = out[k] * inv
  }
  out
}

///|
/// Sum readout.
pub fn sum_pool(h : Array[Float], n_nodes : Int, dim : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(dim, 0.0F)
  for v in 0..<n_nodes {
    let off = v * dim
    for k in 0..<dim {
      out[k] = out[k] + h[off + k]
    }
  }
  out
}

///|
/// Max readout. With n_nodes == 0 the result is all-zero (the neutral
/// element of max), which keeps the head bias as the only signal.
pub fn max_pool(h : Array[Float], n_nodes : Int, dim : Int) -> Array[Float] {
  let out : Array[Float] = Array::make(dim, 0.0F)
  if n_nodes == 0 {
    return out
  }
  for k in 0..<dim {
    let mut best = h[k]
    for v in 1..<n_nodes {
      let x = h[v * dim + k]
      if x > best {
        best = x
      }
    }
    out[k] = best
  }
  out
}

///|
/// Which readout a GraphClassifier uses.
pub(all) enum GraphReadout {
  Mean
  Sum
  Max
} derive(Eq, Debug)

///|
/// GraphClassifier: a message-passing backbone, a graph readout, and
/// an MLP classification head.
pub struct GraphClassifier {
  gcn : GCN
  readout : GraphReadout
  // Head: Linear(node_dim -> hidden) + ReLU + Linear(hidden -> classes).
  head1 : GraphLinear
  head2 : GraphLinear
  node_dim : Int
  hidden_dim : Int
  num_classes : Int
}

///|
/// Build a GraphClassifier. `gcn` must output `node_dim`-wide node
/// embeddings (i.e. its last GCN layer has out_dim == node_dim).
pub fn GraphClassifier::new(
  gcn : GCN,
  readout : GraphReadout,
  num_classes : Int,
  hidden_dim : Int,
  seed : UInt64,
) -> GraphClassifier {
  let node_dim = gcn.out_dim
  {
    gcn,
    readout,
    head1: GraphLinear::new(node_dim, hidden_dim, seed + 70UL),
    head2: GraphLinear::new(hidden_dim, num_classes, seed + 80UL),
    node_dim,
    hidden_dim,
    num_classes,
  }
}

///|
/// Forward: a single graph -> class logits [num_classes].
/// `g` must already carry the normalised adjacency weights.
pub fn graph_classifier_forward(
  clf : GraphClassifier,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  // 1. Message passing over the graph.
  let node_emb = gcn_forward(clf.gcn, g, h)
  // 2. Readout: pool node embeddings into a graph embedding.
  let graph_emb = match clf.readout {
    Mean => mean_pool(node_emb, g.n_nodes, clf.node_dim)
    Sum => sum_pool(node_emb, g.n_nodes, clf.node_dim)
    Max => max_pool(node_emb, g.n_nodes, clf.node_dim)
  }
  // 3. MLP head.
  let hid : Array[Float] = Array::make(1 * clf.hidden_dim, 0.0F)
  let pre = graph_linear_forward(clf.head1, graph_emb, 1)
  for i in 0..<clf.hidden_dim {
    hid[i] = if pre[i] > 0.0F { pre[i] } else { 0.0F }
  }
  graph_linear_forward(clf.head2, hid, 1)
}

///|
/// Argmax over the class logits.
pub fn graph_classifier_predict(
  clf : GraphClassifier,
  g : Graph,
  h : Array[Float],
) -> Int {
  let logits = graph_classifier_forward(clf, g, h)
  let mut best = 0
  let mut best_val = logits[0]
  for k in 1..<clf.num_classes {
    if logits[k] > best_val {
      best_val = logits[k]
      best = k
    }
  }
  best
}

///|
/// Cross-entropy loss on one graph.
///
///   -log softmax(logits)[target]
///     = -(logits[p] - m) + log(sum_exp)          (max-subtracted)
///     =  m - logits[p] + logf(sum_exp)
///
/// The `logf` term is ADDED. It was SUBTRACTED until v0.155.0, which
/// made this return a negative number for every confident and correct
/// prediction: with `p` the argmax, `sum_exp` is just above 1, so
/// `logf(sum_exp)` is a small positive number and the whole expression
/// came out as minus it. The magnitude was right and only the sign was
/// wrong, which is the worst possible shape for this bug:
///
///   * `graph_cross_entropy_grad` was (and still is) the correct
///     `softmax - onehot`, so TRAINING WORKED. Accuracy went up, the
///     optimiser's direction was right.
///   * The REPORTED loss therefore moved the wrong way, and a loop
///     that early-stops, schedules, or simply asserts "the loss fell"
///     on this function was reading a number with the wrong sign.
///
/// It survived v0.149.0 - v0.154.0 because the gradient gate
/// deliberately checks a bounded QUADRATIC objective instead of
/// cross-entropy (CE's loss grows with the logit scale, which made an
/// absolute tolerance meaningless). A gate that exercises one
/// objective cannot see a bug in another. The end-to-end training demo
/// found this in a single run because it is the first thing in the
/// package to actually TRAIN on this function, and
/// `gradcheck_ce_consistency` in gnn_gradcheck.mbt now pins the loss
/// against its own gradient by finite difference so it cannot come back.
pub fn graph_cross_entropy(logits : Array[Float], target : Int) -> Float {
  let n = logits.length()
  if n == 0 {
    return 0.0F
  }
  let mut m = logits[0]
  for i in 1..<n {
    if logits[i] > m {
      m = logits[i]
    }
  }
  let mut sum_exp = 0.0F
  for i in 0..<n {
    sum_exp = sum_exp + expf(logits[i] - m)
  }
  let p = if target >= 0 && target < n { target } else { 0 }
  m - logits[p] + logf(sum_exp)
}

///|
/// Mean cross-entropy over a list of (graph, features) pairs.
pub fn graph_batch_loss(
  clf : GraphClassifier,
  graphs : Array[Graph],
  feats : Array[Array[Float]],
  targets : Array[Int],
) -> Float {
  let n = graphs.length()
  if n == 0 {
    return 0.0F
  }
  let mut total = 0.0F
  for i in 0..<n {
    let logits = graph_classifier_forward(clf, graphs[i], feats[i])
    total = total + graph_cross_entropy(logits, targets[i])
  }
  total / Float::from_int(n)
}

///|
/// Classification accuracy over a list of (graph, features) pairs.
pub fn graph_batch_accuracy(
  clf : GraphClassifier,
  graphs : Array[Graph],
  feats : Array[Array[Float]],
  targets : Array[Int],
) -> Float {
  let n = graphs.length()
  if n == 0 {
    return 0.0F
  }
  let mut correct = 0.0F
  for i in 0..<n {
    let pred = graph_classifier_predict(clf, graphs[i], feats[i])
    if pred == targets[i] {
      correct = correct + 1.0F
    }
  }
  correct / Float::from_int(n)
}

///|
/// Per-node accuracy for node-level classification. `node_emb` is the
/// [n_nodes x out_dim] output of the backbone, `pred` the predicted
/// class per node, and `labels` the ground truth per node.
pub fn node_accuracy(
  pred : Array[Int],
  labels : Array[Int],
  n_nodes : Int,
) -> Float {
  if n_nodes == 0 {
    return 0.0F
  }
  let mut correct = 0.0F
  for i in 0..<n_nodes {
    if pred[i] == labels[i] {
      correct = correct + 1.0F
    }
  }
  correct / Float::from_int(n_nodes)
}