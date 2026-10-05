// Tests for the Time struct.

///|
test "Time::new" {
  let t = @snn_mbt.Time::new()
  assert_eq(@snn_mbt.get_time(t), 0.0F)
  assert_eq(@snn_mbt.get_tt(t), 0)
  assert_eq(@snn_mbt.get_dt(t), 0.125F)
}

///|
test "Time::from_ms" {
  // Julia: Time(0.0f0) → tts = 0.0f0 / 0.125f0 = 0.0, Int32(0.0) = 0
  let t0 = @snn_mbt.Time::from_ms(0.0F)
  assert_eq(@snn_mbt.get_time(t0), 0.0F)
  assert_eq(@snn_mbt.get_tt(t0), 0)

  // Julia: Time(0.125f0) → tts = 1.0, Int32(1.0) = 1
  let t1 = @snn_mbt.Time::from_ms(0.125F)
  assert_eq(@snn_mbt.get_time(t1), 0.125F)
  assert_eq(@snn_mbt.get_tt(t1), 1)

  // Julia: Time(0.25f0) → tts = 2.0, Int32(2.0) = 2
  let t2 = @snn_mbt.Time::from_ms(0.25F)
  assert_eq(@snn_mbt.get_time(t2), 0.25F)
  assert_eq(@snn_mbt.get_tt(t2), 2)

  // Julia: Time(100.0f0) → tts = 800.0, Int32(800.0) = 800
  let t100 = @snn_mbt.Time::from_ms(100.0F)
  assert_eq(@snn_mbt.get_time(t100), 100.0F)
  assert_eq(@snn_mbt.get_tt(t100), 800)
}

///|
test "update_time" {
  let t = @snn_mbt.Time::new()
  @snn_mbt.update_time(t, 0.125F)
  assert_eq(@snn_mbt.get_time(t), 0.125F)
  assert_eq(@snn_mbt.get_tt(t), 1)
  @snn_mbt.update_time(t, 0.125F)
  assert_eq(@snn_mbt.get_time(t), 0.25F)
  assert_eq(@snn_mbt.get_tt(t), 2)
}

///|
test "reset_time" {
  let t = @snn_mbt.Time::from_ms(100.0F)
  @snn_mbt.reset_time(t)
  assert_eq(@snn_mbt.get_time(t), 0.0F)
  assert_eq(@snn_mbt.get_tt(t), 0)
}
