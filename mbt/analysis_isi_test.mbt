// analysis_isi_test.mbt — tests for ISI_CV2 from
// `refs/SNNModels.jl/src/analysis/spikes.jl`.

///|
test "isi_cv2_one — empty spike train returns 0" {
  let st : Array[Float] = []
  assert_eq(isi_cv2_one(st), 0.0F)
}

///|
test "isi_cv2_one — single spike returns 0 (only 1 ISI)" {
  let st : Array[Float] = [100.0F]
  assert_eq(isi_cv2_one(st), 0.0F)
}

///|
test "isi_cv2_one — two spikes return 0 (only 1 ISI, no CV2 pair)" {
  let st : Array[Float] = [100.0F, 200.0F]
  // Only 1 ISI (100); no pair to compute CV2 from.
  assert_eq(isi_cv2_one(st), 0.0F)
}

///|
test "isi_cv2_one — perfectly regular spikes return 0" {
  // 4 spikes at exactly 10 ms intervals → all ISIs = 10.
  // CV2[i] = 2 * |10 - 10| / (10 + 10) = 0.
  let st : Array[Float] = [0.0F, 10.0F, 20.0F, 30.0F]
  assert_eq(isi_cv2_one(st), 0.0F)
}

///|
test "isi_cv2_one — alternating ISIs (10, 20, 10, 20) yield CV2=2/3" {
  // ISIs = [10, 20, 10] (3 ISIs from 4 spikes).
  // CV2[1] = 2 * |20 - 10| / (20 + 10) = 20/30 = 0.6667
  // CV2[2] = 2 * |10 - 20| / (10 + 20) = 20/30 = 0.6667
  // mean = 0.6667.
  let st : Array[Float] = [0.0F, 10.0F, 30.0F, 40.0F]
  let cv2 = isi_cv2_one(st)
  let diff = (cv2 - 2.0F / 3.0F).abs()
  if diff > 1.0e-4F {
    fail("CV2 = \{cv2}, expected ~0.6667 (2/3)")
  }
}

///|
test "isi_cv2_one — irregular spikes yield non-zero CV2" {
  // ISIs = [10, 30, 15] (3 ISIs from 4 spikes).
  // CV2[1] = 2 * |30 - 10| / (30 + 10) = 40/40 = 1.0
  // CV2[2] = 2 * |15 - 30| / (15 + 30) = 30/45 ≈ 0.6667
  // mean = (1.0 + 0.6667) / 2 ≈ 0.8333.
  let st : Array[Float] = [0.0F, 10.0F, 40.0F, 55.0F]
  let cv2 = isi_cv2_one(st)
  if cv2 < 0.5F || cv2 > 1.0F {
    fail("CV2 = \{cv2}, expected ~0.83 (between 0 and 2)")
  }
  // And it must be non-negative.
  if cv2 < 0.0F {
    fail("CV2 should be non-negative, got \{cv2}")
  }
}

///|
test "isi_cv2_one — CV2 bounded by [0, 2]" {
  // CV2 = 2 * |ISI[i] - ISI[i-1]| / (ISI[i] + ISI[i-1]) ≤ 2.
  // Try a pathological case: ISIs = [10, 1000].
  // CV2[1] = 2 * |1000 - 10| / (1000 + 10) = 1980/1010 ≈ 1.96.
  let st : Array[Float] = [0.0F, 10.0F, 1010.0F]
  let cv2 = isi_cv2_one(st)
  if cv2 < 0.0F || cv2 > 2.0F {
    fail("CV2 = \{cv2}, expected in [0, 2]")
  }
}

///|
test "isi_cv2 — per-neuron CV2 with mixed regularity" {
  // 3 neurons:
  //   neuron 0: regular (CV2 = 0)
  //   neuron 1: irregular (CV2 > 0)
  //   neuron 2: silent (empty)
  let st_per : Array[Array[Float]] = [
    [0.0F, 10.0F, 20.0F, 30.0F],  // regular
    [0.0F, 10.0F, 40.0F, 55.0F],  // irregular
    [],                            // silent
  ]
  let cv2s = isi_cv2(st_per)
  assert_eq(cv2s.length(), 3)
  assert_eq(cv2s[0], 0.0F)
  // neuron 1 should have non-zero CV2.
  if cv2s[1] <= 0.0F {
    fail("neuron 1 CV2 = \{cv2s[1]}, expected > 0")
  }
  // neuron 2 silent → 0.
  assert_eq(cv2s[2], 0.0F)
}

///|
test "isi_cv2 — all silent population returns all-zero CV2 array" {
  let st_per : Array[Array[Float]] = [[], [], []]
  let cv2s = isi_cv2(st_per)
  assert_eq(cv2s.length(), 3)
  assert_eq(cv2s[0], 0.0F)
  assert_eq(cv2s[1], 0.0F)
  assert_eq(cv2s[2], 0.0F)
}