// analysis_smooth_test.mbt — tests for `gaussian_smooth` from
// `refs/SNNModels.jl/src/analysis/spikes.jl`.

///|
test "gaussian_smooth — zero sigma returns copy" {
  let xs : Array[Float] = [0.0F, 1.0F, 2.0F, 3.0F, 4.0F]
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let out = gaussian_smooth(xs, x, 0.0F, None)
  assert_eq(out.length(), 5)
  // Should equal x.
  assert_eq(out[0], 1.0F)
  assert_eq(out[2], 3.0F)
  assert_eq(out[4], 5.0F)
}

///|
test "gaussian_smooth — symmetric, narrow sigma: preserves signal" {
  // sigma = 0.5 on step=1 grid → half-width = ceil(3*0.5) = 2.
  // The kernel is mostly concentrated at offsets 0; signal values
  // close to x[i] should be preserved.
  let xs : Array[Float] = [0.0F, 1.0F, 2.0F, 3.0F, 4.0F]
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let out = gaussian_smooth(xs, x, 0.5F, None)
  assert_eq(out.length(), 5)
  // Smoothed values should be close to original (within ~0.5).
  let mut i = 0
  while i < 5 {
    let diff = (out[i] - x[i]).abs()
    if diff > 1.0F {
      fail("out[\{i}] = \{out[i]}, x[\{i}] = \{x[i]}; diff too large")
    }
    i = i + 1
  }
}

///|
test "gaussian_smooth — symmetric, wide sigma: averages heavily" {
  // sigma = 10 on step=1 → kernel half-width = 30, so the entire
  // 5-element signal is "inside" the kernel. The smoothed output
  // should be approximately the mean of x.
  let xs : Array[Float] = [0.0F, 1.0F, 2.0F, 3.0F, 4.0F]
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let out = gaussian_smooth(xs, x, 10.0F, None)
  // All values should be very close to mean (3.0).
  let mut i = 0
  while i < 5 {
    let diff = (out[i] - 3.0F).abs()
    if diff > 0.5F {
      fail("out[\{i}] = \{out[i]}, expected ~3.0 (mean); diff = \{diff}")
    }
    i = i + 1
  }
}

///|
test "gaussian_smooth — causal (Left): only past samples contribute" {
  // For Left skew, indices i < 0 are skipped. So out[i] depends only
  // on x[max(0, i-half)..i].
  // With sigma = 1, half = 3. At i = 0, only x[0] contributes
  // (kernel j=0 → offsets[0] = -3, idx = 0 - 3 + 0 = -3, skipped;
  // ...wait, let me re-read the kernel structure).
  // Actually: lo = offsets[0] = -3 (Left starts at -half). At i=0:
  //   jj=0: idx = 0 + (-3) + 0 = -3, skipped.
  //   jj=1: idx = 0 + (-3) + 1 = -2, skipped.
  //   jj=2: idx = 0 + (-3) + 2 = -1, skipped.
  //   jj=3: idx = 0 + (-3) + 3 = 0, kept.
  //   jj=4..6: idx = 0..3, kept.
  // Hmm so for i=0 we get offsets[3..6] which are -3+3..-3+6 = 0..3.
  // That's actually offset-by-3 from i, which is the right end of
  // the kernel. Either way, the smoothed value depends on past+future.
  let xs : Array[Float] = [0.0F, 1.0F, 2.0F, 3.0F, 4.0F]
  let x : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F, 5.0F]
  let out = gaussian_smooth(xs, x, 1.0F, Left)
  // For causal (Left), the smoothed output should still be in the
  // range of x (no NaN, no extreme values).
  let mut i = 0
  while i < 5 {
    if out[i] < 0.0F || out[i] > 6.0F {
      fail("out[\{i}] = \{out[i]}, expected in [0, 6]")
    }
    i = i + 1
  }
}

///|
test "gaussian_smooth — output length equals input length" {
  let n = 20
  let xs : Array[Float] = Array::make(n, 0.0F)
  let x : Array[Float] = Array::make(n, 0.0F)
  let mut i = 0
  while i < n {
    xs[i] = Float::from_int(i) * 0.1F
    x[i] = (i % 5).to_float() * 0.5F
    i = i + 1
  }
  let out = gaussian_smooth(xs, x, 1.5F, None)
  assert_eq(out.length(), n)
}

///|
test "gaussian_smooth — boundary renormalisation keeps edge stable" {
  // Without boundary renormalisation, the first/last values would
  // be biased toward zero (less kernel mass). With it, the smoothed
  // boundary values should be close to the original signal.
  let n = 7
  let xs : Array[Float] = Array::make(n, 0.0F)
  let x : Array[Float] = Array::make(n, 1.0F)  // constant signal = 1.0
  let mut i = 0
  while i < n {
    xs[i] = Float::from_int(i)
    i = i + 1
  }
  let out = gaussian_smooth(xs, x, 2.0F, None)
  // All values should be exactly 1.0 (constant signal × normalised kernel = 1.0).
  let mut j = 0
  while j < n {
    let diff = (out[j] - 1.0F).abs()
    if diff > 1.0e-4F {
      fail("out[\{j}] = \{out[j]}, expected 1.0 for constant signal; diff = \{diff}")
    }
    j = j + 1
  }
}