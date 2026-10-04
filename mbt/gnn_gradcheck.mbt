// gnn_gradcheck.mbt -- Central-difference gradient checks for the
// message-passing backwards (v0.149.0).
//
// Batches V, W, X and Y shipped nine message-passing architectures
// and four backward passes, and EVERY gradient in them was verified
// by reading the code. That is a real limitation, not a formality: a
// wrong edge-list gradient is invisible to inspection because the
// forward is correct and the shape of the backward is plausible. The
// only thing that catches it is a finite difference.
//
// The reason this file exists rather than a `_test.mbt` file is the
// Windows CreateProcessW 32K command-line limit: this package has
// 511 .mbt files, so `moon test` cannot link. A sub-package executable
// under `examples/` CAN be built and run (`moon run examples/<name>`),
// because its command line only contains its own translation unit
// plus the already-built library. So the checks ship as library
// functions plus a runner, and the runner is what produces evidence.
//
// The check itself is the standard central difference:
//
//   dL/dx_i ~= ( L(x + eps e_i) - L(x - eps e_i) ) / (2 eps)
//
// with eps = 1e-3. In Float32 the noise floor of a central difference
// is about 1e-3 relative for a loss of order 1, so the default
// tolerance is deliberately loose (1e-2 absolute). A tight tolerance
// here would report noise as failure and train the reader to ignore
// the output, which is worse than no check at all.
//
// NEGATIVE CONTROL. A checker that reports PASS for every input is
// worthless, so `gradcheck_negative_control` deliberately hands the
// checker a gradient scaled by 1.5 and asserts that the resulting
// max-abs-difference EXCEEDS the tolerance. If that control ever
// passes "cleanly", the checker is broken, not the code.

///|
/// The outcome of one gradient check.
pub struct GradCheckReport {
  name : String
  /// Largest absolute difference between analytic and numerical.
  max_diff : Float
  /// Tolerance the difference was judged against.
  tol : Float
  /// Index of the worst component in the analytic array.
  worst_index : Int
  /// True when max_diff <= tol.
  passed : Bool
}

///|
/// The default central-difference step: 1e-4.
///
/// 1e-3 is the textbook choice for Float64, but these networks are
/// ReLU-activated, and ReLU is non-differentiable at exactly 0. A
/// central difference of half-width eps straddles the kink whenever a
/// pre-activation lands within eps of zero, and then it reports
/// roughly HALF the one-sided slope -- an O(gradient) error at a
/// handful of indices that has nothing to do with the code being
/// checked. Shrinking eps shrinks that band proportionally, and the
/// round-off cost grows only linearly: at eps = 1e-4 the Float32 noise
/// floor is ~3e-4 on a loss of order 1, still two orders of magnitude
/// below the 1e-2 tolerance. So 1e-4 trades a much larger band for a
/// much smaller noise floor, which is the right side of the trade.
///
/// Measured on the toy graph: at eps = 1e-3 the GCN input check
/// reported max_diff = 0.107 concentrated at 2 of 12 indices (the
/// kink), against a noise floor of ~3e-5.
pub fn gradcheck_eps() -> Float {
  0.0001F
}

///|
/// The default tolerance. 1e-2 RELATIVE when judged against
/// `gradcheck_input_relative`, i.e. `max|analytic| * 1e-2`.
///
/// Absolute tolerances are the wrong instrument here: the six
/// architectures produce gradients spanning two orders of magnitude on
/// the same graph (GIN's logits are unnormalised and reach ~5e3 while
/// GCN's reach ~7), so one absolute number cannot be tight for the
/// small cases and loose enough for the large ones. 1e-2 relative sits
/// ~30x above the Float32 central-difference noise floor and ~30x below
/// the error a genuinely wrong routing produces.
pub fn gradcheck_tol() -> Float {
  0.01F
}

///|
/// Scalar node-level cross-entropy of a tagged network. This is the
/// objective every check differentiates: a function of the input
/// features `h`, which is what makes dL/dh meaningful.
pub fn trainable_net_loss(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
) -> Float {
  let logits = trainable_net_forward(net, g, h)
  node_ce_loss(logits, labels, g.n_nodes, num_classes)
}

