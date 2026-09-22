// Tests for the Xoshiro RNG.
//
// These tests are smoke tests; bit-exact verification against Julia
// requires a Julia runtime. The test vectors are documented in
// rng.mbt (KNOWN GAP for integer seeds).

///|
test "next_u64 advances state" {
  let r = Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let _ = next_u64(r)
  // State must have changed.
  if r.s0 == 1UL && r.s1 == 2UL && r.s2 == 3UL && r.s3 == 4UL {
    fail("state did not advance")
  }
}

///|
test "next_f32 returns Float32 in [0, 1)" {
  let r = Xoshiro::from_state(0xDEADBEEFCAFEBABEUL, 0x1234567890ABCDEFUL, 0xFEDCBA0987654321UL, 0xABCDEF0123456789UL)
  for i in 0..<100 {
    let v = next_f32(r)
    if v < 0.0F || v >= 1.0F {
      fail("next_f32 out of range: \{v}")
    }
  }
}

///|
test "rand_f32 produces n values" {
  let r = Xoshiro::default()
  let arr = rand_f32(r, 5)
  assert_eq(arr.length(), 5)
  // Each value must be in [0, 1)
  for i in 0..<5 {
    let v = arr[i]
    if v < 0.0F || v >= 1.0F {
      fail("rand_f32[\{i}] out of range: \{v}")
    }
  }
}

///|
test "two Xoshiro with same seed produce same sequence" {
  let r1 = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  let r2 = Xoshiro::from_state(7UL, 11UL, 13UL, 17UL)
  for _ in 0..<10 {
    let a = next_f64(r1)
    let b = next_f64(r2)
    assert_eq(a, b)
  }
}

///|
test "Xoshiro::new is deterministic" {
  let r1 = Xoshiro::new(42UL)
  let r2 = Xoshiro::new(42UL)
  for _ in 0..<20 {
    let a = next_f32(r1)
    let b = next_f32(r2)
    assert_eq(a, b)
  }
}
