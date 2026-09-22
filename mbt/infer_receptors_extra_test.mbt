// infer_receptors_extra_test.mbt — additional tests for infer_receptors
// helpers: count_glu, count_gaba, infer_receptors_from.

///|
test "count_glu / count_gaba — separate counts" {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  let rbt = infer_receptors_from(rs)
  if count_glu(rbt) != 2 { fail("count_glu expected 2") }
  if count_gaba(rbt) != 2 { fail("count_gaba expected 2") }
}

///|
test "count_glu — 0 when no glu receptors" {
  // Two gaba receptors only.
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::new(gabaa, gabaa, gabab, gabab)
  let rbt = infer_receptors_from(rs)
  if count_glu(rbt) != 0 { fail("expected 0 glu") }
  if count_gaba(rbt) != 4 { fail("expected 4 gaba") }
}

///|
test "infer_receptors_from — same as infer_receptors(rs.rec)" {
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  let rbt_via_helper = infer_receptors_from(rs)
  let rbt_via_direct = infer_receptors(rs.rec)
  // Both should produce identical glu / gaba arrays.
  let n_glu = rbt_via_helper.glu.length()
  let n_gaba = rbt_via_helper.gaba.length()
  if n_glu != rbt_via_direct.glu.length() {
    fail("glu length mismatch")
  }
  if n_gaba != rbt_via_direct.gaba.length() {
    fail("gaba length mismatch")
  }
  let mut k = 0
  while k < n_glu {
    if rbt_via_helper.glu[k] != rbt_via_direct.glu[k] {
      fail("glu[\{k}] mismatch")
    }
    k = k + 1
  }
  let mut k2 = 0
  while k2 < n_gaba {
    if rbt_via_helper.gaba[k2] != rbt_via_direct.gaba[k2] {
      fail("gaba[\{k2}] mismatch")
    }
    k2 = k2 + 1
  }
}

///|
test "infer_receptors — unknown target is dropped silently" {
  // Build a Receptors with a non-glu/gaba target string. The receptor
  // is skipped in both glu and gaba index arrays.
  let r0 = Receptor::simple(0.0F, 0.1F, 1.0F, 1.0F, "weird")
  let r1 = Receptor::simple(0.0F, 0.1F, 1.0F, 1.0F, "glu")
  let r2 = Receptor::simple(0.0F, 0.1F, 1.0F, 1.0F, "weird")
  let r3 = Receptor::simple(0.0F, 0.1F, 1.0F, 1.0F, "gaba")
  let rbt = infer_receptors([r0, r1, r2, r3])
  if count_glu(rbt) != 1 { fail("expected 1 glu (only r1)") }
  if count_gaba(rbt) != 1 { fail("expected 1 gaba (only r3)") }
  // The glu index should be [1] (r1 is at index 1); gaba [3].
  if rbt.glu[0] != 1 { fail("glu[0] expected 1") }
  if rbt.gaba[0] != 3 { fail("gaba[0] expected 3") }
}

///|
test "count_by_target — total receptor count" {
  // count_by_target returns glu + gaba total.
  let ampa = Receptor::simple(0.0F, 0.26F, 2.0F, 0.73F, "glu")
  let nmda = Receptor::nmda(0.0F, 8.0F, 35.0F, 1.31F)
  let gabaa = Receptor::simple(-70.0F, 4.8F, 29.0F, 0.27F, "gaba")
  let gabab = Receptor::simple(-90.0F, 30.0F, 400.0F, 0.006F, "gaba")
  let rs = Receptors::new(ampa, nmda, gabaa, gabab)
  let rbt = infer_receptors_from(rs)
  // Total = 2 glu + 2 gaba = 4.
  if count_by_target(rbt) != 4 {
    fail("count_by_target expected 4, got \{count_by_target(rbt)}")
  }
}