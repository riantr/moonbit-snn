// normalizing_flow_model.mbt — NormalizingFlowModel (v0.104.0):
// full flow model with prior + sample generation + density
// estimation.
//
// A normalizing flow (Rezende & Mohamed 2015, Dinh et al. 2016/2018)
// is an invertible transformation f: R^d → R^d such that:
//   z = f(x)    (encoder / inverse flow; maps data to latent)
//   x = f^{-1}(z)  (decoder / forward flow; samples from prior p(z))
//
// Given a prior p(z) (typically a standard Normal), the density of x
// under the model is:
//   log p(x) = log p(z) + log |det ∂f^{-1}/∂x|
//            = log p(z) + log |det J_f|
//
// Reference: Rezende & Mohamed 2015 "Variational Inference with
// Normalizing Flows"; Dinh et al. 2016/2018 (Real NVP / Glow).

///|
/// NormalizingFlowModel: a Glow flow + a standard Normal prior.
/// log p(z) = -0.5 · Σ z_i² - 0.5 · d · log(2π).
pub struct NormalizingFlowModel {
  dim : Int
  flow : Glow
}

///|
/// Build a fresh NormalizingFlowModel with `n_steps` Glow steps.
pub fn NormalizingFlowModel::new(
  dim : Int,
  n_steps : Int,
  seed : UInt64,
) -> NormalizingFlowModel {
  let flow = Glow::new(dim, n_steps, seed)
  { dim, flow }
}

///|
/// Initialize the flow's ActNorms from data (must be called once
/// before the first forward pass for proper Glow initialization).
pub fn normalizing_flow_init_from_data(
  model : NormalizingFlowModel,
  data : Array[Float],
  n : Int,
) -> NormalizingFlowModel {
  let new_flow = glow_init_from_data(model.flow, data, n)
  { ..model, flow: new_flow }
}

///|
/// Forward (decoder): z → x via Glow forward. Samples x by drawing
/// z ~ N(0, I) and mapping through the flow. Returns (x, log_det)
/// where log_det = log |det ∂f/∂z|.
pub fn normalizing_flow_forward(
  model : NormalizingFlowModel,
  z : Array[Float],
) -> (Array[Float], Float) {
  glow_forward(model.flow, z)
}

///|
/// Sample x from the model: draw z ~ N(0, I) and apply Glow forward.
pub fn normalizing_flow_sample(
  model : NormalizingFlowModel,
  rng : Xoshiro,
) -> Array[Float] {
  let z : Array[Float] = Array::make(model.dim, 0.0F)
  for i in 0..<model.dim {
    let (z1, _) = box_muller(rng)
    z[i] = Float::from_double(z1)
  }
  let (x, _) = normalizing_flow_forward(model, z)
  x
}

///|
/// Inverse (encoder): x → z via Glow inverse. Returns (z, log_det)
/// where log_det = log |det ∂z/∂x|.
pub fn normalizing_flow_inverse(
  model : NormalizingFlowModel,
  x : Array[Float],
) -> (Array[Float], Float) {
  let z = glow_inverse(model.flow, x)
  let log_det = glow_total_log_det(model.flow, x)
  (z, log_det)
}

///|
/// Total log |det ∂f/∂x| for the current model + x (used by
/// normalizing_flow_inverse / log_likelihood).
pub fn glow_total_log_det(
  flow : Glow,
  x : Array[Float],
) -> Float {
  // The inverse (x → z) change of variables has log_det = total
  // forward log_det (with sign). For the inverse, log|det| is the
  // same magnitude. We compute it as the sum of per-step log dets
  // for the inverse pass: x → z. Each GlowStep invert has the same
  // log|det| as the forward (since affine couplings and ActNorm are
  // self-inverse under |·|, and 1×1 conv has log|det W| = log|det W|).
  let mut cur = x.copy()
  let mut total_log_det = 0.0F
  // Forward direction: same log det as inverse since these are all
  // sign-symmetric. We use the forward pass for simplicity.
  for i in 0..<flow.n_steps {
    let (y, _) = glow_step_forward(flow.steps[i], cur)
    // Re-compute log det of this step.
    let ld_actnorm = actnorm_log_det(flow.steps[i].actnorm)
    let ld_conv = flow.steps[i].conv.log_det_w
    let ld_coupling = coupling_log_det(flow.steps[i].coupling, y)
    total_log_det = total_log_det + ld_actnorm + ld_conv + ld_coupling
    cur = y
  }
  total_log_det
}

///|
/// Compute the log-likelihood log p(x) under the model.
/// log p(x) = log p(z) + log |det J_f|, with z = f^{-1}(x) and log p(z)
/// the standard Normal log-density.
pub fn normalizing_flow_log_likelihood(
  model : NormalizingFlowModel,
  x : Array[Float],
) -> Float {
  let (z, log_det) = normalizing_flow_inverse(model, x)
  // log p(z) = -0.5 · Σ z_i² - 0.5 · d · log(2π)
  let mut lp_z = -0.5F * Float::from_int(model.dim) * log_2pi_v2()
  for i in 0..<model.dim {
    lp_z = lp_z - 0.5F * z[i] * z[i]
  }
  lp_z + log_det
}

///|
/// Helper: log(2π). Local to this file (scm_log_likelihood already
/// has its own; v0.104.0 keeps them separate to avoid file-deps).
fn log_2pi_v2() -> Float {
  logf(2.0F * 3.14159265F)
}

///|
/// Compute the mean log-likelihood over a batch. Useful as the
/// loss to minimize during training.
pub fn normalizing_flow_mean_log_likelihood(
  model : NormalizingFlowModel,
  data : Array[Float],
  n : Int,
) -> Float {
  if n <= 0 {
    return 0.0F
  }
  let mut sum = 0.0F
  for i in 0..<n {
    let x : Array[Float] = Array::make(model.dim, 0.0F)
    for k in 0..<model.dim {
      x[k] = data[i * model.dim + k]
    }
    sum = sum + normalizing_flow_log_likelihood(model, x)
  }
  sum / Float::from_int(n)
}