///|
/// Analytic gradient of `trainable_net_loss` with respect to the input
/// node features. Returns `(d_input, loss)`.
///
/// This calls `trainable_net_sgd_step` with `lr = 0.0F` and uses only
/// its `d_input` return. At lr = 0 the SGD step computes
/// `w - 0 * grad`, which is bit-identical to `w` for any finite
/// gradient, so the returned network is unchanged -- this is a way to
/// reach the input gradient through one dispatch rather than writing
/// four near-identical `*_backward` wrappers for GCN / GIN / PNA /
/// MPNN. (If a gradient were already NaN or Inf, `0 * NaN` would
/// propagate it, which is a bug worth surfacing anyway.)
pub fn trainable_net_input_grad(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
) -> (Array[Float], Float) {
  let logits = trainable_net_forward(net, g, h)
  let loss = node_ce_loss(logits, labels, g.n_nodes, num_classes)
  let d_logits = node_ce_grad(logits, labels, g.n_nodes, num_classes)
  let (_, d_input) = trainable_net_sgd_step(net, g, h, d_logits, 0.0F)
  (d_input, loss)
}

///|
/// Build a report from an analytic and a numerical gradient.
pub fn gradcheck_make_report(
  name : String,
  analytic : Array[Float],
  numeric : Array[Float],
  tol : Float,
) -> GradCheckReport {
  let (max_diff, worst) = max_abs_diff(analytic, numeric)
  {
    name,
    max_diff,
    tol,
    worst_index: worst,
    passed: max_diff <= tol,
  }
}

///|
/// Check the analytic input gradient of a tagged network against
/// central differences. `eps` and `tol` default to the module-level
/// values; pass explicit ones to tighten or loosen a single check.
pub fn gradcheck_input(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
  eps~ : Float = gradcheck_eps(),
  tol~ : Float = gradcheck_tol(),
) -> GradCheckReport {
  let (analytic, _) = trainable_net_input_grad(net, g, h, labels, num_classes)
  let numeric = numerical_gradient(h, eps, fn(x) {
    trainable_net_loss(net, g, x, labels, num_classes)
  })
  gradcheck_make_report(name, analytic, numeric, tol)
}

///|
/// Count how many components exceed the tolerance. A report whose max
/// is 10x the tolerance at 2 of 12 indices is a ReLU kink; one where
/// every index exceeds it is a real error. The count is what tells
/// those two apart, so it is part of the report rather than something
/// a reader has to infer from the max alone.
pub fn gradcheck_over_count(
  analytic : Array[Float],
  numeric : Array[Float],
  tol : Float,
) -> Int {
  let n = if analytic.length() < numeric.length() {
    analytic.length()
  } else {
    numeric.length()
  }
  let mut count = 0
  for i in 0..<n {
    let diff = analytic[i] - numeric[i]
    let ad = if diff < 0.0F { 0.0F - diff } else { diff }
    if ad > tol {
      count = count + 1
    }
  }
  count
}

///|
/// A BOUNDED quadratic objective: L = 0.5 * sum_i out_i^2 over the
/// network's node embeddings. Its gradient w.r.t. the output is
/// exactly `out`, so no softmax / one-hot layer sits between the check
/// and the parameter gradients being tested.
///
/// This replaced node-level cross-entropy as the checked objective.
/// CE is the wrong choice for a gradient check here: the GIN forward
/// has no adjacency normalisation, so its logits run to ~5e3 on the
/// toy graph and the mean CE to ~1.6e3. An ABSOLUTE tolerance is
/// meaningless against gradients that scale with the logit magnitude
/// (it made GIN look 10x worse than GCN when GIN's RELATIVE error was
/// in fact the smallest of all six configs), and the Float32
/// central-difference noise floor grows with the loss, so the noise
/// band grows too. A quadratic objective is O(|out|^2) instead of
/// O(|out|) and needs no logits to be in a sane range.
pub fn trainable_net_l2_loss(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
) -> Float {
  let out = trainable_net_forward(net, g, h)
  let mut acc = 0.0F
  for i in 0..<out.length() {
    acc = acc + out[i] * out[i]
  }
  0.5F * acc
}

///|
/// Analytic dL/d(out) = out for `trainable_net_l2_loss`, then the
/// input gradient through the network. Returns `(d_input, loss)`.
pub fn trainable_net_l2_input_grad(
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
) -> (Array[Float], Float) {
  let out = trainable_net_forward(net, g, h)
  let mut acc = 0.0F
  for i in 0..<out.length() {
    acc = acc + out[i] * out[i]
  }
  let (_, d_input) = trainable_net_sgd_step(net, g, h, out, 0.0F)
  (d_input, 0.5F * acc)
}

