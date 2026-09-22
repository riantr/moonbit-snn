// Test that the unit constants match Julia's Float32 representations.
//
// Uses MoonBit's built-in `assert_eq` (provided by the test framework).

///|
test "length units" {
  // Float32(1e2) = 100.0f0
  assert_eq(metre, 100.0F)
  assert_eq(cm, 1.0F)
  // Float32(0.1) = 0.10000000149011612f0
  assert_eq(mm, 0.1F)
  // Float32(1e-4)
  assert_eq(um, 0.0001F)
  // Float32(1e-7)
  assert_eq(nm, 1.0e-7F)
}

///|
test "time units" {
  // Float32(1e3) = 1000.0f0
  assert_eq(second, 1000.0F)
  assert_eq(s, 1000.0F)
  // ms = 1.0f0
  assert_eq(ms, 1.0F)
  // Hz = Float32(0.001) = 0.001f0
  assert_eq(hz, 0.001F)
  // kHz = 1.0f0
  assert_eq(khz, 1.0F)
}

///|
test "voltage units" {
  assert_eq(voltage, 1000.0F)
  // mV = 1.0f0
  assert_eq(mV, 1.0F)
}

///|
test "current units" {
  assert_eq(ampere, 1.0e12F)
  assert_eq(mA, 1.0e9F)
  assert_eq(uA, 1.0e6F)
  assert_eq(nA, 1000.0F)
  // pA = 1.0f0
  assert_eq(pA, 1.0F)
}

///|
test "capacitance units" {
  assert_eq(farad, 1.0e12F)
  assert_eq(mF, 1.0e9F)
  assert_eq(uF, 1.0e6F)
  assert_eq(nF, 1000.0F)
  // pF = 1.0f0
  assert_eq(pF, 1.0F)
}

///|
test "conductance units" {
  assert_eq(siemens, 1.0e9F)
  assert_eq(mS, 1.0e6F)
  // nS = 1.0f0
  assert_eq(nS, 1.0F)
}

///|
test "resistance units" {
  // Ω = 1.0e-9f0 → ohm
  assert_eq(ohm, 1.0e-9F)
  // MΩ = 0.001f0 → mohm
  assert_eq(mohm, 0.001F)
  // GΩ = 1.0f0 → gohm
  assert_eq(gohm, 1.0F)
}

///|
test "concentration units" {
  // M = 1.0e6f0
  assert_eq(M, 1.0e6F)
  assert_eq(mM, 1000.0F)
  assert_eq(uM, 1.0F)
  assert_eq(nM, 0.001F)
}
