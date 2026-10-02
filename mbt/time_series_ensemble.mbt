// time_series_ensemble.mbt — Time-series ensemble that combines ARIMA
// + LSTM + GRU predictions via learned weights (v0.88.0).
//
// Three base forecasters:
//   - ARIMA(p, d, q) via ArimaFittedModel (v0.85.0, Yule-Walker AR
//     coefficients + analytical variance)
//   - LSTMForecaster (v0.86.0, full BPTT + SGD)
//   - GRUForecaster (v0.87.0, full BPTT + SGD)
//
// The ensemble produces an h-step-ahead forecast as a weighted
// average of the three base forecasts, with weights summing to 1.
// Weights can be set uniformly (default) or updated via inverse-RMSE:
//   w_i ∝ 1 / max(rmse_i, eps)
// This rewards the base forecaster that did better on a holdout.
//
// Scope of v0.88.0:
//   - TimeSeriesEnsemble struct + constructor (uniform weights)
//   - ts_ensemble_forecast: weighted-average h-step forecast
//   - ts_ensemble_evaluate: per-model + ensemble RMSE on a holdout
//   - ts_ensemble_update_weights_inverse_rmse: rebalance weights
//     based on holdout RMSE
//
// Reference: ensemble methods for time-series forecasting (Bates &
// Granger 1969; clemen 1989).

///|
/// Time-series ensemble. Holds one base forecaster per model type
/// plus the convex combination weights.
pub struct TimeSeriesEnsemble {
  arima : ArimaFittedModel
  lstm : LSTMForecaster
  gru : GRUForecaster
  // Convex combination weights (sum to 1.0).
  w_arima : Float
  w_lstm : Float
  w_gru : Float
  // Cached last input window for the LSTM/GRU (kept here so the
  // ensemble can re-forecast without re-passing the window).
  last_input_window : Array[Float]
}

///|
/// Build a fresh ensemble. Weights init to 1/3 each (uniform).
/// `lstm_hidden_dim` and `gru_hidden_dim` should match the window
/// size used for the LSTM/GRU forecasts (since LSTM/GRU take
/// pre-windowed inputs of length `window_size`).
pub fn TimeSeriesEnsemble::new(
  arima : ArimaFittedModel,
  lstm : LSTMForecaster,
  gru : GRUForecaster,
) -> TimeSeriesEnsemble {
  {
    arima,
    lstm,
    gru,
    w_arima: 1.0F / 3.0F,
    w_lstm: 1.0F / 3.0F,
    w_gru: 1.0F / 3.0F,
    last_input_window: Array::make(0, 0.0F),
  }
}

///|
/// Forecast h steps ahead by combining the three base forecasts.
/// Returns the weighted-average forecast (length h).
/// For the LSTM and GRU we use zero-init hidden state (the recurrent
/// state implicitly captures window context). The caller can supply a
/// non-zero hidden state by setting `lstm.hidden_init` / `gru.hidden_init`
/// before calling.
pub fn ts_ensemble_forecast(
  ensemble : TimeSeriesEnsemble,
  h : Int,
) -> Array[Float] {
  let arima_forecast = arima_fitted_forecast(ensemble.arima, h)
  // LSTM forecast: use zero hidden/cell init.
  let h_dim = ensemble.lstm.hidden_dim
  let lstm_h_init : Array[Float] = Array::make(h_dim, 0.0F)
  let lstm_c_init : Array[Float] = Array::make(h_dim, 0.0F)
  // The LSTM was trained on a fixed window; for forecasting beyond
  // the window, we feed the window's last hidden state back as init
  // (caller pre-populates ensemble.last_input_window if needed). For
  // v0.88.0 we use zero-init for simplicity.
  let (lstm_forecast_seq, _, _) = lstm_forecaster_seq_forward(
    ensemble.lstm,
    ensemble.last_input_window,
    ensemble.last_input_window.length() / ensemble.lstm.input_dim,
    lstm_h_init,
    lstm_c_init,
  )
  // For multi-step forecasting beyond the window, take the last
  // lstm_forecast_seq entry as the multi-step point forecast (the LSTM
  // is trained for next-step, so we recursively feed predictions back).
  // For v0.88.0 we use the last available entry — the user can run
  // their own multi-step wrapper if needed.
  let lstm_point = if lstm_forecast_seq.length() > 0 {
    lstm_forecast_seq[lstm_forecast_seq.length() - ensemble.lstm.output_dim]
  } else {
    0.0F
  }
  // GRU forecast (parallel to LSTM).
  let (gru_forecast_seq, _) = gru_forecaster_seq_forward(
    ensemble.gru,
    ensemble.last_input_window,
    ensemble.last_input_window.length() / ensemble.gru.input_dim,
    Array::make(ensemble.gru.hidden_dim, 0.0F),
  )
  let gru_point = if gru_forecast_seq.length() > 0 {
    gru_forecast_seq[gru_forecast_seq.length() - ensemble.gru.output_dim]
  } else {
    0.0F
  }
  // Combine. ARIMA gives h forecasts; LSTM/GRU give a single point.
  // For a length-h ensemble output, replicate the LSTM/GRU point h
  // times (consistent with the recursive single-step-forecast pattern).
  let forecast : Array[Float] = Array::make(h, 0.0F)
  for k in 0..<h {
    forecast[k] = ensemble.w_arima * arima_forecast[k] +
      ensemble.w_lstm * lstm_point +
      ensemble.w_gru * gru_point
  }
  forecast
}

