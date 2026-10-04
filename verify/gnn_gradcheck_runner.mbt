// zz_gradcheck_runner.mbt -- the canonical GNN gradient gate
// (Batch AA, v0.151.0).
//
// WHY IT LIVES HERE AND NOT IN examples/ -- see the header of
// ../verify_gnn_grads.ps1. A fresh sub-package cannot resolve any
// symbol from the parent module in moon 0.1.20260920 (not even
// `Graph`), and `moon test` cannot link with 511 .mbt files on a 32K
// Windows command line, so the gate runs inside the library package
// behind a temporary `is-main` flip that the driver script reverts.
//
// Criteria, and why each one is here:
//   REL = max|analytic - numeric| / max|analytic|   -> scale-free, so
//     six architectures whose gradients span three orders of magnitude
//     are judged on the same footing. (Judging them by absolute
//     max_diff made GIN look 10x worse than GCN when GIN's relative
//     error was the smallest of all six.)
//   over 1% = components deviating by >1% of the gradient scale -> the
//     kink-vs-bug discriminator. A ReLU kink touches 1-2 indices; a
//     wrong routing touches nearly all of them.
//   objective = 0.5 * sum(out^2) -> gradient w.r.t. out is exactly
//     `out`, and the loss is O(|out|^2) so it does not blow up when a
//     network's logits are unnormalised.
//   negative control -> the checker must FAIL a deliberately corrupted
//     gradient, or every PASS above is meaningless.

///| One row per configuration.
fn zz_row(name : String, net : TrainableGraphNet, g : Graph, h : Array[Float]) -> Bool {
  let (analytic, _) = trainable_net_l2_input_grad(net, g, h)
  let numeric = numerical_gradient(h, gradcheck_eps(), fn(x) {
    trainable_net_l2_loss(net, g, x)
  })
  let (mx, worst) = max_abs_diff(analytic, numeric)
  let over = gradcheck_over_fraction(analytic, numeric, 0.01F)
  let mut scale = 0.0F
  for i in 0..<analytic.length() {
    let a = if analytic[i] < 0.0F { 0.0F - analytic[i] } else { analytic[i] }
    if a > scale {
      scale = a
    }
  }
  let rel = mx / scale
  // PASS requires both a small relative error AND that it is not
  // spread across the vector.
  let ok = rel <= 0.01F && over == 0
  let verdict = if ok { "PASS" } else { "FAIL" }
  println(
    "  " + name +
    "  REL=" + Float::to_string(rel) +
    "  over1%=" + over.to_string() + "/" + analytic.length().to_string() +
    "  worst=" + worst.to_string() +
    "  " + verdict,
  )
  ok
}

