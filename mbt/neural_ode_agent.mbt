// neural_ode_agent.mbt — NeuralODEAgent: full forward + adjoint
// training step + SGD (v0.92.0).
//
// Wires the v0.89.0 ODEFunc primitive + v0.90.0 ODESolver (Euler or
// Heun) + v0.91.0 adjoint sensitivity into a single training step.
// The agent fits a continuous-depth transformation from initial state
// x(0) to a target final state — useful as a building block for
// normalizing flows, residual-style continuous-time models, and
// time-series prediction.
//
// Scope of v0.92.0:
//   - NeuralODEAgent struct + constructor (wraps ODEFunc + solver +
//     integration window)
//   - neural_ode_forward: full forward — integrate from x0 over
//     [t0, t1] with n_steps; returns final_state + trajectory
//   - neural_ode_train_step: one SGD step — forward + MSE loss +
//     adjoint backward + SGD on ODEFunc weights. Returns
//     (updated_agent, loss).
//
// Reference: Chen et al. 2018 "Neural Ordinary Differential Equations".

///|
/// Choice of ODE solver for the NeuralODEAgent.
pub enum NeuralODESolver {
  Euler
  Heun
}

///|
/// NeuralODEAgent: ODEFunc + integration window + solver choice.
/// Stores the time interval [t0, t1] and step count; ODEFunc owns the
/// parameters.
pub struct NeuralODEAgent {
  func : ODEFunc
  t0 : Float
  t1 : Float
  n_steps : Int
  solver : NeuralODESolver
}

///|
/// Build a fresh NeuralODEAgent with the given ODEFunc and
/// integration window. Default solver: Heun (2nd-order, better
/// accuracy per step).
pub fn NeuralODEAgent::new(
  func : ODEFunc,
  t0 : Float,
  t1 : Float,
  n_steps : Int,
) -> NeuralODEAgent {
  { func, t0, t1, n_steps, solver: Heun }
}

///|
/// Set the solver (Euler or Heun). Returns an updated agent.
pub fn neural_ode_set_solver(
  agent : NeuralODEAgent,
  solver : NeuralODESolver,
) -> NeuralODEAgent {
  { ..agent, solver }
}

///|
/// Full forward: integrate ODEFunc from x0 over [t0, t1] with
/// n_steps using the agent's chosen solver. Returns the final state
/// and the full trajectory (flat [n_steps+1 × state_dim]).
pub fn neural_ode_forward(
  agent : NeuralODEAgent,
  x0 : Array[Float],
) -> (Array[Float], Array[Float]) {
  match agent.solver {
    Euler => ode_euler_solve(
      agent.func, x0, agent.t0, agent.t1, agent.n_steps,
    )
    Heun => ode_heun_solve(
      agent.func, x0, agent.t0, agent.t1, agent.n_steps,
    )
  }
}

///|
/// One full training step: forward + MSE loss + adjoint backward +
/// SGD step on ODEFunc weights. Returns (updated_agent, loss).
pub fn neural_ode_train_step(
  agent : NeuralODEAgent,
  x0 : Array[Float],
  target : Array[Float],
  lr : Float,
) -> (NeuralODEAgent, Float) {
  // 1. Forward.
  let (final_state, trajectory) = neural_ode_forward(agent, x0)
  // 2. Loss (MSE).
  let state_dim = agent.func.state_dim
  let mut sum_sq = 0.0F
  for i in 0..<state_dim {
    let d = final_state[i] - target[i]
    sum_sq = sum_sq + d * d
  }
  let loss = sum_sq / Float::from_int(state_dim)
  // 3. Backward via the adjoint method.
  let dL_dx_t1 = ode_mse_grad_final(final_state, target)
  let (d_w1, d_b1, d_w2, d_b2) = ode_adjoint_gradients(
    agent.func, trajectory, agent.t0, agent.t1, agent.n_steps,
    dL_dx_t1,
  )
  // 4. SGD step on ODEFunc weights via the shared flatten_2d /
  //    sgd_update_arrays helpers.
  let (new_w1, new_b1) = sgd_update_arrays(
    flatten_2d(agent.func.w1), agent.func.b1,
    flatten_2d(d_w1), d_b1, lr,
  )
  let (new_w2, new_b2) = sgd_update_arrays(
    flatten_2d(agent.func.w2), agent.func.b2,
    flatten_2d(d_w2), d_b2, lr,
  )
  let new_func : ODEFunc = {
    ..agent.func,
    w1: unflatten_2d(new_w1, agent.func.hidden_dim, state_dim),
    b1: new_b1,
    w2: unflatten_2d(new_w2, state_dim, agent.func.hidden_dim),
    b2: new_b2,
  }
  let updated : NeuralODEAgent = { ..agent, func: new_func }
  (updated, loss)
}