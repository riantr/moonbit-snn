// zz_gradcheck_runner.mbt -- TEMPORARY in-package gradient-check
// diagnostic (v0.149.0 follow-up).
//
// Layer-count isolation: run the SAME check on 1-layer and 2-layer
// stacks of each architecture. A 1-layer stack has no composition to
// get wrong, so a failure there is a single-layer backward bug while
// a failure that only appears at 2 layers is a stack bug.
//
// Also prints analytic-vs-numeric side by side for the first few
// components, because "max_diff = 1.39" does not say whether the
// analytic side is too big, too small, or has the wrong sign.

fn zz_dump(
  name : String,
  net : TrainableGraphNet,
  g : Graph,
  h : Array[Float],
  n : Int,
) -> Unit {
  let (analytic, loss) = trainable_net_l2_input_grad(net, g, h)
  let numeric = numerical_gradient(h, gradcheck_eps(), fn(x) {
    trainable_net_l2_loss(net, g, x)
  })
  let (mx, _) = max_abs_diff(analytic, numeric)
  let mut scale = 0.0F
  for i in 0..<analytic.length() {
    let a = if analytic[i] < 0.0F { 0.0F - analytic[i] } else { analytic[i] }
    if a > scale { scale = a }
  }
  let rel = mx / scale
  let verdict = if rel <= 0.01F { "PASS" } else { "FAIL" }
  println(
    "  " + name + "  L2=" + Float::to_string(loss) +
    "  |grad|max=" + Float::to_string(scale) +
    "  max_diff=" + Float::to_string(mx) +
    "  REL=" + Float::to_string(rel) +
    "  " + verdict,
  )
  for i in 0..<n {
    if i < analytic.length() && i < numeric.length() {
      println(
        "    i=" + i.to_string() +
        "  analytic=" + Float::to_string(analytic[i]) +
        "  numeric=" + Float::to_string(numeric[i]),
      )
    }
  }
}

fn main {
  let raw = gradcheck_toy_graph()
  let norm = gradcheck_toy_graph_gcn()
  let feats = raw.features
  println("=== quadratic objective, RELATIVE tolerance (tol=1e-2) ===")
  zz_dump("GINx1", TrainableGraphNet::Gin(GIN::new([2, 2], 0.0F, 2024UL)), raw, feats, 3)
  zz_dump("GINx2", TrainableGraphNet::Gin(GIN::new([2, 4, 2], 0.0F, 2024UL)), raw, feats, 3)
  zz_dump("GCNx1", TrainableGraphNet::Gcn(GCN::new([2, 2], 0.0F, 2024UL)), norm, feats, 3)
  zz_dump("GCNx2", TrainableGraphNet::Gcn(GCN::new([2, 4, 2], 0.0F, 2024UL)), norm, feats, 3)
  zz_dump("MPNNx1", TrainableGraphNet::Mpnn(MPnn::new([2, 2], 2024UL)), raw, feats, 3)
  zz_dump("MPNNx2", TrainableGraphNet::Mpnn(MPnn::new([2, 4, 2], 2024UL)), raw, feats, 3)
  zz_dump("PNAx1", TrainableGraphNet::Pna(PNA::new([2, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL)), raw, feats, 3)
  zz_dump("PNAx2", TrainableGraphNet::Pna(PNA::new([2, 4, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL)), raw, feats, 3)
  println("done")
}