// ode_solver.mbt — ODE solvers for Neural ODEs (v0.90.0).
//
// Integrators that advance the state x(t) of an ODE dx/dt = f(x, t; θ)
// over a time interval [t0, t1] using a fixed step size `h` and a
// fixed number of `n_steps`. Both solvers return:
//   - the final state x(t1)
//   - the full state trajectory flat `[n_steps+1 × state_dim]` for use
//     by the adjoint method (v0.91.0).
//
// Scope of v0.90.0:
//   - ode_euler_solve: explicit Euler integration (1st order)
//   - ode_heun_solve: Heun / improved Euler / RK2 (2nd order)
//
// Reference: Hairer & Wanner 1991 "Solving Ordinary Differential
// Equations"; Chen et al. 2018 (Neural ODEs) uses these solvers.

///|
/// Explicit Euler integration. Step: x_{n+1} = x_n + h · f(x_n, t_n).
/// O(h) local error, O(h) global error. Cheapest per step but
/// accumulates error quickly for stiff systems.
pub fn ode_euler_solve(
  func : ODEFunc,
  x0 : Array[Float],
  t0 : Float,
  t1 : Float,
  n_steps : Int,
) -> (Array[Float], Array[Float]) {
  let state_dim = func.state_dim
  let trajectory : Array[Float] = Array::make(
    (n_steps + 1) * state_dim, 0.0F,
  )
  // Initial state.
  let x : Array[Float] = Array::make(state_dim, 0.0F)
  for i in 0..<state_dim {
    x[i] = x0[i]
    trajectory[i] = x[i]
  }
  let h = (t1 - t0) / Float::from_int(n_steps)
  let mut t = t0
  for step in 0..<n_steps {
    let dx = ode_func_eval(func, x, t)
    for i in 0..<state_dim {
      x[i] = x[i] + h * dx[i]
    }
    t = t + h
    let off = (step + 1) * state_dim
    for i in 0..<state_dim {
      trajectory[off + i] = x[i]
    }
  }
  (x, trajectory)
}

///|
/// Heun / improved Euler / RK2 integration. Steps:
///   k1 = f(x_n, t_n)
///   k2 = f(x_n + h · k1, t_n + h)
///   x_{n+1} = x_n + (h / 2) · (k1 + k2)
/// O(h²) local error, O(h²) global error. Better accuracy per step than
/// Euler at the cost of 2x the vector-field evaluations.
pub fn ode_heun_solve(
  func : ODEFunc,
  x0 : Array[Float],
  t0 : Float,
  t1 : Float,
  n_steps : Int,
) -> (Array[Float], Array[Float]) {
  let state_dim = func.state_dim
  let trajectory : Array[Float] = Array::make(
    (n_steps + 1) * state_dim, 0.0F,
  )
  let x : Array[Float] = Array::make(state_dim, 0.0F)
  for i in 0..<state_dim {
    x[i] = x0[i]
    trajectory[i] = x[i]
  }
  let h = (t1 - t0) / Float::from_int(n_steps)
  let half_h = h * 0.5F
  let mut t = t0
  for step in 0..<n_steps {
    let k1 = ode_func_eval(func, x, t)
    // Predictor: x_pred = x + h · k1
    let x_pred : Array[Float] = Array::make(state_dim, 0.0F)
    for i in 0..<state_dim {
      x_pred[i] = x[i] + h * k1[i]
    }
    let k2 = ode_func_eval(func, x_pred, t + h)
    // Corrector: x <- x + (h / 2) · (k1 + k2)
    for i in 0..<state_dim {
      x[i] = x[i] + half_h * (k1[i] + k2[i])
    }
    t = t + h
    let off = (step + 1) * state_dim
    for i in 0..<state_dim {
      trajectory[off + i] = x[i]
    }
  }
  (x, trajectory)
}