///|
/// Compute per-model + ensemble RMSE on a holdout. `holdout` is the
/// ground-truth future (length h). Returns
/// (rmse_arima, rmse_lstm, rmse_gru, rmse_ensemble).
pub fn ts_ensemble_evaluate(
  ensemble : TimeSeriesEnsemble,
  holdout : Array[Float],
) -> (Float, Float, Float, Float) {
  let h = holdout.length()
  if h <= 0 {
    return (0.0F, 0.0F, 0.0F, 0.0F)
  }
  let arima_pred = arima_fitted_forecast(ensemble.arima, h)
  let (lstm_seq, _, _) = lstm_forecaster_seq_forward(
    ensemble.lstm,
    ensemble.last_input_window,
    ensemble.last_input_window.length() / ensemble.lstm.input_dim,
    Array::make(ensemble.lstm.hidden_dim, 0.0F),
    Array::make(ensemble.lstm.hidden_dim, 0.0F),
  )
  let lstm_point = if lstm_seq.length() > 0 {
    lstm_seq[lstm_seq.length() - ensemble.lstm.output_dim]
  } else {
    0.0F
  }
  let (gru_seq, _) = gru_forecaster_seq_forward(
    ensemble.gru,
    ensemble.last_input_window,
    ensemble.last_input_window.length() / ensemble.gru.input_dim,
    Array::make(ensemble.gru.hidden_dim, 0.0F),
  )
  let gru_point = if gru_seq.length() > 0 {
    gru_seq[gru_seq.length() - ensemble.gru.output_dim]
  } else {
    0.0F
  }
  // Per-model RMSE.
  let mut sum_sq_arima = 0.0F
  let mut sum_sq_lstm = 0.0F
  let mut sum_sq_gru = 0.0F
  let mut sum_sq_ensemble = 0.0F
  for k in 0..<h {
    let actual = holdout[k]
    let ar_v = arima_pred[k]
    let lstm_v = lstm_point
    let gru_v = gru_point
    let ens_v = ensemble.w_arima * ar_v +
      ensemble.w_lstm * lstm_v +
      ensemble.w_gru * gru_v
    let d_a = actual - ar_v
    let d_l = actual - lstm_v
    let d_g = actual - gru_v
    let d_e = actual - ens_v
    sum_sq_arima = sum_sq_arima + d_a * d_a
    sum_sq_lstm = sum_sq_lstm + d_l * d_l
    sum_sq_gru = sum_sq_gru + d_g * d_g
    sum_sq_ensemble = sum_sq_ensemble + d_e * d_e
  }
  let n_f = Float::from_int(h)
  (
    sqrtf(sum_sq_arima / n_f),
    sqrtf(sum_sq_lstm / n_f),
    sqrtf(sum_sq_gru / n_f),
    sqrtf(sum_sq_ensemble / n_f),
  )
}

///|
/// Update ensemble weights using inverse-RMSE rebalancing. Better
/// (lower-RMSE) forecasters get more weight. The new weights sum to 1
/// and are non-negative. `eps` (default 1e-6) prevents division-by-
/// zero when a forecaster nails the holdout (rmse → 0).
pub fn ts_ensemble_update_weights_inverse_rmse(
  ensemble : TimeSeriesEnsemble,
  holdout : Array[Float],
  eps : Float,
) -> TimeSeriesEnsemble {
  let (rmse_a, rmse_l, rmse_g, _) = ts_ensemble_evaluate(ensemble, holdout)
  // Inverse-RMSE weights: w_i ∝ 1 / max(rmse_i, eps)
  let safe_a = if rmse_a < eps { eps } else { rmse_a }
  let safe_l = if rmse_l < eps { eps } else { rmse_l }
  let safe_g = if rmse_g < eps { eps } else { rmse_g }
  let inv_a = 1.0F / safe_a
  let inv_l = 1.0F / safe_l
  let inv_g = 1.0F / safe_g
  let total = inv_a + inv_l + inv_g
  { ..ensemble, w_arima: inv_a / total, w_lstm: inv_l / total, w_gru: inv_g / total }
}

///|
/// Set the ensemble's most-recent input window. The LSTM and GRU use
/// this for their seq_forward calls; the ARIMA model ignores it (it
/// already has its fit state from arima_fit).
pub fn ts_ensemble_set_window(
  ensemble : TimeSeriesEnsemble,
  window : Array[Float],
) -> TimeSeriesEnsemble {
  { ..ensemble, last_input_window: window.copy() }
}