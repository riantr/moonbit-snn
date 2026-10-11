// Tests for StimulusGroup.

///|
test "StimulusGroup::new — default name + active all true" {
  let s = @snn_mbt.StimulusGroup::new("StimulusGroup", ["Poisson", "SpikeTime", "Current"])
  assert_eq(s.name, "StimulusGroup")
  assert_eq(s.elements.length(), 3)
  assert_eq(s.elements[0], "Poisson")
  assert_eq(s.elements[2], "Current")
  // All active by default
  assert_eq(s.active[0], true)
  assert_eq(s.active[2], true)
}

///|
test "StimulusGroup::new — custom name" {
  let s = @snn_mbt.StimulusGroup::new("MultiComp", ["Poisson", "Poisson"])
  assert_eq(s.name, "MultiComp")
}

///|
test "StimulusGroup::n_elements — count" {
  let s = @snn_mbt.StimulusGroup::new("Group", ["A", "B", "C", "D", "E"])
  assert_eq(@snn_mbt.StimulusGroup::n_elements(s), 5)
}

///|
test "StimulusGroup::n_elements — empty group" {
  let s = @snn_mbt.StimulusGroup::new("Empty", [])
  assert_eq(@snn_mbt.StimulusGroup::n_elements(s), 0)
}

///|
test "StimulusGroup::set_active_all — toggles all" {
  let s = @snn_mbt.StimulusGroup::new("Group", ["A", "B", "C"])
  @snn_mbt.StimulusGroup::set_active_all(s, false)
  assert_eq(s.active[0], false)
  assert_eq(s.active[1], false)
  assert_eq(s.active[2], false)
  @snn_mbt.StimulusGroup::set_active_all(s, true)
  assert_eq(s.active[0], true)
  assert_eq(s.active[2], true)
}

///|
test "StimulusGroup::set_active_one — single toggle" {
  let s = @snn_mbt.StimulusGroup::new("Group", ["A", "B", "C"])
  @snn_mbt.StimulusGroup::set_active_one(s, 1, false)
  assert_eq(s.active[0], true)
  assert_eq(s.active[1], false)
  assert_eq(s.active[2], true)
}

///|
test "StimulusGroup::is_active — reflects set_active calls" {
  let s = @snn_mbt.StimulusGroup::new("Group", ["A", "B"])
  @snn_mbt.StimulusGroup::set_active_one(s, 0, false)
  assert_eq(@snn_mbt.StimulusGroup::is_active(s, 0), false)
  assert_eq(@snn_mbt.StimulusGroup::is_active(s, 1), true)
}

///|
test "StimulusGroup::tag — get element name" {
  let s = @snn_mbt.StimulusGroup::new("Group", ["Poisson", "Current"])
  assert_eq(@snn_mbt.StimulusGroup::tag(s, 0), "Poisson")
  assert_eq(@snn_mbt.StimulusGroup::tag(s, 1), "Current")
}