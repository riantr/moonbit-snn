// ode_func.mbt — ODEFunc: dynamics field for Neural ODEs (v0.89.0).
//
// The Neural ODE (Chen et al. 2018) replaces discrete-depth ResNet
// blocks with a continuous-depth ODE:
//
//   dx/dt = f(x, t; θ)        (vector field, parametrized by θ)
//   x(t)  = x(0) + ∫_0^t f(x(s), s; θ) ds
//
// This file ships the `ODEFunc` primitive — a small MLP that maps
// (state, time) → state derivative. The MLP is intentionally shallow
// (1 hidden layer with tanh) to keep the per-step cost low, since the
// ODE solver will call `f` many times per forward pass.
//
// Reference: Chen et al. 2018 "Neural Ordinary Differential Equations".
//
// Scope of v0.89.0:
//   - ODEFunc struct + constructor (MLP w1, b1, w2, b2)
//   - ode_func_eval: forward — given (state, t), returns f(x, t)

///|
/// ODEFunc: a small MLP parameterizing the vector field
/// dx/dt = f(x, t; θ). Two-layer MLP: state → Linear → tanh → Linear →
/// state_derivative. The `t` input is broadcast-added to the hidden
/// layer (time-conditioning).
pub struct ODEFunc {
  state_dim : Int
  hidden_dim : Int
  // Linear1: (hidden_dim × state_dim) + bias of length hidden_dim
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // Linear2: (state_dim × hidden_dim) + bias of length state_dim
  w2 : Array[Array[Float]]
  b2 : Array[Float]
}

///|
/// Build fresh ODEFunc. Weights init via xavier_normal scaled by
/// sqrt(2/state_dim) for the input layer and sqrt(2/hidden_dim) for
/// the output layer. Zero biases.
pub fn ODEFunc::new(
  state_dim : Int,
  hidden_dim : Int,
  seed : UInt64,
) -> ODEFunc {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(state_dim))
  let w1 = xavier_normal(hidden_dim, state_dim, std1, rng1)
  let b1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let std2 = sqrtf(2.0F / Float::from_int(hidden_dim))
  let w2 = xavier_normal(state_dim, hidden_dim, std2, rng2)
  let b2 : Array[Float] = Array::make(state_dim, 0.0F)
  { state_dim, hidden_dim, w1, b1, w2, b2 }
}

///|
/// Evaluate the vector field at (state, t). Returns the state
/// derivative f(state, t). Time `t` is broadcast-added to the hidden
/// layer (single scalar addition — broadcast across hidden_dim).
pub fn ode_func_eval(
  func : ODEFunc,
  state : Array[Float],
  t : Float,
) -> Array[Float] {
  // hidden = tanh(w1 · state + b1 + t)
  let hidden : Array[Float] = Array::make(func.hidden_dim, 0.0F)
  for i in 0..<func.hidden_dim {
    let mut s = func.b1[i] + t
    for k in 0..<func.state_dim {
      s = s + func.w1[i][k] * state[k]
    }
    hidden[i] = tanhf(s)
  }
  // dx/dt = w2 · hidden + b2
  let dx : Array[Float] = Array::make(func.state_dim, 0.0F)
  for i in 0..<func.state_dim {
    let mut s = func.b2[i]
    for k in 0..<func.hidden_dim {
      s = s + func.w2[i][k] * hidden[k]
    }
    dx[i] = s
  }
  dx
}