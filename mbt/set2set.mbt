// set2set.mbt -- Set2Set attention readout + attention-pooling
// graph classifier (v0.143.0).
//
// Reference: Vinyals, Keras, Bengio 2016, "Set2Set: Learning from
// Sets", ICLR 2017; Lee et al. 2019, "A Set-to-Set Approach to
// Multi-Class Molecular Graph Classification" (PNA paper's
// companion result for readout).
//
// The readouts in v0.140.0 (mean / sum / max) are PERMUTATION
// INVARIANT: they collapse the whole node set to one vector with a
// fixed reduction, so any reordering of the nodes is invisible. That
// is a virtue for bags of tokens and a limitation for molecular
// graphs, where the *identity* of a specific substructure matters --
// "there exists a nitrogen bonded to a carbon bonded to oxygen" is a
// statement about one element of the set, and mean-pooling cannot
// express it.
//
// Set2Set replaces the single reduction with `processing_steps` rounds
// of soft attention. Each round lets the model ask one question of the
// node set ("which node has the most mass in direction k?") and fold
// the answer into a running query state, so T rounds can extract T
// components of information instead of one summary:
//
//   q*_0 = 0
//   for t in 1..T:
//     q*_t   = LSTM(q*_{t-1}, q*_{t-1})          running query
//     e_i,t  = <W_s H_i , W_q q*_t>               attention logits
//     a_i,t  = softmax_i(e_i,t)
//     c*_t   = sum_i a_i,t H_i                   attended read-out
//   return c*_T
//
// The LSTM cell is the one from lstm_cell.mbt (v0.29.0), so Set2Set
// inherits its tested gates and its BPTT cache.
//
// Scope of v0.143.0:
//   - Set2Set parameter bundle + Set2SetState (q, c).
//   - set2set_forward: run T attention rounds, return the final read
//     -out and the advanced state (so a caller can continue a read
//     -out across chunks without restarting the query).
//   - GraphBackbone enum: a tagged GIN / PNA node encoder.
//   - Set2SetClassifier: backbone + Set2Set + MLP head, with
//     forward / predict / loss / accuracy.
//
// Deliberate simplification vs the paper: the paper scores
// e_i = a^T tanh(W_s H_i + W_q q). This uses the two projections
// inside a plain inner product, dropping the tanh and the learned
// output vector. It is the common implementation (and the paper's
// own ablation notes the tanh contributes little); keeping it means
// two Linear maps instead of three.

///|
/// Set2Set parameters: the query LSTM plus the two attention
/// projections. The LSTM's input and hidden widths are both
/// `in_dim`, matching the paper (the previous query is fed as both
/// the input and the hidden state).
pub struct Set2Set {
  in_dim : Int
  processing_steps : Int
  lstm : LstmCellParam
  w_source : GraphLinear
  w_query : GraphLinear
}

///|
/// Build a Set2Set readout for `in_dim`-wide node embeddings.
pub fn Set2Set::new(
  in_dim : Int,
  processing_steps : Int,
  seed : UInt64,
) -> Set2Set {
  {
    in_dim,
    processing_steps,
    lstm: LstmCellParam::new(in_dim, in_dim, seed),
    w_source: GraphLinear::new(in_dim, in_dim, seed + 20UL),
    w_query: GraphLinear::new(in_dim, in_dim, seed + 30UL),
  }
}

///|
/// The carried state of an (optionally interrupted) read-out: the
/// running query `q` and the last attended vector `c`. Both have
/// length `in_dim`.
pub struct Set2SetState {
  q : Array[Float]
  c : Array[Float]
}

///|
/// A zero state, i.e. the state before the first attention round.
pub fn Set2SetState::new(in_dim : Int) -> Set2SetState {
  {
    q: Array::make(in_dim, 0.0F),
    c: Array::make(in_dim, 0.0F),
  }
}

///|
/// Number of parameters in the readout.
pub fn set2set_num_params(m : Set2Set) -> Int {
  let d_h = m.lstm.d_h
  let d_x = m.lstm.d_x
  let in_dim = d_h + d_x
  // 4 gates, each a d_h x in_dim matrix plus a d_h bias.
  let lstm_params = 4 * (d_h * in_dim + d_h)
  let source = m.w_source.in_dim * m.w_source.out_dim + m.w_source.out_dim
  let query = m.w_query.in_dim * m.w_query.out_dim + m.w_query.out_dim
  lstm_params + source + query
}

