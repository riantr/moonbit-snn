// autodiff_demo.mbt — Practical autodiff demonstrations (v0.31.1).
//
// Each demo below uses the `Tape` from `autodiff_tape.mbt` to compute
// gradients for a non-trivial scalar function. These serve both as
// library validation (the gradients match closed-form / finite-
// difference baselines) and as a worked example for future layers
// that want to opt into autodiff instead of hand-rolling backward.
//
// Three demos:
//   1. `demo_polynomial` — f(x) = x³ - 2x + 1, verify df/dx at x = 2.
//   2. `demo_rosenbrock` — classic 2-variable test, verify both grads.
//   3. `demo_ma1_loss` — MA(1) residual-loss gradient w.r.t. θ. This
//      is the killer application: ARIMA's MA(q) backward pass is
//      notoriously fiddly because residuals are defined recursively.
//      Tape handles it automatically.

///|
/// Demo 1: Polynomial f(x) = x³ - 2x + 1. Returns the gradient
/// df/dx at `x_val`. Closed-form: df/dx = 3x² - 2.
pub fn demo_polynomial(x_val : Float) -> Float {
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, sub, mul, .. } = prims
  let x = tape.variable(x_val)
  let x2 = mul(x, x)
  let x3 = mul(x2, x)
  let two_x = mul(constant(2.0F), x)
  let subbed = sub(x3, two_x)
  let result = add(subbed, constant(1.0F))
  ignore(result)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  grads[0]
}

///|
/// Forward-only value of the polynomial f(x) = x³ - 2x + 1. Used as
/// the reference for finite-difference gradient checks (finite_diff
/// needs the *value* function, not the gradient).
pub fn polynomial_value(x_val : Float) -> Float {
  x_val * x_val * x_val - 2.0F * x_val + 1.0F
}

///|
/// Demo 2: Rosenbrock function f(x, y) = (a - x)² + b · (y - x²)².
/// Returns (df/dx, df/dy) at the given (x_val, y_val). Classic test
/// for non-linear optimisation — narrow curved valley.
pub fn demo_rosenbrock(x_val : Float, y_val : Float) -> (Float, Float) {
  let a : Float = 1.0
  let b : Float = 100.0
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, sub, mul, .. } = prims
  let x = tape.variable(x_val)
  let y = tape.variable(y_val)
  // term1 = (a - x)^2
  let diff = sub(constant(a), x)
  let term1 = mul(diff, diff)
  // term2 = b · (y - x²)²
  let x2 = mul(x, x)
  let inner = sub(y, x2)
  let inner_sq = mul(inner, inner)
  let term2 = mul(constant(b), inner_sq)
  // f = term1 + term2
  let f_loc = add(term1, term2)
  ignore(f_loc)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  (grads[0], grads[1])
}

///|
/// Forward-only value of the Rosenbrock function. Used as the
/// reference for finite-difference gradient checks.
pub fn rosenbrock_value(x_val : Float, y_val : Float) -> Float {
  let a : Float = 1.0
  let b : Float = 100.0
  let term1 = (a - x_val) * (a - x_val)
  let inner = y_val - x_val * x_val
  let term2 = b * inner * inner
  term1 + term2
}

///|
/// Demo 3: MA(1) residual-loss gradient w.r.t. θ (the moving-average
/// coefficient). Given a series `y` and an initial θ value, build the
/// tape computing ε[0] = y[0]; ε[t] = y[t] - θ · ε[t-1] for t ≥ 1;
/// loss = Σ ε[t]². Returns dloss/dθ.
///
/// **Why this matters**: the MA backward in classical ARIMA is hard
/// to derive by hand because every residual depends on the previous
/// one, which depends on θ. With Tape, each residual is just a `mul`
/// + `sub` and the chain rule propagates correctly through the loop.
pub fn demo_ma1_loss(
  y : Array[Float],
  theta_init : Float,
) -> Float {
  let tape : Tape[Float] = Tape::new()
  let prims = BasicPrims::on(tape)
  let { add, sub, mul, .. } = prims
  // θ is the only learnable parameter; y values are constants.
  let theta = tape.variable(theta_init)
  // Running residual & loss accumulators (Loc values).
  let mut loss_loc : Loc[Float] = constant(0.0F)
  // ε[0] is initialised to y[0] (no θ dependence).
  let mut prev_eps : Loc[Float] = constant(y[0])
  for t in 1..<y.length() {
    // ε[t] = y[t] - θ · ε[t-1]
    let y_t : Loc[Float] = constant(y[t])
    let theta_eps = mul(theta, prev_eps)
    let eps_t = sub(y_t, theta_eps)
    // loss += ε[t]²
    let eps_sq = mul(eps_t, eps_t)
    loss_loc = add(loss_loc, eps_sq)
    prev_eps = eps_t
  }
  ignore(loss_loc)
  let mem = tape.eval()
  let grads = tape.diff_backward(mem)
  grads[0]
}

///|
/// Compute MA(1) residual loss (forward only, no tape). Used for
/// finite-difference validation of `demo_ma1_loss`.
pub fn ma1_loss_value(y : Array[Float], theta : Float) -> Float {
  let n = y.length()
  if n == 0 {
    return 0.0F
  }
  let mut eps_prev = y[0]
  let mut loss = 0.0F
  for t in 1..<n {
    let eps = y[t] - theta * eps_prev
    loss = loss + eps * eps
    eps_prev = eps
  }
  loss
}

///|
/// Central-difference finite-difference gradient. Used as the
/// reference in tests to validate Tape-computed gradients.
pub fn finite_diff(
  f : (Float) -> Float,
  x : Float,
  h : Float,
) -> Float {
  (f(x + h) - f(x - h)) / (2.0F * h)
}