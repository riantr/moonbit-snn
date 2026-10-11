// Test that the unit constants match Julia's Float32 representations.
//
// Uses MoonBit's built-in `assert_eq` (provided by the test framework).

///|
test "length units" {
  // Float32(1e2) = 100.0f0
  assert_eq(@core.metre, 100.0F)
  assert_eq(@core.cm, 1.0F)
  // Float32(0.1) = 0.10000000149011612f0
  assert_eq(@core.mm, 0.1F)
  // Float32(1e-4)
  assert_eq(@core.um, 0.0001F)
  // Float32(1e-7)
  assert_eq(@core.nm, 1.0e-7F)
}

///|
test "time units" {
  // Float32(1e3) = 1000.0f0
  assert_eq(@core.second, 1000.0F)
  assert_eq(@core.s, 1000.0F)
  // ms = 1.0f0
  assert_eq(@core.ms, 1.0F)
  // Hz = Float32(0.001) = 0.001f0
  assert_eq(@core.hz, 0.001F)
  // kHz = 1.0f0
  assert_eq(@core.khz, 1.0F)
}

///|
test "voltage units" {
  assert_eq(@core.voltage, 1000.0F)
  // mV = 1.0f0
  assert_eq(@core.mV, 1.0F)
}

///|
test "current units" {
  assert_eq(@core.ampere, 1.0e12F)
  assert_eq(@core.mA, 1.0e9F)
  assert_eq(@core.uA, 1.0e6F)
  assert_eq(@core.nA, 1000.0F)
  // pA = 1.0f0
  assert_eq(@core.pA, 1.0F)
}

///|
test "capacitance units" {
  assert_eq(@core.farad, 1.0e12F)
  assert_eq(@core.mF, 1.0e9F)
  assert_eq(@core.uF, 1.0e6F)
  assert_eq(@core.nF, 1000.0F)
  // pF = 1.0f0
  assert_eq(@core.pF, 1.0F)
}

///|
test "conductance units" {
  assert_eq(@core.siemens, 1.0e9F)
  assert_eq(@core.mS, 1.0e6F)
  // nS = 1.0f0
  assert_eq(@core.nS, 1.0F)
}

///|
test "resistance units" {
  // Ω = 1.0e-9f0 → ohm
  assert_eq(@core.ohm, 1.0e-9F)
  // MΩ = 0.001f0 → mohm
  assert_eq(@core.mohm, 0.001F)
  // GΩ = 1.0f0 → gohm
  assert_eq(@core.gohm, 1.0F)
}

///|
test "concentration units" {
  // M = 1.0e6f0
  assert_eq(@core.M, 1.0e6F)
  assert_eq(@core.mM, 1000.0F)
  assert_eq(@core.uM, 1.0F)
  assert_eq(@core.nM, 0.001F)
}
