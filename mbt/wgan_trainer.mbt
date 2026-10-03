// wgan_trainer.mbt -- WGAN-GP trainer (v0.128.0).
//
// WGAN-GP alternates two updates per iteration:
//
//   1. n_critic critic steps (the paper uses n_critic = 5):
//        sample z ~ N(0, I)
//        fake = G(z)
//        L_critic = E[f_w(real)] - E[f_w(fake)] + lambda * L_GP
//        descend on L_critic
//   2. one generator step:
//        sample z ~ N(0, I)
//        L_G = -E[f_w(G(z))]
//        descend on L_G
//
// The extra critic steps keep the critic close to optimal before each
// generator update, which is what makes the Wasserstein objective
// well-behaved in practice.
//
// Scope of v0.128.0:
//   - WGAN struct: generator + critic + latent_dim.
//   - wgan_critic_step: one critic round (Wasserstein + GP loss).
//   - wgan_generator_step: one generator round.
//   - WGANTrainer: n_critic loop over a mini-batch.
//   - wgan_eval: held-out Wasserstein score gap + mean gradient norm.
//   - wgan_num_params: total trainable scalars.
//
// Parameter updates (backward through the conv stacks) are deferred
// to a follow-up batch, consistent with the forward-only pattern used
// across this project.
//
// Reference: Gulrajani et al. 2018 (WGAN-GP).

///|
/// WGAN: composite of a generator and a Wasserstein critic.
pub struct WGAN {
  g : DCGANGenerator
  c : WCritic
  latent_dim : Int
}

///|
/// Build a fresh WGAN.
pub fn WGAN::new(g : DCGANGenerator, c : WCritic) -> WGAN {
  { g, c, latent_dim: g.latent_dim }
}

///|
/// One critic round: sample z, generate a fake image, and evaluate the
/// Wasserstein + gradient-penalty objective. Returns
/// (w_loss, gp_value, fake_image).
pub fn wgan_critic_step(
  wgan : WGAN,
  real_image : Array[Float],
  lambda_gp : Float,
  fd_eps : Float,
  rng : Xoshiro,
) -> (Float, Float, Array[Float]) {
  let z = dcgan_sample_latent(wgan.latent_dim, rng)
  let fake = dcgan_generator_forward(wgan.g, z)
  let w_loss = w_critic_loss(wgan.c, real_image, fake)
  let eps = gp_sample_eps(rng)
  let x_hat = gp_interpolate(real_image, fake, eps)
  let grad = gp_input_gradient(wgan.c, x_hat, fd_eps)
  let norm = gp_grad_norm(grad)
  let diff = norm - 1.0F
  let gp = diff * diff
  let _ = lambda_gp
  (w_loss, gp, fake)
}

///|
/// One generator round: sample z, generate a fake image, and evaluate
/// the generator objective L_G = -E[f_w(G(z))]. Returns
/// (g_loss, fake_image).
pub fn wgan_generator_step(
  wgan : WGAN,
  rng : Xoshiro,
) -> (Float, Array[Float]) {
  let z = dcgan_sample_latent(wgan.latent_dim, rng)
  let fake = dcgan_generator_forward(wgan.g, z)
  let score = w_critic_score(wgan.c, fake)
  (-score, fake)
}

///|
/// WGANTrainer: wraps a WGAN with training hyperparameters.
pub struct WGANTrainer {
  wgan : WGAN
  n_critic : Int
  lambda_gp : Float
  fd_eps : Float
  batch_size : Int
}

///|
/// Build a fresh WGANTrainer. `n_critic` is the number of critic steps
/// per generator step (the paper uses 5); `lambda_gp` is the
/// gradient-penalty coefficient (the paper uses 10).
pub fn WGANTrainer::new(
  wgan : WGAN,
  n_critic : Int,
  lambda_gp : Float,
  fd_eps : Float,
  batch_size : Int,
) -> WGANTrainer {
  { wgan, n_critic, lambda_gp, fd_eps, batch_size }
}

///|
/// One full training iteration on a mini-batch: n_critic critic rounds
/// followed by one generator round. Returns the mean critic loss
/// (Wasserstein + lambda * GP), the mean gradient-penalty value, and
/// the generator loss.
pub fn wgan_train_step(
  trainer : WGANTrainer,
  real_batch : Array[Array[Float]],
  rng : Xoshiro,
) -> (Float, Float, Float) {
  let m = real_batch.length()
  let mut w_total = 0.0F
  let mut gp_total = 0.0F
  let mut g_total = 0.0F
  let n_iter = trainer.n_critic
  for i in 0..<m {
    for _ in 0..<n_iter {
      let (wl, gp, _fake) = wgan_critic_step(
        trainer.wgan,
        real_batch[i],
        trainer.lambda_gp,
        trainer.fd_eps,
        rng,
      )
      w_total = w_total + wl
      gp_total = gp_total + gp
    }
  }
  // One generator round per training iteration.
  for i in 0..<m {
    let (gl, _fake) = wgan_generator_step(trainer.wgan, rng)
    g_total = g_total + gl
  }
  let n = Float::from_int(m)
  let critic_rounds = Float::from_int(n_iter)
  let mean_w = w_total / (n * critic_rounds)
  let mean_gp = gp_total / (n * critic_rounds)
  let mean_g = g_total / n
  let full_critic_loss = mean_w + trainer.lambda_gp * mean_gp
  (full_critic_loss, mean_gp, mean_g)
}

///|
/// Held-out evaluation: the Wasserstein score gap
/// (E[f_w(real)] - E[f_w(fake)]) and the mean gradient norm. A healthy
/// WGAN-GP critic has gradient norm near 1.0. Uses a deterministic seed.
pub fn wgan_eval(
  trainer : WGANTrainer,
  held_out : Array[Array[Float]],
) -> (Float, Float, Float) {
  let rng = Xoshiro::new(0UL)
  let m = held_out.length()
  let mut real_sum = 0.0F
  let mut fake_sum = 0.0F
  let mut norm_sum = 0.0F
  for i in 0..<m {
    let z = dcgan_sample_latent(trainer.wgan.latent_dim, rng)
    let fake = dcgan_generator_forward(trainer.wgan.g, z)
    real_sum = real_sum + w_critic_score(trainer.wgan.c, held_out[i])
    fake_sum = fake_sum + w_critic_score(trainer.wgan.c, fake)
    let eps = gp_sample_eps(rng)
    let x_hat = gp_interpolate(held_out[i], fake, eps)
    let grad = gp_input_gradient(trainer.wgan.c, x_hat, trainer.fd_eps)
    norm_sum = norm_sum + gp_grad_norm(grad)
  }
  let n = Float::from_int(m)
  (real_sum / n, fake_sum / n, norm_sum / n)
}

///|
/// Total trainable scalars across the generator and the critic.
pub fn wgan_num_params(wgan : WGAN) -> Int {
  let critic_params = dcgan_discriminator_num_params(w_critic_disc(wgan.c))
  dcgan_generator_num_params(wgan.g) + critic_params
}
