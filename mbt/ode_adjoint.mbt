// ode_adjoint.mbt — Adjoint sensitivity method for Neural ODEs
// (v0.91.0).
//
// The adjoint method (Chen et al. 2018) computes dL/dθ without
// backpropagating through the ODE solver — instead it solves another
// ODE backward in time. Given:
//   - Forward trajectory x(t) for t in [t0, t1] (from v0.90.0)
//   - Loss gradient w.r.t. final state: a(t1) = dL/dx(t1)
//   - Jacobian of the vector field: A(t) = ∂f/∂x(x(t), t)
//
// The adjoint a(t) = dL/dx(t) satisfies:
//   da/dt = -A(t)ᵀ · a(t)
// with initial condition a(t1) = dL/dx(t1).
//
// The parameter gradient accumulates as we integrate:
//   dL/dθ = -∫[t1..t0] a(t)ᵀ · ∂f(x(t), t; θ)/∂θ dt
//
// For our MLP ODEFunc (state → tanh(w1·state + b1) → w2·hidden + b2):
//   A = ∂f/∂x = w2 · diag(1 - hidden²) · w1
//   ∂f/∂w1 = (1 - hidden²) · a · stateᵀ (per hidden neuron)
//   ∂f/∂w2 = a · hiddenᵀ
//   ∂f/∂b1 = (1 - hidden²) · a
//   ∂f/∂b2 = a
//
// Scope of v0.91.0:
//   - ode_adjoint_gradients: BPTT-free parameter gradients via the
//     adjoint method (Euler backward integration of the adjoint ODE).
//   - Returns (d_w1, d_b1, d_w2, d_b2) matching the ODEFunc shape.
//
// Reference: Chen et al. 2018 "Neural Ordinary Differential Equations"
// Section 4 (adjoint sensitivity method).

