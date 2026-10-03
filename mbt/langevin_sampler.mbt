// langevin_sampler.mbt -- Langevin dynamics sampler for EBM (v0.118.0).
//
// Langevin Monte Carlo (Parisi 1981; Roberts & Tweedie 1996) generates
// samples from a distribution p(x) ~ exp(-E(x)) by iterating
//   x_{k+1} = x_k - (step_size/2) * grad_x E(x_k) + sqrt(step_size) * z,
// where z ~ N(0, I). The gradient of the energy w.r.t. the input is the
// score of the distribution (with a sign flip).
//
// Because BPTT through the EnergyFunction MLP is deferred to a
// follow-up batch, we approximate the input-gradient with central
// finite differences:
//   dE/dx_i ~= (E(x + eps e_i) - E(x - eps e_i)) / (2 * eps)
// Cost: 2 * input_dim forward passes per gradient call.
//
// Scope of v0.118.0:
//   - langevin_gradient: finite-difference input gradient.
//   - langevin_step: one Langevin update step.
//   - langevin_chain: full chain over n_steps.

///|
/// Central finite-difference gradient of E w.r.t. input x. Returns a
/// fresh array of length `x.length()`. `eps` is the perturbation size
/// (typically 1e-3 to 1e-2).
pub fn langevin_gradient(
  net : EnergyFunction,
  x : Array[Float],
  eps : Float,
) -> Array[Float] {
  let n = x.length()
  let grad : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    let x_plus = copy_with_perturb(x, i, eps)
    let x_minus = copy_with_perturb(x, i, -eps)
    let e_plus = energy_function_predict(net, x_plus)
    let e_minus = energy_function_predict(net, x_minus)
    grad[i] = (e_plus - e_minus) / (2.0F * eps)
  }
  grad
}

///|
/// helper: copy x and add `delta` to x[i].
fn copy_with_perturb(x : Array[Float], i : Int, delta : Float) -> Array[Float] {
  let n = x.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    out[k] = x[k]
  }
  out[i] = out[i] + delta
  out
}

///|
/// One Langevin dynamics step:
///   x_new = x - (step_size / 2) * grad_E(x) + sqrt(step_size) * z
/// `noise_scale` is shorthand for sqrt(step_size). Caller supplies the
/// RNG so the chain is reproducible.
pub fn langevin_step(
  net : EnergyFunction,
  x : Array[Float],
  step_size : Float,
  eps : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let n = x.length()
  let grad = langevin_gradient(net, x, eps)
  let stddev = sqrtf(step_size)
  let noise : Array[Float] = Array::make(n, 0.0F)
  let pairs = n / 2
  let mut i = 0
  while i < pairs * 2 {
    let (z1d, z2d) = box_muller(rng)
    noise[i] = Float::from_double(z1d)
    if i + 1 < n {
      noise[i + 1] = Float::from_double(z2d)
    }
    i = i + 2
  }
  if n % 2 == 1 {
    let (z1d, _) = box_muller(rng)
    noise[n - 1] = Float::from_double(z1d)
  }
  let out : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    out[k] = x[k] - (step_size / 2.0F) * grad[k] + stddev * noise[k]
  }
  out
}

///|
/// Run a full Langevin chain starting from `x0` for `n_steps` steps.
/// Returns the final state.
pub fn langevin_chain(
  net : EnergyFunction,
  x0 : Array[Float],
  n_steps : Int,
  step_size : Float,
  eps : Float,
  rng : Xoshiro,
) -> Array[Float] {
  let mut x = x0
  for _ in 0..<n_steps {
    x = langevin_step(net, x, step_size, eps, rng)
  }
  x
}