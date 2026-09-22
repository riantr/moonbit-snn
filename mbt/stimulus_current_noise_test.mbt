// Tests for CurrentNoise.

///|
test "CurrentNoise::new — all zeros" {
  let c = CurrentNoise::new(4)
  assert_eq(c.n, 4)
  assert_eq(c.i_base.length(), 4)
  assert_eq(c.i_base[0], 0.0F)
  assert_eq(c.noise_sigma, 0.0F)
  assert_eq(c.alpha, 0.0F)
}

///|
test "CurrentNoise::custom — explicit values" {
  let c = CurrentNoise::custom(2, 100.0F, 20.0F, 0.3F)
  assert_eq(c.i_base[0], 100.0F)
  assert_eq(c.i_base[1], 100.0F)
  assert_eq(c.noise_sigma, 20.0F)
  assert_eq(c.alpha, 0.3F)
}

///|
test "stimulate_noise — α=0: I = I_base + noise" {
  let c = CurrentNoise::custom(1, 100.0F, 0.0F, 0.0F)
  let i_target : Array[Float] = Array::make(1, 0.0F)
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  stimulate_noise(c, i_target, rng)
  // α=0, σ=0: i_target[0] = (100 + 0) * 1 + 0 * 0 = 100
  assert_eq(i_target[0], 100.0F)
}

///|
test "stimulate_noise — α=0, σ>0: I varies due to noise" {
  let c = CurrentNoise::custom(4, 100.0F, 20.0F, 0.0F)
  let i_target : Array[Float] = Array::make(4, 0.0F)
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  stimulate_noise(c, i_target, rng)
  // α=0: i_target[k] = (100 + N(0,20)) * 1 = 100 + N(0,20)
  // With 8 draws from rng and σ=20, the seeded sequence can produce
  // values up to ±100 (5σ) for a single unlucky draw. Use loose bounds.
  let mut k : Int = 0
  while k < 4 {
    let finite = i_target[k] == i_target[k]
    assert_eq(finite, true)
    assert_eq(i_target[k] > -50.0F && i_target[k] < 250.0F, true)
    k = k + 1
  }
}

///|
test "stimulate_noise — α=1: I holds previous value (no update)" {
  let c = CurrentNoise::custom(1, 500.0F, 100.0F, 1.0F)
  let i_target : Array[Float] = Array::make(1, 42.0F)
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  stimulate_noise(c, i_target, rng)
  // α=1: i_target = (500 + noise) * 0 + 42 * 1 = 42 (unchanged)
  assert_eq(i_target[0], 42.0F)
}

///|
test "stimulate_noise — α=0.5: 50/50 blend with previous" {
  let c = CurrentNoise::custom(1, 200.0F, 0.0F, 0.5F)
  let i_target : Array[Float] = Array::make(1, 100.0F)
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  stimulate_noise(c, i_target, rng)
  // α=0.5, σ=0: i_target = 200 * 0.5 + 100 * 0.5 = 150
  assert_eq(i_target[0], 150.0F)
}

///|
test "stimulate_noise — 1000 steps stays bounded with decay" {
  let c = CurrentNoise::custom(4, 50.0F, 10.0F, 0.9F)
  let i_target : Array[Float] = Array::make(4, 0.0F)
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  let mut step : Int = 0
  while step < 1000 {
    stimulate_noise(c, i_target, rng)
    step = step + 1
  }
  // α=0.9: low-pass filter, values should converge near I_base = 50
  let mut k : Int = 0
  while k < 4 {
    let finite = i_target[k] == i_target[k]
    assert_eq(finite, true)
    assert_eq(i_target[k] > 30.0F && i_target[k] < 70.0F, true)
    k = k + 1
  }
}

///|
test "stimulate_noise — same seed → same output (deterministic)" {
  let c = CurrentNoise::custom(2, 0.0F, 10.0F, 0.5F)
  let i1 : Array[Float] = Array::make(2, 50.0F)
  let i2 : Array[Float] = Array::make(2, 50.0F)
  let rng1 = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  let rng2 = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0xFEEDFACEUL, 0x12345678UL)
  stimulate_noise(c, i1, rng1)
  stimulate_noise(c, i2, rng2)
  assert_eq(i1[0], i2[0])
  assert_eq(i1[1], i2[1])
}