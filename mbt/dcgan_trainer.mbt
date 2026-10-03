// dcgan_trainer.mbt -- DCGAN trainer (v0.124.0).
//
// Scope of v0.124.0:
//   - DCGANTrainer struct (DCGAN + learning rates + batch_size).
//   - dcgan_train_step: one mini-batch adversarial round; returns the
//     mean (d_loss, g_loss) over the batch.
//   - dcgan_eval: held-out D/G losses plus a realism/accuracy metric.
//   - dcgan_sample_grid_stats: simple summary statistics of generated
//     samples (min / max / mean), useful for monitoring collapse.
//   - dcgan_num_params: total trainable scalars in G + D.
//
// Parameter updates (backward through the conv stacks) are deferred to
// a follow-up batch, consistent with the forward-only pattern used
// across this project.
//
// Reference: Radford et al. 2016.

///|
/// DCGANTrainer: pairs a DCGAN with training hyperparameters.
pub struct DCGANTrainer {
  gan : DCGAN
  lr_g : Float
  lr_d : Float
  batch_size : Int
}

///|
/// Build a fresh DCGANTrainer.
pub fn DCGANTrainer::new(
  gan : DCGAN,
  lr_g : Float,
  lr_d : Float,
  batch_size : Int,
) -> DCGANTrainer {
  { gan, lr_g, lr_d, batch_size }
}

///|
/// One mini-batch adversarial training round. Returns the mean
/// discriminator loss and mean generator loss over the batch.
pub fn dcgan_train_step(
  trainer : DCGANTrainer,
  real_batch : Array[Array[Float]],
  rng : Xoshiro,
) -> (Float, Float) {
  let m = real_batch.length()
  let mut d_total = 0.0F
  let mut g_total = 0.0F
  for i in 0..<m {
    let (dl, gl, _fake) = dcgan_step(trainer.gan, real_batch[i], rng)
    d_total = d_total + dl
    g_total = g_total + gl
  }
  let n = Float::from_int(m)
  (d_total / n, g_total / n)
}

///|
/// Held-out evaluation: mean D loss, mean G loss, and discriminator
/// accuracy (fraction of real-vs-fake predictions that are correct,
/// computed against a 0.5 decision threshold on the sigmoid output).
/// Uses a deterministic seed so the metric is reproducible.
pub fn dcgan_eval(
  trainer : DCGANTrainer,
  held_out : Array[Array[Float]],
) -> (Float, Float, Float) {
  let rng = Xoshiro::new(0UL)
  let m = held_out.length()
  let mut d_total = 0.0F
  let mut g_total = 0.0F
  let mut correct = 0.0F
  for i in 0..<m {
    let (dl, gl, fake) = dcgan_step(trainer.gan, held_out[i], rng)
    d_total = d_total + dl
    g_total = g_total + gl
    // Real should be classified as real (prob > 0.5).
    if dcgan_discriminator_prob(trainer.gan.d, held_out[i]) > 0.5F {
      correct = correct + 1.0F
    }
    // Fake should be classified as fake (prob < 0.5).
    if dcgan_discriminator_prob(trainer.gan.d, fake) < 0.5F {
      correct = correct + 1.0F
    }
  }
  let n = Float::from_int(m)
  (d_total / n, g_total / n, correct / (2.0F * n))
}

///|
/// Summary statistics of generated samples: (min, max, mean). Useful
/// for spotting mode collapse (a collapsed generator produces samples
/// with near-zero variance) and saturation (tanh output pinned at
/// +/-1).
pub fn dcgan_sample_grid_stats(
  gan : DCGAN,
  n_samples : Int,
  rng : Xoshiro,
) -> (Float, Float, Float) {
  let batch = dcgan_generate_batch(gan, n_samples, rng)
  let first = batch[0]
  let mut lo = first[0]
  let mut hi = first[0]
  let mut sum = 0.0F
  let mut count = 0.0F
  for s in 0..<batch.length() {
    let img = batch[s]
    for i in 0..<img.length() {
      if img[i] < lo {
        lo = img[i]
      }
      if img[i] > hi {
        hi = img[i]
      }
      sum = sum + img[i]
      count = count + 1.0F
    }
  }
  (lo, hi, sum / count)
}

///|
/// Total trainable scalars across the generator and the discriminator.
pub fn dcgan_num_params(gan : DCGAN) -> Int {
  dcgan_generator_num_params(gan.g) + dcgan_discriminator_num_params(gan.d)
}