///|
/// Adjoint sensitivity: compute parameter gradients dL/dθ for the
/// ODEFunc via the adjoint method (BPTT-free). The caller provides:
//   - `func`: the ODEFunc to differentiate
///   - `trajectory`: the forward state trajectory flat
///     `[n_steps+1 × state_dim]` from `ode_euler_solve` or
///     `ode_heun_solve`
///   - `t0, t1, n_steps`: same as the forward solver
///   - `dL_dx_t1`: gradient of the loss w.r.t. the final state (length
///     state_dim)
///
/// Returns (d_w1, d_b1, d_w2, d_b2) — gradients matching ODEFunc's
/// weight shapes. Implementation uses Euler integration of the
/// adjoint ODE backward from t1 to t0.
pub fn ode_adjoint_gradients(
  func : ODEFunc,
  trajectory : Array[Float],
  t0 : Float,
  t1 : Float,
  n_steps : Int,
  dL_dx_t1 : Array[Float],
) -> (Array[Array[Float]], Array[Float], Array[Array[Float]], Array[Float]) {
  let state_dim = func.state_dim
  let hidden_dim = func.hidden_dim
  let h = (t1 - t0) / Float::from_int(n_steps)
  // Initialize adjoint at t1 with dL/dx(t1).
  let a : Array[Float] = Array::make(state_dim, 0.0F)
  for i in 0..<state_dim {
    a[i] = dL_dx_t1[i]
  }
  // Accumulate parameter gradients.
  let d_w1 : Array[Array[Float]] = Array::make(
    hidden_dim, Array::make(state_dim, 0.0F),
  )
  let d_b1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let d_w2 : Array[Array[Float]] = Array::make(
    state_dim, Array::make(hidden_dim, 0.0F),
  )
  let d_b2 : Array[Float] = Array::make(state_dim, 0.0F)
  // Helper: compute the cached hidden layer for state[t].
  // Returns hidden[i] = tanh(w1[i] · state + b1[i] + t)
  let cached_hidden : Array[Array[Float]] = Array::make(n_steps + 1, Array::make(hidden_dim, 0.0F))
  for step in 0..<(n_steps + 1) {
    let off = step * state_dim
    let x_t : Array[Float] = Array::make(state_dim, 0.0F)
    for i in 0..<state_dim {
      x_t[i] = trajectory[off + i]
    }
    let t = t0 + Float::from_int(step) * h
    for i in 0..<hidden_dim {
      let mut s = func.b1[i] + t
      for k in 0..<state_dim {
        s = s + func.w1[i][k] * x_t[k]
      }
      cached_hidden[step][i] = tanhf(s)
    }
  }
  // Backward Euler integration of the adjoint ODE.
  for step_back in 0..<n_steps {
    // step_back = 0 → process step n_steps (at t1); step_back = n-1 → step 1.
    let step = n_steps - step_back
    let off = step * state_dim
    let x_t : Array[Float] = Array::make(state_dim, 0.0F)
    for i in 0..<state_dim {
      x_t[i] = trajectory[off + i]
    }
    let hidden = cached_hidden[step]
    // Compute Jacobian-vector product Aᵀ · a = (w2 · diag(1-h²) · w1)ᵀ · a.
    // First: v = w1ᵀ · a (length hidden_dim)
    let v_hidden : Array[Float] = Array::make(hidden_dim, 0.0F)
    for i in 0..<hidden_dim {
      let mut s = 0.0F
      for k in 0..<state_dim {
        s = s + func.w1[i][k] * a[k]
      }
      v_hidden[i] = s
    }
    // Apply diag(1 - hidden²): u = (1 - hidden²) ⊙ v_hidden
    let u_hidden : Array[Float] = Array::make(hidden_dim, 0.0F)
    for i in 0..<hidden_dim {
      let one_minus_h_sq = 1.0F - hidden[i] * hidden[i]
      u_hidden[i] = one_minus_h_sq * v_hidden[i]
    }
    // Apply w2ᵀ: Aᵀ · a = w2ᵀ · u_hidden (length state_dim)
    let a_dot : Array[Float] = Array::make(state_dim, 0.0F)
    for i in 0..<state_dim {
      let mut s = 0.0F
      for k in 0..<hidden_dim {
        s = s + func.w2[i][k] * u_hidden[k]
      }
      a_dot[i] = s
    }
    // Update adjoint: a <- a - h · a_dot (Euler backward step)
    for i in 0..<state_dim {
      a[i] = a[i] - h * a_dot[i]
    }
    // Accumulate parameter gradients: dL/dθ += h · (a(t)ᵀ · ∂f/∂θ)
    // d_w2[i, k] += h · a[i] · hidden[k]
    for i in 0..<state_dim {
      for k in 0..<hidden_dim {
        d_w2[i][k] = d_w2[i][k] + h * a[i] * hidden[k]
      }
      d_b2[i] = d_b2[i] + h * a[i]
    }
    // d_w1[i, k] += h · u_hidden[i] · x_t[k]   (gradient flows through tanh derivative)
    for i in 0..<hidden_dim {
      for k in 0..<state_dim {
        d_w1[i][k] = d_w1[i][k] + h * u_hidden[i] * x_t[k]
      }
      d_b1[i] = d_b1[i] + h * u_hidden[i]
    }
  }
  (d_w1, d_b1, d_w2, d_b2)
}

///|
/// Compute the gradient of MSE loss w.r.t. the final state.
/// `final_pred` is the ODE's final state x(t1), `target` is the
/// ground-truth final state, both length state_dim. Returns
/// dL/dx(t1) = (2 / N) · (final_pred - target).
pub fn ode_mse_grad_final(
  final_pred : Array[Float],
  target : Array[Float],
) -> Array[Float] {
  let n = final_pred.length()
  let grad : Array[Float] = Array::make(n, 0.0F)
  if n <= 0 {
    return grad
  }
  let scale = 2.0F / Float::from_int(n)
  for i in 0..<n {
    grad[i] = scale * (final_pred[i] - target[i])
  }
  grad
}