///|
/// Run `processing_steps` attention rounds over the node embeddings.
///
/// Returns (graph_embedding, advanced_state). The returned embedding
/// has length `in_dim` and equals the last round's attended read-out.
/// Because the state is returned rather than hidden, a caller may
/// split a large node set into chunks, run the read-out per chunk,
/// and continue from the returned state.
pub fn set2set_forward(
  m : Set2Set,
  state : Set2SetState,
  node_emb : Array[Float],
  n_nodes : Int,
  feat_dim : Int,
) -> (Array[Float], Set2SetState) {
  let dim = m.in_dim
  let mut q = state.q
  let mut c = state.c
  if n_nodes == 0 {
    return (c, { q, c, })
  }
  // The source projection W_s H_i does not depend on the query, so it
  // is computed once and reused across all T rounds.
  let src = graph_linear_forward(m.w_source, node_emb, n_nodes)
  for _ in 0..<m.processing_steps {
    // Query update: q* <- LSTM(q*, q*). The previous query is both the
    // input x and the hidden state h_prev.
    let (q_next, cache) = lstm_cell_forward(q, q, c, m.lstm)
    // Attention logits e_i = <W_s H_i, W_q q*>.
    let qproj = graph_linear_forward(m.w_query, q_next, 1)
    let logits : Array[Float] = Array::make(n_nodes, 0.0F)
    for i in 0..<n_nodes {
      let off = i * dim
      let mut acc = 0.0F
      for k in 0..<dim {
        acc = acc + src[off + k] * qproj[k]
      }
      logits[i] = acc
    }
    // Softmax over nodes, max-subtracted for numerical stability.
    let mut mx = logits[0]
    for i in 1..<n_nodes {
      if logits[i] > mx {
        mx = logits[i]
      }
    }
    let mut z = 0.0F
    for i in 0..<n_nodes {
      let e = expf(logits[i] - mx)
      logits[i] = e
      z = z + e
    }
    let inv = if z > 0.0F { 1.0F / z } else { 0.0F }
    // Attended read-out over the ORIGINAL embeddings, not over the
    // source projection -- reading out W_s H instead of H would make
    // the output invariant to a linear reparametrisation of H.
    let read_size = n_nodes * dim
    let read : Array[Float] = Array::make(read_size, 0.0F)
    for i in 0..<n_nodes {
      let a = logits[i] * inv
      let off = i * dim
      for k in 0..<dim {
        read[off + k] = read[off + k] + a * node_emb[off + k]
      }
    }
    c = read
    q = q_next
    ignore(cache)
  }
  (c, { q, c, })
}

///|
/// A node encoder, dispatched by tag. Lets one classifier accept
/// either a GIN or a PNA backbone without duplicating the read-out
/// and head code.
pub(all) enum GraphBackbone {
  Gin(GIN)
  Pna(PNA)
}

///|
/// Forward a tagged backbone to node embeddings [n_nodes x node_dim].
pub fn graph_backbone_forward(
  bb : GraphBackbone,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  match bb {
    Gin(net) => gin_forward(net, g, h)
    Pna(net) => pna_forward(net, g, h)
  }
}

///|
/// Node-embedding width produced by a tagged backbone. The next
/// layer's input width depends on this, so getting it wrong is a
/// silent shape mismatch that `moon check` cannot see.
pub fn graph_backbone_node_dim(bb : GraphBackbone) -> Int {
  match bb {
    Gin(net) => net.out_dim
    Pna(net) => net.out_dim
  }
}

///|
/// Attention-pooling graph classifier: a node encoder, a Set2Set
/// read-out, and a 2-layer MLP head.
pub struct Set2SetClassifier {
  backbone : GraphBackbone
  readout : Set2Set
  head1 : GraphLinear
  head2 : GraphLinear
  node_dim : Int
  hidden_dim : Int
  num_classes : Int
}

///|
/// Build a Set2SetClassifier. The backbone's output width is the
/// read-out's input width.
pub fn Set2SetClassifier::new(
  backbone : GraphBackbone,
  num_classes : Int,
  hidden_dim : Int,
  processing_steps : Int,
  seed : UInt64,
) -> Set2SetClassifier {
  let node_dim = graph_backbone_node_dim(backbone)
  {
    backbone,
    readout: Set2Set::new(node_dim, processing_steps, seed + 40UL),
    head1: GraphLinear::new(node_dim, hidden_dim, seed + 50UL),
    head2: GraphLinear::new(hidden_dim, num_classes, seed + 60UL),
    node_dim,
    hidden_dim,
    num_classes,
  }
}

///|
/// Forward one graph -> class logits [num_classes].
pub fn set2set_classifier_forward(
  clf : Set2SetClassifier,
  g : Graph,
  h : Array[Float],
) -> Array[Float] {
  let node_emb = graph_backbone_forward(clf.backbone, g, h)
  let state = Set2SetState::new(clf.node_dim)
  let (graph_emb, _) = set2set_forward(
    clf.readout, state, node_emb, g.n_nodes, clf.node_dim,
  )
  let pre = graph_linear_forward(clf.head1, graph_emb, 1)
  let hid : Array[Float] = Array::make(clf.hidden_dim, 0.0F)
  for i in 0..<clf.hidden_dim {
    hid[i] = if pre[i] > 0.0F { pre[i] } else { 0.0F }
  }
  graph_linear_forward(clf.head2, hid, 1)
}

///|
/// Argmax over the class logits.
pub fn set2set_classifier_predict(
  clf : Set2SetClassifier,
  g : Graph,
  h : Array[Float],
) -> Int {
  let logits = set2set_classifier_forward(clf, g, h)
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
/// Mean cross-entropy over a list of (graph, features) pairs.
/// Reuses `graph_cross_entropy` from graph_classifier.mbt (v0.140.0).
pub fn set2set_classifier_loss(
  clf : Set2SetClassifier,
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
    let logits = set2set_classifier_forward(clf, graphs[i], feats[i])
    total = total + graph_cross_entropy(logits, targets[i])
  }
  total / Float::from_int(n)
}

///|
/// Classification accuracy over a list of (graph, features) pairs.
pub fn set2set_classifier_accuracy(
  clf : Set2SetClassifier,
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
    let pred = set2set_classifier_predict(clf, graphs[i], feats[i])
    if pred == targets[i] {
      correct = correct + 1.0F
    }
  }
  correct / Float::from_int(n)
}
