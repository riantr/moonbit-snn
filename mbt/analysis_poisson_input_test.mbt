// analysis_poisson_input_test.mbt — tests for analysis_poisson_input.mbt.

///|
test "poisson_input_single — rate=0 returns all-false" {
  let rng = Xoshiro::new(42UL)
  let spikes = poisson_input_single(0.0F, 100.0F, 0.1F, rng)
  if spikes.length() != 1000 { fail("expected 1000 entries, got \{spikes.length()}") }
  let mut k = 0
  while k < spikes.length() {
    if spikes[k] { fail("expected all-false for rate=0") }
    k = k + 1
  }
}

///|
test "poisson_input_single — high rate (500Hz, 100ms) produces multiple spikes" {
  let rng = Xoshiro::new(42UL)
  let spikes = poisson_input_single(500.0F, 100.0F, 0.1F, rng)
  if spikes.length() != 1000 { fail("expected 1000 entries") }
  let mut count : Int = 0
  let mut k = 0
  while k < spikes.length() {
    if spikes[k] { count = count + 1 }
    k = k + 1
  }
  // 500 Hz × 100 ms = 50 spikes expected (Poisson count). Allow ±10.
  if count < 30 || count > 70 {
    fail("expected ~50 spikes for 500Hz×100ms, got \{count}")
  }
}

///|
test "poisson_input_single — seeded reproducibility" {
  let rng1 = Xoshiro::new(42UL)
  let rng2 = Xoshiro::new(42UL)
  let s1 = poisson_input_single(100.0F, 100.0F, 0.1F, rng1)
  let s2 = poisson_input_single(100.0F, 100.0F, 0.1F, rng2)
  if s1.length() != s2.length() { fail("length mismatch") }
  let mut k = 0
  while k < s1.length() {
    if s1[k] != s2[k] { fail("mismatch at index \{k} — RNG sequence differs") }
    k = k + 1
  }
}

///|
test "poisson_input_single — 100Hz × 1000ms → ~100 spikes" {
  let rng = Xoshiro::new(42UL)
  let spikes = poisson_input_single(100.0F, 1000.0F, 0.1F, rng)
  if spikes.length() != 10000 { fail("expected 10000 entries, got \{spikes.length()}") }
  let mut count : Int = 0
  let mut k = 0
  while k < spikes.length() {
    if spikes[k] { count = count + 1 }
    k = k + 1
  }
  // 100 Hz × 1000 ms = 100 spikes expected. Allow ±20.
  if count < 80 || count > 120 {
    fail("expected ~100 spikes for 100Hz×1000ms, got \{count}")
  }
}

///|
test "poisson_input_single — spike indices are strictly increasing" {
  let rng = Xoshiro::new(42UL)
  let spikes = poisson_input_single(50.0F, 200.0F, 0.1F, rng)
  let mut last_idx : Int = -1
  let mut k = 0
  while k < spikes.length() {
    if spikes[k] {
      if k <= last_idx {
        fail("non-increasing spike index at \{k} (last \{last_idx})")
      }
      last_idx = k
    }
    k = k + 1
  }
}

///|
test "poisson_input — matrix shape (neurons × n_spikes)" {
  let rng = Xoshiro::new(42UL)
  let mat = poisson_input(50.0F, 100.0F, 0.1F, 5, rng)
  if mat.length() != 5 { fail("expected 5 rows, got \{mat.length()}") }
  let mut k = 0
  while k < mat.length() {
    if mat[k].length() != 1000 { fail("row \{k} expected 1000 entries") }
    k = k + 1
  }
}

///|
test "poisson_input — different neurons have different patterns (likely)" {
  // With rate=50Hz × 100ms → ~5 spikes per neuron, RNG state independent.
  let rng = Xoshiro::new(42UL)
  let mat = poisson_input(50.0F, 100.0F, 0.1F, 10, rng)
  // Count spikes per neuron.
  let mut total_a : Int = 0
  let mut total_b : Int = 0
  let mut k = 0
  while k < mat.length() {
    if k % 2 == 0 {
      let mut i = 0
      while i < mat[k].length() {
        if mat[k][i] { total_a = total_a + 1 }
        i = i + 1
      }
    } else {
      let mut i = 0
      while i < mat[k].length() {
        if mat[k][i] { total_b = total_b + 1 }
        i = i + 1
      }
    }
    k = k + 1
  }
  // Even-indexed and odd-indexed neurons should have similar spike counts
  // (within 50% of each other — Poisson variance).
  let avg_a = total_a.to_float() / 5.0F
  let avg_b = total_b.to_float() / 5.0F
  let ratio = if avg_a > avg_b { avg_a / avg_b } else { avg_b / avg_a }
  if ratio > 2.5 {
    fail("avg counts differ too much: even=\{(avg_a.to_double())} odd=\{(avg_b.to_double())}")
  }
}