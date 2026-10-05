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

///| Unwrap the GAT behind the tag, for the ELU-coverage probe.
fn zz_gat_of(net : TrainableGraphNet) -> GraphAttention {
  match net {
    Gat(m) => m
    _ => GraphAttention::new([2, 2], 1, 0UL)
  }
}

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
  println("--- GAT single head (the two-endpoint fan-out) ---")
  {
    let gl = GATLayer::new(2, 2, 2024UL)
    let out = gat_layer_forward(gl, raw, h)
    let (da, _) = gat_layer_backward(gl, raw, h, out, false)
    let num = numerical_gradient(h, gradcheck_eps(), fn(x) {
      let o2 = gat_layer_forward(gl, raw, x)
      let mut acc = 0.0F
      for i in 0..<o2.length() { acc = acc + o2[i] * o2[i] }
      0.5F * acc
    })
    let (mx, worst) = max_abs_diff(da, num)
    let mut sc = 0.0F
    for i in 0..<da.length() {
      let a = if da[i] < 0.0F { 0.0F - da[i] } else { da[i] }
      if a > sc { sc = a }
    }
    let over = gradcheck_over_fraction(da, num, 0.01F)
    let rel = mx / sc
    let ok = rel <= 0.01F && over == 0
    let vd = if ok { "PASS" } else { "FAIL" }
    println(
      "  GAT-head  REL=" + Float::to_string(rel) +
      "  over1%=" + over.to_string() + "/" + da.length().to_string() +
      "  worst=" + worst.to_string() + "  " + vd,
    )
    if !ok { fails = fails + 1 }
    // Negative control for GAT specifically.
    let bad : Array[Float] = Array::make(da.length(), 0.0F)
    for i in 0..<da.length() { bad[i] = da[i] * 1.5F }
    let bo = gradcheck_over_fraction(bad, num, 0.01F)
    let bm = if bo > 0 { "checker has teeth" } else { "CHECKER IS VACUOUS" }
    println("  GAT control over1%=" + bo.to_string() + "  " + bm)
  }
  println("--- GAT multi-head stack: concatenation + shared-input sum ---")
  // Four shapes, chosen so each structural fact of the stack is
  // exercised by at least one row and no row can pass on the others'
  // account:
  //   GATx1h   1 head,  1 layer -- baseline, no concat, no ELU.
  //   GATx3h   3 heads, 1 layer -- concat only; ELU masks every head.
  //   GATx2h2L 2 heads, 2 layers -- ELU on layer 0 only, plus the
  //            cross-layer accumulation (the final layer is linear).
  //   GATx3h2L 3 heads, 2 layers with a width change -- concat AND a
  //            stride that differs between the two layers, which is the
  //            shape that caught the constructor's in_dim bug.
  {
    let g1 = TrainableGraphNet::Gat(GraphAttention::new([2, 2], 1, 2024UL))
    let g2 = TrainableGraphNet::Gat(GraphAttention::new([2, 3], 3, 2024UL))
    let g3 = TrainableGraphNet::Gat(GraphAttention::new([2, 2, 2], 2, 2024UL))
    let g4 = TrainableGraphNet::Gat(GraphAttention::new([2, 3, 2], 3, 2024UL))
    let gn = ["GATx1h", "GATx3h", "GATx2h2L", "GATx3h2L"]
    let gg = [g1, g2, g3, g4]
    for i in 0..<4 {
      let net = gg[i]
      let gd = trainable_net_node_dim(net)
      let out = trainable_net_forward(net, raw, h)
      // Shape agreement is asserted, not assumed: an interleaving bug
      // in the forward still produces an array, just of the wrong one.
      let shape_ok = out.length() == raw.n_nodes * gd
      let emb_ok = trainable_net_node_dim(net) == gd
      if !shape_ok || !emb_ok {
        println(
          "  " + gn[i] + "  SHAPE MISMATCH out=" + out.length().to_string() +
          " expected=" + (raw.n_nodes * gd).to_string() + "  FAIL",
        )
        fails = fails + 1
      } else {
        let r = zz_row(gn[i], net, raw, h)
        if !r { fails = fails + 1 }
      }
      // The ELU mask is only load-bearing where a hidden pre-activation
      // is <= 0, and ELU's derivative is exactly 1 on its whole
      // positive half-line. So report how many sites that is HERE, but
      // do NOT hang the guarantee on it: a random seed landing entirely
      // on the positive side is a property of the fixture, not of the
      // code, and searching seeds for coverage would be a test that
      // passes by construction. The deterministic row below is what
      // actually proves the mask.
      let (acts, lo, hi) = gat_elu_coverage(zz_gat_of(net), raw, h)
      let multi = zz_gat_of(net).num_layers > 1
      let cov_msg = if !multi {
        "single layer -- no ELU on this row's path"
      } else if acts > 0 {
        "elu-mask load-bearing at " + acts.to_string() +
        " of this row's sites (preact in [" + Float::to_string(lo) + ", " +
        Float::to_string(hi) + "])"
      } else {
        "elu mask inert here (preact in [" + Float::to_string(lo) + ", " +
        Float::to_string(hi) + "]) -- ELU is proved by the GATelu row, not this one"
      }
      println("      " + cov_msg)
    }
  }
  println("--- GAT ELU mask: deterministic, hand-checkable ---")
  // The random stack rows above reported ZERO sites where the ELU mask
  // is load-bearing: every hidden pre-activation came out positive, so
  // exp(raw) was 1.0 everywhere and deleting the mask outright would
  // have produced identical numbers while every row still said PASS.
  // A green row that never touched the code it claims to test is worse
  // than no row, so the mask gets its own row whose pre-activations are
  // known by arithmetic rather than hoped for.
  //
  // Construction: w = [[-1,0],[0,0]], a_src = a_dst = [0,0], and node 0's
  // features set to (1,0) with every other node's zero. Then p[0] is the
  // only nonzero projection and it equals (-1, 0), so the head output is
  // -1 at node 1 (its single in-edge is 0->1) and -0.5 at node 3 (whose
  // in-edges 2->3 and 0->3 split the attention evenly). Every other
  // node reads only zero projections and lands at exactly 0.
  // The two load-bearing sites are therefore -1 and -0.5, and the mask
  // multiplies their gradient by exp(-1) and exp(-0.5).
  {
    let w0 : Array[Float] = Array::make(2, 0.0F)
    w0[0] = 0.0F - 1.0F
    let w1 : Array[Float] = Array::make(2, 0.0F)
    let w : Array[Array[Float]] = Array::make(2, w0)
    w[0] = w0
    w[1] = w1
    let lay = GATLayer::{
      in_dim: 2,
      out_dim: 2,
      w,
      a_src: Array::make(2, 0.0F),
      a_dst: Array::make(2, 0.0F),
    }
    // Only node 0 carries a feature; everything else is zero.
    let h2 : Array[Float] = Array::make(raw.n_nodes * 2, 0.0F)
    h2[0] = 1.0F
    let pre = gat_layer_forward(lay, raw, h2)
    let d_out : Array[Float] = Array::make(pre.length(), 0.0F)
    let mut lo = 0.0F
    let mut hi = 0.0F
    let mut seen = false
    for i in 0..<pre.length() {
      d_out[i] = gat_elu(pre[i])
      if !seen {
        lo = pre[i]
        hi = pre[i]
        seen = true
      } else {
        if pre[i] < lo { lo = pre[i] }
        if pre[i] > hi { hi = pre[i] }
      }
    }
    let (d_masked, _) = gat_layer_backward(lay, raw, h2, d_out, true)
    let (d_plain, _) = gat_layer_backward(lay, raw, h2, d_out, false)
    let num = numerical_gradient(h2, gradcheck_eps(), fn(x) {
      let r = gat_layer_forward(lay, raw, x)
      let mut acc = 0.0F
      for i in 0..<r.length() {
        let y = gat_elu(r[i])
        acc = acc + y * y
      }
      0.5F * acc
    })
    let (mx, worst) = max_abs_diff(d_masked, num)
    let mut sc = 0.0F
    for i in 0..<d_masked.length() {
      let a = if d_masked[i] < 0.0F { 0.0F - d_masked[i] } else { d_masked[i] }
      if a > sc { sc = a }
    }
    let over = gradcheck_over_fraction(d_masked, num, 0.01F)
    let rel = if sc > 0.0F { mx / sc } else { 0.0F }
    let ok = rel <= 0.01F && over == 0
    let vmsg = if ok { "PASS" } else { "FAIL" }
    println(
      "  GATelu  preact in [" + Float::to_string(lo) + ", " +
      Float::to_string(hi) + "]" +
      "  REL=" + Float::to_string(rel) +
      "  over1%=" + over.to_string() + "/" + d_masked.length().to_string() +
      "  worst=" + worst.to_string() + "  " + vmsg,
    )
    if !ok { fails = fails + 1 }
    // Teeth: the masked and unmasked backwards must DIFFER, or this row
    // is not testing the mask either -- it would pass identically with
    // the mask removed.
    let (gap, _) = max_abs_diff(d_masked, d_plain)
    let teeth_ok = gap > 0.0F
    let tmsg = if teeth_ok {
      "mask is load-bearing: elu=true vs elu=false differ by " +
      Float::to_string(gap)
    } else {
      "MASK IS INERT -- masked and unmasked gradients are identical"
    }
    println("  GATelu  " + tmsg)
    if !teeth_ok { fails = fails + 1 }
  }
  println("--- cross-entropy loss vs its own gradient (the SECOND objective) ---")
  // Until v0.155.0 this gate exercised a bounded quadratic objective
  // only, and `graph_cross_entropy` carried an inverted sign on its
  // `logf(sum_exp)` term for six versions because nothing in the package
  // ever trained on it. A gate that checks one objective cannot see a
  // bug in another.
  //
  // Two separate checks, because they are valid on different rows:
  //   VALUE  -- loss vs a hand-computed expectation. Runs on ALL rows,
  //            including the saturated one that exposed the bug.
  //   FD     -- loss vs its own gradient by central difference. Only on
  //            rows whose gradient is O(1); see `gradcheck_ce_fd_rows`
  //            for why the saturated row is unmeasurable this way.
  {
    let cl = gradcheck_ce_probe_logits()
    let ct = gradcheck_ce_probe_targets()
    let ce = gradcheck_ce_probe_expected()
    let fd_rows = gradcheck_ce_fd_rows()
    let mut ce_fails = 0
    for i in 0..<cl.length() {
      let got = graph_cross_entropy(cl[i], ct[i])
      let vdiff = if got - ce[i] < 0.0F {
        0.0F - (got - ce[i])
      } else {
        got - ce[i]
      }
      let vscale = if ce[i] < 0.0F { 0.0F - ce[i] } else { ce[i] }
      let vtol = 0.0001F * vscale
      let vok = vdiff <= vtol
      // Is this row also finite-difference checkable?
      let mut fd_rep = GradCheckReport::{ name: "", max_diff: 0.0F, tol: 0.0F, worst_index: 0, passed: true }
      let mut fd_done = false
      for j in 0..<fd_rows.length() {
        if fd_rows[j] == i {
          fd_rep = gradcheck_ce_consistency("ce" + i.to_string(), cl[i], ct[i])
          fd_done = true
        }
      }
      let verdict = if vok && fd_rep.passed { "PASS" } else { "FAIL" }
      let fd_note = if fd_done {
        "  fd_max_diff=" + Float::to_string(fd_rep.max_diff) +
        "/" + Float::to_string(fd_rep.tol)
      } else {
        "  fd=skipped (gradient is O(1e-4) here; see the doc)"
      }
      println(
        "  " + verdict + "  L=" + Float::to_string(got) +
        " expected=" + Float::to_string(ce[i]) + fd_note,
      )
      if !vok || !fd_rep.passed { ce_fails = ce_fails + 1 }
    }
    if ce_fails > 0 { fails = fails + ce_fails }
    // Negative control: the pre-v0.155.0 formula. This is the exact
    // expression that shipped, and the check must reject it.
    let cl0 = cl[0]
    let m0 = cl0[0]
    let mut se = 0.0F
    for j in 0..<cl0.length() { se = se + expf(cl0[j] - m0) }
    let broken = m0 - cl0[ct[0]] - logf(se)
    let se_ok = broken < 0.0F
    let se_msg = if se_ok {
      "checker has teeth: the old sign gives " +
      Float::to_string(broken) + " (< 0, impossible for a cross-entropy)"
    } else {
      "SUSPECT: old sign gives " + Float::to_string(broken)
    }
    println("  control old-sign loss = " + Float::to_string(broken) + "  " + se_msg)
    if !se_ok { fails = fails + 1 }
  }
  println("--- GAT multi-head dL/dW[0][0] (head 0, layer 0) ---")
  {
    let gn = ["GATx1h", "GATx3h", "GATx2h2L", "GATx3h2L"]
    let gg = [
      TrainableGraphNet::Gat(GraphAttention::new([2, 2], 1, 2024UL)),
      TrainableGraphNet::Gat(GraphAttention::new([2, 3], 3, 2024UL)),
      TrainableGraphNet::Gat(GraphAttention::new([2, 2, 2], 2, 2024UL)),
      TrainableGraphNet::Gat(GraphAttention::new([2, 3, 2], 3, 2024UL)),
    ]
    for i in 0..<4 {
      let pr = gradcheck_param_relative(gn[i], gg[i], raw, h)
      let v = if pr.passed { "PASS" } else { "FAIL" }
      println(
        "  " + gn[i] +
        "  max_diff=" + Float::to_string(pr.max_diff) +
        "  tol=" + Float::to_string(pr.tol) +
        "  " + v,
      )
      if !pr.passed { fails = fails + 1 }
    }
    // Negative control on the multi-head dL/dW, with the same 1.5x
    // corruption that every other row's control uses.
    let gc = TrainableGraphNet::Gat(GraphAttention::new([2, 3, 2], 3, 2024UL))
    let pctl = gradcheck_param_relative("GATpctl", gc, raw, h, corrupt=1.5F)
    let pctl_msg = if pctl.passed { "SUSPECT" } else { "checker has teeth" }
    println(
      "  control max_diff=" + Float::to_string(pctl.max_diff) +
      "  tol=" + Float::to_string(pctl.tol) +
      "  " + pctl_msg,
    )
    // A count check on the parameter count itself: a stack whose
    // num_params disagreed with what the SGD step actually updates
    // would misreport model size without affecting a single gradient.
    // The widths are spelled out rather than derived from the code
    // under test, or the check would be comparing the code with
    // itself. For dims [2,3,2] and 3 heads:
    //   layer 0: in 2,  out 3   -> per head 2*3 + 2*3 = 12, x3 = 36
    //   layer 1: in 9,  out 2   -> per head 9*2 + 2*2 = 22, x3 = 66
    // Layer 1's input width is 9, not 3, because heads are
    // CONCATENATED: 3 heads x layer 0's per-head width 3. Writing 3
    // here was my own first answer and it was wrong -- the same
    // off-by-a-factor-of-num_heads that `GraphAttention::new` had.
    let ga = GraphAttention::new([2, 3, 2], 3, 2024UL)
    let expect_params = 3 * (2 * 3 + 2 * 3) + 3 * (9 * 2 + 2 * 2)
    let got_params = graph_attention_num_params(ga)
    let pc_ok = got_params == expect_params
    let pc_msg = if pc_ok { "PASS" } else { "FAIL" }
    println(
      "  num_params=" + got_params.to_string() +
      " expected=" + expect_params.to_string() +
      "  " + pc_msg,
    )
    if !pc_ok { fails = fails + 1 }
  }
  println("")
  if fails == 0 && ctrl_ok {
    println("VERDICT: PASS")
  } else {
    println("VERDICT: FAIL (" + fails.to_string() + " configs failed)")
  }
}
