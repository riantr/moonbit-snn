// Tests for infer_receptors.

///|
test "infer_receptors — empty input → empty result" {
  let recs : Array[Receptor] = []
  let r = infer_receptors(recs)
  assert_eq(r.glu.length(), 0)
  assert_eq(r.gaba.length(), 0)
}

///|
test "infer_receptors — single glu receptor" {
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let r = infer_receptors([ampa])
  assert_eq(r.glu.length(), 1)
  assert_eq(r.gaba.length(), 0)
  assert_eq(r.glu[0], 0)
}

///|
test "infer_receptors — single gaba receptor" {
  let gabaa = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let r = infer_receptors([gabaa])
  assert_eq(r.glu.length(), 0)
  assert_eq(r.gaba.length(), 1)
  assert_eq(r.gaba[0], 0)
}

///|
test "infer_receptors — full 4-receptor set" {
  let ampa = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let nmda = Receptor::nmda(0.0F, 1.0F, 100.0F, 1.0F)
  let gabaa = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let gabab = Receptor::simple(-75.0F, 0.5F, 20.0F, 1.0F, "gaba")
  let r = infer_receptors([ampa, nmda, gabaa, gabab])
  // AMPA + NMDA both have target="glu" via nmda's simple call
  // → 2 indices in glu; GABAa + GABAb → 2 in gaba
  assert_eq(r.glu.length(), 2)
  assert_eq(r.gaba.length(), 2)
  assert_eq(r.glu[0], 0)  // ampa
  assert_eq(r.glu[1], 1)  // nmda
  assert_eq(r.gaba[0], 2) // gabaa
  assert_eq(r.gaba[1], 3) // gabab
}

///|
test "infer_receptors — mixed order preserved" {
  let r1 = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let r2 = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let r3 = Receptor::simple(-75.0F, 0.5F, 20.0F, 1.0F, "gaba")
  let r4 = Receptor::simple(0.0F, 1.0F, 100.0F, 1.0F, "glu")
  let r = infer_receptors([r1, r2, r3, r4])
  // glu indices: 1 (r2), 3 (r4)
  // gaba indices: 0 (r1), 2 (r3)
  assert_eq(r.glu.length(), 2)
  assert_eq(r.gaba.length(), 2)
  assert_eq(r.glu[0], 1)
  assert_eq(r.glu[1], 3)
  assert_eq(r.gaba[0], 0)
  assert_eq(r.gaba[1], 2)
}

///|
test "infer_receptors — unknown target dropped (no error)" {
  // Receptor with target="none" is silently dropped (matches Julia's
  // @error path but doesn't raise).
  let r1 = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu")
  let r2 = Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "weird")
  let r3 = Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba")
  let r = infer_receptors([r1, r2, r3])
  assert_eq(r.glu.length(), 1)
  assert_eq(r.gaba.length(), 1)
}

///|
test "count_by_target — sums both buckets" {
  let recs : Array[Receptor] = [
    Receptor::simple(0.0F, 1.0F, 6.0F, 1.0F, "glu"),
    Receptor::simple(0.0F, 1.0F, 100.0F, 1.0F, "glu"),
    Receptor::simple(-75.0F, 0.5F, 2.0F, 1.0F, "gaba"),
  ]
  let r = infer_receptors(recs)
  assert_eq(count_by_target(r), 3)
}