fn main {
  let raw = gradcheck_toy_graph()
  let norm = gradcheck_toy_graph_gcn()
  let h = raw.features
  println("=== GNN gradient gate: quadratic objective, RELATIVE tol 1e-2 ===")
  println("--- 1 layer (no composition) ---")
  let mut fails = 0
  if !zz_row("GINx1", TrainableGraphNet::Gin(GIN::new([2, 2], 0.0F, 2024UL)), raw, h) { fails = fails + 1 }
  if !zz_row("GCNx1", TrainableGraphNet::Gcn(GCN::new([2, 2], 0.0F, 2024UL)), norm, h) { fails = fails + 1 }
  if !zz_row("MPNNx1", TrainableGraphNet::Mpnn(MPnn::new([2, 2], 2024UL)), raw, h) { fails = fails + 1 }
  println("--- 2 layers, width-changing [2,4,2] (composition) ---")
  if !zz_row("GINx2", TrainableGraphNet::Gin(GIN::new([2, 4, 2], 0.0F, 2024UL)), raw, h) { fails = fails + 1 }
  if !zz_row("GCNx2", TrainableGraphNet::Gcn(GCN::new([2, 4, 2], 0.0F, 2024UL)), norm, h) { fails = fails + 1 }
  if !zz_row("MPNNx2", TrainableGraphNet::Mpnn(MPnn::new([2, 4, 2], 2024UL)), raw, h) { fails = fails + 1 }
  println("--- PNA 1 layer ---")
  if !zz_row(
    "PNAx1",
    TrainableGraphNet::Pna(
      PNA::new([2, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL),
    ),
    raw, h,
  ) { fails = fails + 1 }
  println("--- negative control (must FAIL) ---")
  let gin = TrainableGraphNet::Gin(GIN::new([2, 2], 0.0F, 2024UL))
  let (a_ok, _) = trainable_net_l2_input_grad(gin, raw, h)
  let corrupt : Array[Float] = Array::make(a_ok.length(), 0.0F)
  for i in 0..<a_ok.length() {
    corrupt[i] = a_ok[i] * 1.5F
  }
  let n_ok = numerical_gradient(h, gradcheck_eps(), fn(x) {
    trainable_net_l2_loss(gin, raw, x)
  })
  let (c_max, _) = max_abs_diff(corrupt, n_ok)
  let c_over = gradcheck_over_fraction(corrupt, n_ok, 0.01F)
  let ctrl_ok = c_over > 0
  let ctrl_msg = if ctrl_ok { "checker has teeth" } else { "CHECKER IS VACUOUS" }
  println(
    "  control  max_diff=" + Float::to_string(c_max) +
    "  over1%=" + c_over.to_string() +
    "  " + ctrl_msg,
  )
  println("--- PNA 2 layers: separate probe (known crash) ---")
  let pna2 = PNA::new([2, 4, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL)
  let fwd = pna_forward(pna2, raw, h)
  let (_, ins) = pna_forward_with_inputs(pna2, raw, h)
  println(
    "  widths: out=" + fwd.length().to_string() +
    "  in0=" + ins[0].length().to_string() +
    "  in1=" + ins[1].length().to_string() +
    "  (forward is consistent)",
  )
  if !zz_row("PNAx2", TrainableGraphNet::Pna(pna2), raw, h) { fails = fails + 1 }
  println("--- dL/dW[0][0] parameter gradients (first-layer projection) ---")
  let gin1 = TrainableGraphNet::Gin(GIN::new([2, 2], 0.0F, 2024UL))
  let gin2 = TrainableGraphNet::Gin(GIN::new([2, 4, 2], 0.0F, 2024UL))
  let gcn1 = TrainableGraphNet::Gcn(GCN::new([2, 2], 0.0F, 2024UL))
  let gcn2 = TrainableGraphNet::Gcn(GCN::new([2, 4, 2], 0.0F, 2024UL))
  let mpnn1 = TrainableGraphNet::Mpnn(MPnn::new([2, 2], 2024UL))
  let mpnn2 = TrainableGraphNet::Mpnn(MPnn::new([2, 4, 2], 2024UL))
  let pna1 = TrainableGraphNet::Pna(
    PNA::new([2, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL),
  )
  let pna2b = TrainableGraphNet::Pna(
    PNA::new([2, 4, 2], 4, 0.001F, PNAScaler::Amplification, 2024UL),
  )
  let pn = ["GINx1", "GINx2", "GCNx1", "GCNx2", "MPNNx1", "MPNNx2", "PNAx1", "PNAx2"]
  let pg = [gin1, gin2, gcn1, gcn2, mpnn1, mpnn2, pna1, pna2b]
  let pp = [raw, raw, norm, norm, raw, raw, raw, raw]
  for i in 0..<8 {
    let pr = gradcheck_param_relative(pn[i], pg[i], pp[i], h)
    let v = if pr.passed { "PASS" } else { "FAIL" }
    println(
      "  " + pn[i] +
      "  max_diff=" + Float::to_string(pr.max_diff) +
      "  tol=" + Float::to_string(pr.tol) +
      "  " + v,
    )
    if !pr.passed {
      fails = fails + 1
    }
  }
  println("--- negative control on dL/dW (must FAIL) ---")
  {
    let pctl = gradcheck_param_relative("pctl", gin1, raw, h, corrupt=1.5F)
    let pctl_msg = if pctl.passed { "SUSPECT" } else { "checker has teeth" }
    println(
      "  control max_diff=" + Float::to_string(pctl.max_diff) +
      "  tol=" + Float::to_string(pctl.tol) +
      "  " + pctl_msg,
    )
  }
  println("")
  if fails == 0 && ctrl_ok {
    println("VERDICT: PASS")
  } else {
    println("VERDICT: FAIL (" + fails.to_string() + " configs failed)")
  }
}
