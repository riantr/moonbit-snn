// ddpm_trainer.mbt -- DDPM trainer (v0.116.0).
//
// Scope of v0.116.0:
//   - DDPMTrainer struct (wraps a DDPM + learning rate).
//   - ddpm_train_step: sample t, q_sample, compute MSE loss, return loss.
//     (BPTT-driven SGD through the ScoreNetwork MLP is deferred to a
//     follow-up batch — the same way v0.107/v0.108 only ship the
//     forward pass + loss for VAE/IWAE.)
//   - ddpm_train_batch: mean loss over a mini-batch.
//   - ddpm_eval_loss: held-out loss (no dropout / no noise bookkeeping
//     beyond what DDPM already does).
//
// Reference: Ho et al. 2020 (training objective).

///|
/// DDPMTrainer: thin wrapper that pairs a DDPM with a learning rate
/// (used by future BPTT-driven variants).
pub struct DDPMTrainer {
  ddpm : DDPM
  lr : Float
}

///|
/// Build a fresh DDPMTrainer.
pub fn DDPMTrainer::new(ddpm : DDPM, lr : Float) -> DDPMTrainer {
  { ddpm, lr }
}

///|
/// One training step:
///   1. Sample a random timestep t in [0, T).
///   2. Run `q_sample_pair(x0, t, ...)` to obtain (x_t, noise).
///   3. Compute the MSE loss between predicted and true noise.
/// Returns the loss. The score network parameters are NOT updated in
/// this version (BPTT is deferred).
pub fn ddpm_train_step(
  trainer : DDPMTrainer,
  x0 : Array[Float],
  rng : Xoshiro,
) -> Float {
  let num_T = trainer.ddpm.schedule.num_steps
  // Sample t uniformly in [0, T-1].
  let t = (next_u64(rng) % num_T.to_uint64()).to_int()
  let (xt, noise) = q_sample_pair(x0, t, trainer.ddpm.schedule, rng)
  // Predict noise from x_t.
  let pred = score_network_predict(trainer.ddpm.net, xt, t)
  let n = trainer.ddpm.input_dim
  let mut sum = 0.0F
  for i in 0..<n {
    let d = pred[i] - noise[i]
    sum = sum + d * d
  }
  let loss = sum / Float::from_int(n)
  // BPTT-driven SGD through the MLP score network is deferred.
  let _ = trainer.lr
  loss
}

///|
/// Mean training loss over a mini-batch.
pub fn ddpm_train_batch(
  trainer : DDPMTrainer,
  batch : Array[Array[Float]],
  rng : Xoshiro,
) -> Float {
  let mut total = 0.0F
  let m = batch.length()
  for e in 0..<m {
    total = total + ddpm_train_step(trainer, batch[e], rng)
  }
  total / Float::from_int(m)
}

///|
/// Held-out evaluation loss: same MSE objective but averaged without
/// any parameter mutation. The seed for the noise is deterministic
/// (zero) so eval is reproducible across calls for a given batch.
pub fn ddpm_eval_loss(
  trainer : DDPMTrainer,
  batch : Array[Array[Float]],
) -> Float {
  let rng = Xoshiro::new(0UL)
  let mut total = 0.0F
  let m = batch.length()
  for e in 0..<m {
    let x0 = batch[e]
    let num_T = trainer.ddpm.schedule.num_steps
    // Use a deterministic t for each example (e % T) so eval is reproducible.
    let t = e % num_T
    let (xt, noise) = q_sample_pair(x0, t, trainer.ddpm.schedule, rng)
    let pred = score_network_predict(trainer.ddpm.net, xt, t)
    let n = trainer.ddpm.input_dim
    let mut sum = 0.0F
    for i in 0..<n {
      let d = pred[i] - noise[i]
      sum = sum + d * d
    }
    total = total + sum / Float::from_int(n)
  }
  total / Float::from_int(m)
}

///|
/// Count learnable scalars in the DDPM (i.e. in the score network).
/// Useful for capacity reporting.
pub fn ddpm_num_params(ddpm : DDPM) -> Int {
  let net = ddpm.net
  let mut total = 0
  total = total + net.w_in.length() * net.w_in[0].length()
  total = total + net.b_in.length()
  for l in 0..<net.hidden_w.length() {
    total = total + net.hidden_w[l].length() * net.hidden_w[l][0].length()
    total = total + net.hidden_b[l].length()
  }
  total = total + net.w_out.length() * net.w_out[0].length()
  total = total + net.b_out.length()
  total
}
