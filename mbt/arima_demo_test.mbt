// arima_demo_test.mbt — Tests for the ARIMA demo pipeline.

///|
test "arima_synthesize: pure AR(1) without differencing" {
  // ARIMA(1, 0, 0) with phi=0.5 → AR(1) on y directly.
  let y = arima_synthesize(
    50, 1, 0, 0, 0.0F, [0.5F], [], 42UL,
  )
  assert_eq(y.length(), 50)
  // Subsequent values should be autocorrelated.
  // |corr(y[t], y[t-1])| should be around 0.5.
  // We don't strictly check this (depends on noise), just sanity.
}

///|
test "arima_synthesize: ARIMA(0, 1, 0) is a random walk" {
  // ARIMA(0, 1, 0) is a random walk: y[t] = y[t-1] + ε[t].
  let y = arima_synthesize(
    100, 0, 1, 0, 0.0F, [], [], 7UL,
  )
  assert_eq(y.length(), 101) // d=1 adds 1 element
}

///|
test "arima_fit: fit ARIMA(1, 1, 0) on a random walk" {
  // Generate a near-random-walk with small AR(1) drift.
  let y = arima_synthesize(
    200, 1, 1, 0, 0.0F, [0.3F], [], 11UL,
  )
  let fit = arima_fit(y, 1, 1, 0, 200, 0.0001F)
  assert_eq(fit.p, 1)
  assert_eq(fit.d, 1)
  assert_eq(fit.q, 0)
  assert_eq(fit.ar_coeffs.length(), 1)
  assert_eq(fit.ma_thetas.length(), 0)
  // After 1-st differencing, the recovered AR(1) coefficient on the
  // differenced series is NOT the original φ (the relationship is more
  // complex — y[t] = φ·y[t-1] + ε[t] ⇒ z[t] = (φ-1)·y[t-1] + ε[t]).
  // We just verify the fit produces a finite, bounded coefficient.
  if fit.ar_coeffs[0].abs() > 2.0F {
    fail("fitted φ = \{fit.ar_coeffs[0]}, suspiciously large")
  }
}

///|
test "arima_fit: fit ARIMA(1, 0, 1) directly (no differencing)" {
  // ARMA(1, 1) joint fitting via simple gradient descent is
  // notoriously tricky — the loss surface has ridges where the AR
  // and MA gradients partially cancel. We only verify that the fit
  // (a) terminates, (b) produces finite coefficients, and (c) the
  // residual loss decreases from the initial random-coefficient
  // loss.
  let y = arima_synthesize(
    300, 1, 0, 1, 0.0F, [0.5F], [0.3F], 13UL,
  )
  // Compute initial loss with zero coefficients (= no AR/MA model).
  let initial_loss = arma_residual_loss(
    y,
    ArmaParam::new(1, 1, 0.0F, [0.0F], [0.0F]),
  )
  let fit = arima_fit(y, 1, 0, 1, 300, 0.0001F)
  let fitted_loss = arma_residual_loss(
    y,
    ArmaParam::new(1, 1, 0.0F, fit.ar_coeffs, fit.ma_thetas),
  )
  // Fitted loss should be lower than initial (gradient descent
  // decreased the loss).
  if fitted_loss > initial_loss {
    fail(
      "fit didn't reduce loss: initial = \{initial_loss}, fitted = \{fitted_loss}",
    )
  }
  // Coefficients should be finite and bounded.
  if fit.ar_coeffs[0].abs() > 2.0F || fit.ma_thetas[0].abs() > 2.0F {
    fail(
      "coefficients diverged: φ = \{fit.ar_coeffs[0]}, θ = \{fit.ma_thetas[0]}",
    )
  }
}

///|
test "arima_forecast: 1-step forecast is close to next actual" {
  // Generate ARIMA(1, 0, 0) with phi=0.5. Hold out the last 10 points
  // and verify 1-step forecast on each is close to actual.
  let full_y = arima_synthesize(
    200, 1, 0, 0, 0.0F, [0.5F], [], 17UL,
  )
  let n_hold = 10
  let train_end = full_y.length() - n_hold
  let train : Array[Float] = Array::make(train_end, 0.0F)
  for i in 0..<train_end {
    train[i] = full_y[i]
  }
  let fit = arima_fit(train, 1, 0, 0, 200, 0.0001F)
  // Forecast 1 step ahead from end of train
  let fc1 = arima_forecast(train, fit, 1)
  // The actual next value is full_y[train_end]
  if fc1.length() != 1 {
    fail("expected 1 forecast, got \{fc1.length()}")
  }
  // Forecast should be within 2 stdev of actual (loose check).
  let actual = full_y[train_end]
  let err = (fc1[0] - actual).abs()
  if err > 5.0F {
    fail("1-step forecast error = \{err}, suspiciously large")
  }
}

///|
test "arima_forecast: k-step forecast length matches n_ahead" {
  let y = arima_synthesize(
    100, 1, 0, 1, 0.0F, [0.4F], [0.3F], 19UL,
  )
  let fit = arima_fit(y, 1, 0, 1, 100, 0.0001F)
  let fc = arima_forecast(y, fit, 5)
  assert_eq(fc.length(), 5)
  // Forecast should be finite (not NaN / Inf).
  for i in 0..<5 {
    if fc[i] != fc[i] {
      fail("forecast[\{i}] is NaN")
    }
  }
}

///|
test "arima_rmse: zero for identical arrays" {
  let a : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let rmse = arima_rmse(a, a)
  if rmse != 0.0F {
    fail("rmse of identical arrays = \{rmse}, expected 0")
  }
}

///|
test "arima_rmse: matches manual sqrt(mean((a-p)²))" {
  let actual : Array[Float] = [1.0F, 2.0F, 3.0F]
  let predicted : Array[Float] = [2.0F, 4.0F, 6.0F]
  // errors: [-1, -2, -3] → squared: [1, 4, 9] → mean: 14/3 → sqrt
  let expected = sqrtf((1.0F + 4.0F + 9.0F) / 3.0F)
  let rmse = arima_rmse(actual, predicted)
  if (rmse - expected).abs() > 1.0e-4F {
    fail("rmse = \{rmse}, expected ~\{expected}")
  }
}

///|
test "arima_mae: zero for identical arrays" {
  let a : Array[Float] = [1.0F, 2.0F, 3.0F]
  let mae = arima_mae(a, a)
  if mae != 0.0F {
    fail("mae = \{mae}, expected 0")
  }
}

///|
test "arima_evaluate: end-to-end forecast accuracy" {
  // Generate a long ARIMA(1, 0, 0) series, fit on first 80%, evaluate
  /// forecast on the held-out 20%.
  let full_y = arima_synthesize(
    300, 1, 0, 0, 0.0F, [0.7F], [], 23UL,
  )
  let n_hold = 60
  let train_end = full_y.length() - n_hold
  let train : Array[Float] = Array::make(train_end, 0.0F)
  for i in 0..<train_end {
    train[i] = full_y[i]
  }
  let fit = arima_fit(train, 1, 0, 0, 300, 0.0001F)
  let fc = arima_forecast(train, fit, n_hold)
  // Compute MAE on the held-out portion.
  let mut sum_err = 0.0F
  for i in 0..<n_hold {
    sum_err = sum_err + (fc[i] - full_y[train_end + i]).abs()
  }
  let mae = sum_err / Float::from_int(n_hold)
  // MAE should be < the variance of the noise (which is ~1 for
  // standard normal). Loose bound for robustness.
  if mae > 1.5F {
    fail("forecast MAE = \{mae}, too large for n=300 AR(1)")
  }
}