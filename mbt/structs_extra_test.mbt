// structs_extra_test.mbt — port of `test/utils/structs_test.jl`
// for the EmptyParam + NetworkModel helpers added in structs_extra.mbt.
//
// (Time + IF + compose tests already covered by time_test.mbt /
// compose_test.mbt; this file focuses on the new helpers.)

///|
test "EmptyParam::new defaults" {
  let p = EmptyParam::new()
  assert_eq(p.p_type, "empty")
  assert_eq(EmptyParam::type_str(p), "empty")
}

///|
test "EmptyParam::with_type custom" {
  let p = EmptyParam::with_type("custom_type")
  assert_eq(p.p_type, "custom_type")
}

///|
test "NetworkModel::new is_valid = true" {
  let m = NetworkModel::new()
  assert_eq(m.is_valid, true)
  assert_eq(isa_model(m), true)
}

///|
test "Time::from_ms — 100ms → tt = 800" {
  // Julia: tts = 100 / 0.125 = 800; Int32(800) = 800.
  let t = Time::from_ms(100.0F)
  assert_eq(get_time(t), 100.0F)
  assert_eq(get_tt(t), 800)
  assert_eq(get_dt(t), 0.125F)
}

///|
test "Time::from_ms — 0.5ms → tt = 4" {
  // tts = 0.5 / 0.125 = 4.
  let t = Time::from_ms(0.5F)
  assert_eq(get_time(t), 0.5F)
  assert_eq(get_tt(t), 4)
}

///|
test "Time::update_time advances correctly" {
  // After 100 steps at dt=0.125ms, t should be 12.5ms and tt=100.
  let t = Time::new()
  let mut s = 0
  while s < 100 {
    update_time(t, get_dt(t))
    s = s + 1
  }
  assert_eq(get_time(t), 12.5F)
  assert_eq(get_tt(t), 100)
}