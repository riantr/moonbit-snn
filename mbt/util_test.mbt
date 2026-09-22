// util_test.mbt — port of `refs/SNNModels.jl/test/utils/util_test.jl`.
//
// Tests cover:
//   - rand_value: length, range, degenerate range
//   - exp32 / exp64 / exp256: approximation accuracy
//   - name / str_name: symbol/string generation with optional suffix
//   - f2l: padding and truncation
//
// MoonBit-specific notes:
//   - Julia's `name(:E, :I)` returns a Symbol; MoonBit doesn't have
//     first-class Symbols, so our port returns String ("E_to_I").
//     Same as Julia's `string(name(:E, :I))` for comparison purposes.

///|
test "rand_value — length matches n" {
  let rng = Xoshiro::default()
  let out = rand_value(100, 1.0F, 5.0F, rng)
  assert_eq(out.length(), 100)
}

///|
test "rand_value — all values in [min, max]" {
  let rng = Xoshiro::default()
  let out = rand_value(50, 1.0F, 5.0F, rng)
  let mut i = 0
  while i < 50 {
    let v = out[i]
    if v < 1.0F || v > 5.0F {
      fail("value out of range: \{v}")
    }
    i = i + 1
  }
}

///|
test "rand_value — handles reversed p1 > p2" {
  let rng = Xoshiro::default()
  let out = rand_value(50, 5.0F, 1.0F, rng)
  let mut i = 0
  while i < 50 {
    let v = out[i]
    if v < 1.0F || v > 5.0F {
      fail("value out of range after swap: \{v}")
    }
    i = i + 1
  }
}

///|
test "rand_value — degenerate p1 == p2 returns all p1" {
  let rng = Xoshiro::default()
  let out = rand_value(10, 3.0F, 3.0F, rng)
  let mut i = 0
  while i < 10 {
    assert_eq(out[i], 3.0F)
    i = i + 1
  }
}

///|
test "exp32(0) == 1.0" {
  let r = exp32(0.0F)
  assert_eq(r, 1.0F)
}

///|
test "exp32(1) ~ exp(1)" {
  let r = exp32(1.0F)
  let target = expf(1.0F)
  let diff = if r > target { r - target } else { target - r }
  if diff > 0.07F {
    fail("exp32(1) too far from exp(1): got \{r}, expected \{target}")
  }
}

///|
test "exp32(-1) ~ exp(-1)" {
  let r = exp32(-1.0F)
  let target = expf(-1.0F)
  let diff = if r > target { r - target } else { target - r }
  if diff > 0.02F {
    fail("exp32(-1) too far from exp(-1): got \{r}, expected \{target}")
  }
}

///|
test "exp32(-20) > 0 (clamped to -10)" {
  let r = exp32(-20.0F)
  if r <= 0.0F {
    fail("exp32(-20) must be > 0 (clamped to -10): got \{r}")
  }
}

///|
test "exp64(0) == 1.0" {
  assert_eq(exp64(0.0F), 1.0F)
}

///|
test "exp256(0) == 1.0" {
  assert_eq(exp256(0.0F), 1.0F)
}

///|
test "name — two-argument form" {
  let r = name2("E", "I")
  assert_eq(r, "E_to_I")
}

///|
test "name — three-argument form with k" {
  let r = name("E", "I", "AMPA")
  assert_eq(r, "E_to_I_AMPA")
}

///|
test "str_name — two-argument form" {
  let r = str_name("E", "I", "")
  assert_eq(r, "E_to_I")
}

///|
test "str_name — three-argument form with k" {
  let r = str_name("E", "I", "AMPA")
  assert_eq(r, "E_to_I_AMPA")
}

///|
test "str_name_single — one-argument form" {
  let r = str_name_single("pre", "")
  assert_eq(r, "pre")
}

///|
test "str_name_single — two-argument form with k" {
  let r = str_name_single("pre", "suffix")
  assert_eq(r, "pre_suffix")
}

///|
test "f2l — pads short string to default 10" {
  let r = f2l_default("test")
  assert_eq(r.length(), 10)
  assert_eq(r, "test      ")
}

///|
test "f2l — pads to custom length" {
  let r = f2l("test", 5)
  assert_eq(r, "test ")
}

///|
test "f2l — truncates long string" {
  let r = f2l("verylongstring", 5)
  assert_eq(r, "veryl")
}

///|
test "f2l — exact length returned unchanged" {
  let r = f2l("1234567890", 10)
  assert_eq(r, "1234567890")
}

///|
test "f2l — pads numeric-looking string" {
  let r = f2l("123", 5)
  assert_eq(r, "123  ")
}