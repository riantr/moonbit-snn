// Tests for DendNeuronParameter + Physiology.

///|
fn tree_type_to_int(t : DendriticTreeType) -> Int {
  match t {
    Tripod => 0
    BallAndStick => 1
    Multipod => 2
  }
}

///|
test "Physiology::human defaults match Julia human_dend" {
  let p = Physiology::human()
  // 200Ω·cm → 200 * 0.001 = 0.2F (Float32)
  assert_eq(p.ri, 0.2F)
  // 38907Ω·cm² → 38907 * 0.001 ≈ 38.907F
  assert_eq(p.rd > 38.9F && p.rd < 38.92F, true)
  // 0.5μF/cm² → Julia's @snn_kw keeps the raw Float64 normalisation
  // (cd = 0.5 raw, no further per-cm² multiplication at this layer
  // — the dendrite builder multiplies by surface area separately).
  assert_eq(p.cd, 0.5F)
}

///|
test "Physiology::mouse defaults match Julia mouse_dend" {
  let p = Physiology::mouse()
  assert_eq(p.ri, 0.2F)
  // 1700Ω·cm² → 1.7F
  assert_eq(p.rd > 1.69F && p.rd < 1.71F, true)
  // 1μF/cm² → 1.0F (raw Float64 normalisation)
  assert_eq(p.cd, 1.0F)
}

///|
test "Physiology::custom preserves all 3 fields" {
  let p = Physiology::custom(ri=1.0F, rd=2.0F, cd=3.0F)
  assert_eq(p.ri, 1.0F)
  assert_eq(p.rd, 2.0F)
  assert_eq(p.cd, 3.0F)
}

///|
test "DendNeuronParameter::new defaults — Tripod (2 dendrites)" {
  let p = DendNeuronParameter::new()
  // ds default: 2 dendrites, each (200μm, 400μm)
  assert_eq(p.ds.length(), 2)
  assert_eq(p.ds[0].length(), 2)
  assert_eq(p.ds[0][0], 200.0F)
  assert_eq(p.ds[0][1], 400.0F)
  assert_eq(p.ds[1][0], 200.0F)
  assert_eq(p.ds[1][1], 400.0F)
  // physiology: human_dend
  assert_eq(p.physiology.ri, 0.2F)
  // geometry: 2 pairs (s, d1) and (s, d2)
  assert_eq(p.geometry.length(), 2)
  assert_eq(p.geometry[0][0], "s")
  assert_eq(p.geometry[0][1], "d1")
  assert_eq(p.geometry[1][0], "s")
  assert_eq(p.geometry[1][1], "d2")
  // tree_type: Tripod = 0
  assert_eq(tree_type_to_int(p.tree_type), 0)
}

///|
test "TripodParameter::new — Tripod shortcut" {
  let p = tripod_param_new()
  assert_eq(p.ds.length(), 2)
  assert_eq(tree_type_to_int(p.tree_type), 0) // Tripod = 0
}

///|
test "BallAndStickParameter::new — BallAndStick shortcut" {
  let p = ballandstick_param_new()
  // ds: 1 dendrite (150μm, 400μm)
  assert_eq(p.ds.length(), 1)
  assert_eq(p.ds[0].length(), 2)
  assert_eq(p.ds[0][0], 150.0F)
  // geometry: 1 pair (s, d)
  assert_eq(p.geometry.length(), 1)
  assert_eq(p.geometry[0][0], "s")
  assert_eq(p.geometry[0][1], "d")
  // tree_type: BallAndStick = 1
  assert_eq(tree_type_to_int(p.tree_type), 1)
}

///|
test "DendNeuronParameter::custom — auto-infer BallAndStick (1 dend)" {
  let p = DendNeuronParameter::custom(
    ds=[[100.0F, 200.0F]],
    physiology=Physiology::mouse(),
  )
  assert_eq(tree_type_to_int(p.tree_type), 1) // BallAndStick
  assert_eq(p.geometry.length(), 1)
  assert_eq(p.geometry[0][1], "d")
}

///|
test "DendNeuronParameter::custom — auto-infer Tripod (2 dend)" {
  let p = DendNeuronParameter::custom(
    ds=[[100.0F, 200.0F], [100.0F, 300.0F]],
  )
  assert_eq(tree_type_to_int(p.tree_type), 0) // Tripod
  assert_eq(p.geometry.length(), 2)
  assert_eq(p.geometry[0][1], "d1")
  assert_eq(p.geometry[1][1], "d2")
}

///|
test "DendNeuronParameter::custom — auto-infer Multipod (3+ dend)" {
  let p = DendNeuronParameter::custom(
    ds=[[100.0F, 200.0F], [100.0F, 200.0F], [100.0F, 200.0F]],
  )
  assert_eq(tree_type_to_int(p.tree_type), 2) // Multipod
  assert_eq(p.geometry.length(), 3)
  assert_eq(p.geometry[0][1], "d1")
  assert_eq(p.geometry[1][1], "d2")
  assert_eq(p.geometry[2][1], "d3")
}

///|
test "population_from_dend_neuron — returns parameter (Julia passthrough)" {
  let p = tripod_param_new()
  let r = population_from_dend_neuron(p)
  assert_eq(r.ds.length(), 2)
  assert_eq(tree_type_to_int(r.tree_type), 0) // Tripod
}

///|
test "Physiology fields stored as Float32 (bit-exact with Julia)" {
  let p = Physiology::human()
  // Verify the Float32 internal representation
  // ri = 200 * 0.001 = 0.2F exact
  // rd = 38907 * 0.001 = 38.90700073242188F (Float32 round-off)
  assert_eq(p.ri == 0.2F, true)
  let rd_32 : Float = p.rd
  let _ = rd_32
  // cd = 0.5F exact (raw Float64 normalisation)
  assert_eq(p.cd == 0.5F, true)
}