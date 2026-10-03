// ebm_trainer.mbt -- EBM trainer via Denoising Score Matching (v0.120.0).
//
// Scope of v0.120.0:
//   - EBMTrainer struct (EBM + learning rate).
//   - ebm_train_step: one Denoising Score Matching (DSM) update:
//       1. Sample z ~ N(0, I).
//       2. x_tilde = x + sigma * z.
//       3. score = -dE/dx_tilde (FD approximation, via langevin_gradient).
//       4. loss = 0.5 * mean_i (score_i + z_i / sigma)^2.
//     BPTT-driven parameter updates are deferred to a follow-up batch.
//   - ebm_train_batch: mean loss over a mini-batch.
//   - ebm_eval_loss: deterministic held-out DSM loss.
//   - ebm_num_params: learnable scalar count.
//
// Reference: Vincent 2011 "A Connection Between Score Matching and
// Denoising Autoencoders".

///|
/// EBMTrainer: thin wrapper pairing an EBM with a learning rate.
pub struct EBMTrainer {
  ebm : EBM
  lr : Float
  sigma : Float
}

///|
/// Build a fresh EBMTrainer. `sigma` is the noise scale for DSM
/// (typical values: 0.1 to 1.0 depending on data scale).
pub fn EBMTrainer::new(ebm : EBM, lr : Float, sigma : Float) -> EBMTrainer {
  { ebm, lr, sigma }
}

///|
/// One DSM training step. Returns the loss. Energy parameters are NOT
/// updated in this version.
pub fn ebm_train_step(
  trainer : EBMTrainer,
  x : Array[Float],
  rng : Xoshiro,
) -> Float {
  let n = x.length()
  let sigma = trainer.sigma
  // Sample z ~ N(0, I) and build x_tilde = x + sigma * z.
  let z : Array[Float] = Array::make(n, 0.0F)
  let pairs = n / 2
  let mut i = 0
  while i < pairs * 2 {
    let (z1d, z2d) = box_muller(rng)
    z[i] = Float::from_double(z1d)
    if i + 1 < n {
      z[i + 1] = Float::from_double(z2d)
    }
    i = i + 2
  }
  if n % 2 == 1 {
    let (z1d, _) = box_muller(rng)
    z[n - 1] = Float::from_double(z1d)
  }
  let x_tilde : Array[Float] = Array::make(n, 0.0F)
  for k in 0..<n {
    x_tilde[k] = x[k] + sigma * z[k]
  }
  // Compute score = -dE/dx_tilde via FD gradient.
  let grad = langevin_gradient(trainer.ebm.net, x_tilde, trainer.ebm.fd_eps)
  // DSM loss: 0.5 * mean_i (score_i + z_i / sigma)^2.
  let mut sum = 0.0F
  for k in 0..<n {
    let target = z[k] / sigma
    let diff = -grad[k] - target
    sum = sum + diff * diff
  }
  let loss = 0.5F * sum / Float::from_int(n)
  let _ = trainer.lr
  loss
}

///|
/// Mean DSM loss over a mini-batch.
pub fn ebm_train_batch(
  trainer : EBMTrainer,
  batch : Array[Array[Float]],
  rng : Xoshiro,
) -> Float {
  let m = batch.length()
  let mut total = 0.0F
  for e in 0..<m {
    total = total + ebm_train_step(trainer, batch[e], rng)
  }
  total / Float::from_int(m)
}

///|
/// Held-out DSM loss: same objective as training, no parameter mutation.
/// Uses a deterministic seed (zero) for reproducibility.
pub fn ebm_eval_loss(
  trainer : EBMTrainer,
  batch : Array[Array[Float]],
) -> Float {
  let rng = Xoshiro::new(0UL)
  let m = batch.length()
  let mut total = 0.0F
  let n = batch[0].length()
  let sigma = trainer.sigma
  // Draw one batch of z (one per example) up-front so eval is reproducible.
  for e in 0..<m {
    let x = batch[e]
    let z : Array[Float] = Array::make(n, 0.0F)
    let pairs = n / 2
    let mut i = 0
    while i < pairs * 2 {
      let (z1d, z2d) = box_muller(rng)
      z[i] = Float::from_double(z1d)
      if i + 1 < n {
        z[i + 1] = Float::from_double(z2d)
      }
      i = i + 2
    }
    if n % 2 == 1 {
      let (z1d, _) = box_muller(rng)
      z[n - 1] = Float::from_double(z1d)
    }
    let x_tilde : Array[Float] = Array::make(n, 0.0F)
    for k in 0..<n {
      x_tilde[k] = x[k] + sigma * z[k]
    }
    let grad = langevin_gradient(trainer.ebm.net, x_tilde, trainer.ebm.fd_eps)
    let mut sum = 0.0F
    for k in 0..<n {
      let target = z[k] / sigma
      let diff = -grad[k] - target
      sum = sum + diff * diff
    }
    total = total + 0.5F * sum / Float::from_int(n)
  }
  total / Float::from_int(m)
}

///|
/// Count learnable scalars in the energy function.
pub fn ebm_num_params(ebm : EBM) -> Int {
  let net = ebm.net
  let mut n_params = 0
  n_params = n_params + net.w1.length() * net.w1[0].length()
  n_params = n_params + net.b1.length()
  for l in 0..<net.hidden_w.length() {
    n_params = n_params + net.hidden_w[l].length() * net.hidden_w[l][0].length()
    n_params = n_params + net.hidden_b[l].length()
  }
  n_params = n_params + net.w_out.length() * net.w_out[0].length()
  n_params = n_params + 1
  n_params
}