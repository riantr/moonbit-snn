// Test that the unit constants match Julia's Float32 representations.
//
// Uses MoonBit's built-in `assert_eq` (provided by the test framework).

///|
test "length units" {
  // Float32(1e2) = 100.0f0
  assert_eq(@snn_mbt.metre, 100.0F)
  assert_eq(@snn_mbt.cm, 1.0F)
  // Float32(0.1) = 0.10000000149011612f0
  assert_eq(@snn_mbt.mm, 0.1F)
  // Float32(1e-4)
  assert_eq(@snn_mbt.um, 0.0001F)
  // Float32(1e-7)
  assert_eq(@snn_mbt.nm, 1.0e-7F)
}

///|
test "time units" {
  // Float32(1e3) = 1000.0f0
  assert_eq(@snn_mbt.second, 1000.0F)
  assert_eq(@snn_mbt.s, 1000.0F)
  // ms = 1.0f0
  assert_eq(@snn_mbt.ms, 1.0F)
  // Hz = Float32(0.001) = 0.001f0
  assert_eq(@snn_mbt.hz, 0.001F)
  // kHz = 1.0f0
  assert_eq(@snn_mbt.khz, 1.0F)
}

///|
test "voltage units" {
  assert_eq(@snn_mbt.voltage, 1000.0F)
  // mV = 1.0f0
  assert_eq(@snn_mbt.mV, 1.0F)
}

///|
test "current units" {
  assert_eq(@snn_mbt.ampere, 1.0e12F)
  assert_eq(@snn_mbt.mA, 1.0e9F)
  assert_eq(@snn_mbt.uA, 1.0e6F)
  assert_eq(@snn_mbt.nA, 1000.0F)
  // pA = 1.0f0
  assert_eq(@snn_mbt.pA, 1.0F)
}

///|
test "capacitance units" {
  assert_eq(@snn_mbt.farad, 1.0e12F)
  assert_eq(@snn_mbt.mF, 1.0e9F)
  assert_eq(@snn_mbt.uF, 1.0e6F)
  assert_eq(@snn_mbt.nF, 1000.0F)
  // pF = 1.0f0
  assert_eq(@snn_mbt.pF, 1.0F)
}

///|
test "conductance units" {
  assert_eq(@snn_mbt.siemens, 1.0e9F)
  assert_eq(@snn_mbt.mS, 1.0e6F)
  // nS = 1.0f0
  assert_eq(@snn_mbt.nS, 1.0F)
}

///|
test "resistance units" {
  // Ω = 1.0e-9f0 → ohm
  assert_eq(@snn_mbt.ohm, 1.0e-9F)
  // MΩ = 0.001f0 → mohm
  assert_eq(@snn_mbt.mohm, 0.001F)
  // GΩ = 1.0f0 → gohm
  assert_eq(@snn_mbt.gohm, 1.0F)
}

///|
test "concentration units" {
  // M = 1.0e6f0
  assert_eq(@snn_mbt.M, 1.0e6F)
  assert_eq(@snn_mbt.mM, 1000.0F)
  assert_eq(@snn_mbt.uM, 1.0F)
  assert_eq(@snn_mbt.nM, 0.001F)
}
