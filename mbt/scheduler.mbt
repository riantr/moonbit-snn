// scheduler.mbt — Learning-rate schedulers (v0.18.0 + v0.18.1).
//
// All schedulers expose a small uniform API:
//   - Constructor takes the hyper-parameters (no `step` counter —
//     it's tracked via returned state to avoid MoonBit's "struct fields
//     share memory across functions" pitfall).
//   - `step(s)` returns the current learning rate (pure read).
//   - `next(s)` returns a fresh state with the step counter incremented.
//
// The caller pattern is:
//   for step_idx in 0..<num_steps {
//     let lr = scheduler.step(s)
//     // ... use lr in optimizer.step() ...
//     s = scheduler.next(s)
//   }
//
// Conventions:
//   - `StepLR`     PyTorch convention: lr_t = base_lr * gamma^(t // step_size).
//   - `ExponentialLR`  lr_t = base_lr * gamma^t.
//   - `CosineAnnealingLR`  Loshchilov & Hinton 2017:
//     lr_t = eta_min + 0.5 * (eta_max - eta_min) * (1 + cos(pi * t / T_max)).
//   - `ReduceLROnPlateau` PyTorch convention: drops lr by `factor` when
//     a metric fails to improve by at least `threshold` for `patience`
//     consecutive calls. The metric is "lower is better" (loss-like);
//     callers flip the sign for accuracy-like metrics.

// ---------------------------------------------------------------------------
// StepLR
// ---------------------------------------------------------------------------

///|
/// StepLR state. lr drops by `gamma` every `step_size` calls to `next`.
pub struct StepLR {
  base_lr : Float
  step_size : Int
  gamma : Float
  current_step : Int
}

///|
/// Build a StepLR scheduler.
pub fn StepLR::new(base_lr : Float, step_size : Int, gamma : Float) -> StepLR {
  { base_lr, step_size, gamma, current_step: 0 }
}

///|
/// Compute the current learning rate.
pub fn StepLR::step(self : StepLR) -> Float {
  // Number of completed step intervals.
  let n_drops = self.current_step / self.step_size
  let mut factor = 1.0F
  for _ in 0..<n_drops {
    factor = factor * self.gamma
  }
  self.base_lr * factor
}

///|
/// Increment the step counter; returns a fresh state.
pub fn StepLR::next(self : StepLR) -> StepLR {
  { base_lr: self.base_lr, step_size: self.step_size, gamma: self.gamma,
    current_step: self.current_step + 1 }
}

// ---------------------------------------------------------------------------
// ExponentialLR
// ---------------------------------------------------------------------------

///|
/// ExponentialLR state. lr_t = base_lr * gamma^t.
pub struct ExponentialLR {
  base_lr : Float
  gamma : Float
  current_step : Int
}

///|
/// Build an ExponentialLR scheduler.
pub fn ExponentialLR::new(base_lr : Float, gamma : Float) -> ExponentialLR {
  { base_lr, gamma, current_step: 0 }
}

///|
/// Compute the current learning rate.
pub fn ExponentialLR::step(self : ExponentialLR) -> Float {
  self.base_lr * pow_beta(self.gamma, self.current_step)
}

///|
/// Increment the step counter; returns a fresh state.
pub fn ExponentialLR::next(self : ExponentialLR) -> ExponentialLR {
  { base_lr: self.base_lr, gamma: self.gamma,
    current_step: self.current_step + 1 }
}

// ---------------------------------------------------------------------------
// CosineAnnealingLR
// ---------------------------------------------------------------------------

///|
/// CosineAnnealingLR state. lr follows a half-cosine from eta_max to
/// eta_min over `t_max` steps, then jumps back to eta_max and repeats.
pub struct CosineAnnealingLR {
  eta_max : Float
  eta_min : Float
  t_max : Int
  current_step : Int
}

///|
/// Build a CosineAnnealingLR scheduler.
pub fn CosineAnnealingLR::new(
  eta_max : Float,
  eta_min : Float,
  t_max : Int,
) -> CosineAnnealingLR {
  { eta_max, eta_min, t_max, current_step: 0 }
}

