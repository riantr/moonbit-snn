// Tests for EmptyStimulus.

///|
test "EmptyStimulus::new — default" {
  let s = @stimulus.EmptyStimulus::new()
  assert_eq(s.param.type_str(), "empty")
  assert_eq(s.records.length(), 0)
}

///|
test "EmptyStimulus::with_param — custom EmptyParam" {
  let p = @nn.EmptyParam::with_type("none")
  let s = @stimulus.EmptyStimulus::with_param(p)
  assert_eq(s.param.type_str(), "none")
}

///|
test "stimulate_empty — no-op (doesn't crash)" {
  let s = @stimulus.EmptyStimulus::new()
  @stimulus.stimulate_empty(s, s.param, 100.0F, 0.125F)
  // No state changes expected
  assert_eq(s.param.type_str(), "empty")
}

///|
test "stimulate_empty — multiple calls don't accumulate state" {
  let s = @stimulus.EmptyStimulus::new()
  let mut step : Int = 0
  while step < 1000 {
    @stimulus.stimulate_empty(s, s.param, Float::from_int(step), 0.125F)
    step = step + 1
  }
  // Records still empty
  assert_eq(s.records.length(), 0)
}