///|
/// Check dL/dh against central differences of the quadratic objective,
/// judged RELATIVELY: the pass criterion is
/// `max_diff <= tol * max|analytic|`, so a configuration whose
/// gradients are legitimately 100x larger is not penalised for it.
/// Falls back to the absolute criterion when the gradient is ~0.
pub fn gradcheck_input_relative(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  eps~ : Float = gradcheck_eps(),
  tol~ : Float = gradcheck_tol(),
) -> GradCheckReport {
  let (analytic, _) = trainable_net_l2_input_grad(net, g, h)
  let numeric = numerical_gradient(h, eps, fn(x) {
    trainable_net_l2_loss(net, g, x)
  })
  let (max_diff, worst) = max_abs_diff(analytic, numeric)
  let mut scale = 0.0F
  for i in 0..<analytic.length() {
    let a = if analytic[i] < 0.0F { 0.0F - analytic[i] } else { analytic[i] }
    if a > scale {
      scale = a
    }
  }
  // When the gradient is essentially zero the absolute criterion is the
  // only meaningful one, so report against `tol` directly.
  let eff = if scale < 0.000001F { tol } else { tol * scale }
  {
    name,
    max_diff,
    tol: eff,
    worst_index: worst,
    passed: max_diff <= eff,
  }
}

///|
/// Negative control: scale a gradient and confirm the checker notices.
///
/// The point is that `gradcheck_input` must FAIL on a deliberately
/// wrong gradient. If this returns `passed == true`, the checker is
/// vacuous -- e.g. if both arrays are all-zero, or the comparison is
/// against a copy of the analytic value -- and every other PASS from
/// it is meaningless.
pub fn gradcheck_negative_control(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  labels : Array[Int],
  num_classes : Int,
  tol~ : Float = gradcheck_tol(),
) -> GradCheckReport {
  let (analytic, _) = trainable_net_input_grad(net, g, h, labels, num_classes)
  let numeric = numerical_gradient(h, gradcheck_eps(), fn(x) {
    trainable_net_loss(net, g, x, labels, num_classes)
  })
  // Corrupt the analytic side by 50 percent.
  let corrupt : Array[Float] = Array::make(analytic.length(), 0.0F)
  for i in 0..<analytic.length() {
    corrupt[i] = analytic[i] * 1.5F
  }
  // Expected: `passed == false`. The report is returned so the caller
  // can assert that rather than trusting a bare Unit.
  gradcheck_make_report(name, corrupt, numeric, tol)
}

///|
/// helper: a small deterministic graph for the checks. A ring of 6
/// nodes with 2 input features and an extra chord, so every node has
/// an in-degree of 1 or 2 -- enough for the degree scalers, the
/// softmax normalisers and the argmax routers all to be exercised,
/// without the cost of a large graph.
pub fn gradcheck_toy_graph() -> Graph {
  let n = 6
  // A 6-ring plus three chords, so in-degrees are 1 or 2: enough to
  // exercise the degree scalers, the softmax normalisers and the
  // argmax routers without the cost of a large graph.
  let total = n + 3
  let s : Array[Int] = Array::make(total, 0)
  let d : Array[Int] = Array::make(total, 0)
  let mut k = 0
  for i in 0..<n {
    s[k] = i
    d[k] = (i + 1) % n
    k = k + 1
  }
  s[k] = 0
  d[k] = 3
  k = k + 1
  s[k] = 1
  d[k] = 4
  k = k + 1
  s[k] = 2
  d[k] = 5
  k = k + 1
  let feats : Array[Float] = Array::make(n * 2, 0.0F)
  for v in 0..<n {
    feats[v * 2] = 0.5F + 0.1F * Float::from_int(v)
    feats[v * 2 + 1] = -0.25F + 0.05F * Float::from_int(v)
  }
  Graph::new(n, 2, feats, s, d, Array::make(0, 0.0F))
}

///|
/// helper: labels for the toy graph, two classes split by a ring
/// parity so that neighbouring nodes mostly DISAGREE. A homophilous
/// fixture would let a broken mean aggregator look correct.
pub fn gradcheck_toy_labels() -> Array[Int] {
  let labels : Array[Int] = Array::make(6, 0)
  for v in 0..<6 {
    labels[v] = if v % 3 == 0 { 0 } else { 1 }
  }
  labels
}

///|
/// helper: the toy graph with the symmetric GCN normalisation applied.
/// Use this for the GCN check specifically -- `gcn_sgd_step` reads
/// `edge_weight`, and a raw unweighted edge list is a legal but
/// different model.
pub fn gradcheck_toy_graph_gcn() -> Graph {
  normalise_adjacency(gradcheck_toy_graph())
}