///|
/// Compute the current learning rate.
pub fn CosineAnnealingLR::step(self : CosineAnnealingLR) -> Float {
  // Modulo into the cycle: at t = t_max we wrap back to t = 0.
  let t = self.current_step % self.t_max
  let t_f = Float::from_int(t)
  let t_max_f = Float::from_int(self.t_max)
  let angle = 3.14159265F * t_f / t_max_f
  let c = cos_f32(angle)
  self.eta_min + 0.5F * (self.eta_max - self.eta_min) * (1.0F + c)
}

///|
/// Increment the step counter; returns a fresh state.
pub fn CosineAnnealingLR::next(self : CosineAnnealingLR) -> CosineAnnealingLR {
  { eta_max: self.eta_max, eta_min: self.eta_min, t_max: self.t_max,
    current_step: self.current_step + 1 }
}

///|
/// Float32 cos via libm. Wraps the `cosf` symbol exposed by moonbit's
/// built-in math.
extern "C" fn cosf(x : Float) -> Float = "cosf"

///|
fn cos_f32(x : Float) -> Float {
  cosf(x)
}

// ---------------------------------------------------------------------------
// ReduceLROnPlateau
// ---------------------------------------------------------------------------

///|
/// ReduceLROnPlateau state. Tracks the best metric seen so far and the
/// number of consecutive non-improving steps. lr is multiplied by
/// `factor` when patience runs out.
pub struct ReduceLROnPlateau {
  base_lr : Float
  factor : Float
  patience : Int
  threshold : Float
  best_metric : Float
  num_bad_epochs : Int
  current_lr : Float
}

///|
/// Build a ReduceLROnPlateau scheduler. `base_lr` is the initial lr;
/// `factor` is the decay multiplier applied on plateau (typically 0.1);
/// `patience` is the number of consecutive non-improving steps before
/// decay; `threshold` is the minimum relative improvement (in the
/// metric's own units) to count as "improved".
pub fn ReduceLROnPlateau::new(
  base_lr : Float,
  factor : Float,
  patience : Int,
  threshold : Float,
) -> ReduceLROnPlateau {
  { base_lr, factor, patience, threshold, best_metric: 1000000000.0F,
    num_bad_epochs: 0, current_lr: base_lr }
}

///|
/// Feed the current step's metric to the scheduler. Returns the new lr
/// and the updated state. The metric convention is "lower is better"
/// (loss-like); the metric counts as "improved" if it is at least
/// `threshold` below the previous best.
pub fn ReduceLROnPlateau::step(
  self : ReduceLROnPlateau,
  metric : Float,
) -> (Float, ReduceLROnPlateau) {
  // First call: no comparison yet.
  if self.best_metric == 1000000000.0F {
    return (self.current_lr, {
      base_lr: self.base_lr,
      factor: self.factor,
      patience: self.patience,
      threshold: self.threshold,
      best_metric: metric,
      num_bad_epochs: 0,
      current_lr: self.current_lr,
    })
  }
  // Improved by at least `threshold` -> reset bad counter.
  if metric < self.best_metric - self.threshold {
    return (self.current_lr, {
      base_lr: self.base_lr,
      factor: self.factor,
      patience: self.patience,
      threshold: self.threshold,
      best_metric: metric,
      num_bad_epochs: 0,
      current_lr: self.current_lr,
    })
  }
  // Did not improve enough -> increment bad counter.
  let next_bad = self.num_bad_epochs + 1
  if next_bad > self.patience {
    // Plateau: decay lr, reset counter.
    let new_lr = self.current_lr * self.factor
    return (new_lr, {
      base_lr: self.base_lr,
      factor: self.factor,
      patience: self.patience,
      threshold: self.threshold,
      best_metric: self.best_metric,
      num_bad_epochs: 0,
      current_lr: new_lr,
    })
  }
  // Plateau not yet triggered.
  (self.current_lr, {
    base_lr: self.base_lr,
    factor: self.factor,
    patience: self.patience,
    threshold: self.threshold,
    best_metric: self.best_metric,
    num_bad_epochs: next_bad,
    current_lr: self.current_lr,
  })
}