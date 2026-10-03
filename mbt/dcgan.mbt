// dcgan.mbt -- DCGAN composite: generator + discriminator (v0.123.0).
//
// A Generative Adversarial Network (Goodfellow et al. 2014) pairs a
// generator G(z) with a discriminator D(x). The discriminator learns to
// distinguish real images from G's samples; the generator learns to
// fool the discriminator. The adversarial game is:
//
//   min_G max_D  E_{x~data}[log D(x)] + E_{z~N(0,I)}[log(1 - D(G(z)))]
//
// In practice this is implemented as two separate binary cross-entropy
// losses:
//   - D loss:  BCE(D(real), 1) + BCE(D(G(z)), 0)
//   - G loss:  BCE(D(G(z)), 1)
//
// Scope of v0.123.0:
//   - DCGAN struct (generator + discriminator + latent_dim).
//   - bce_with_logits: numerically stable binary cross-entropy.
//   - dcgan_d_loss / dcgan_g_loss: the two objectives.
//   - dcgan_step: one adversarial round (sample z, run G, run D twice).
//
// Reference: Radford et al. 2016 (DCGAN); Goodfellow et al. 2014 (GAN).

///|
/// DCGAN: composite of a generator and a discriminator.
pub struct DCGAN {
  g : DCGANGenerator
  d : DCGANDiscriminator
  latent_dim : Int
}

///|
/// Build a fresh DCGAN.
pub fn DCGAN::new(
  g : DCGANGenerator,
  d : DCGANDiscriminator,
) -> DCGAN {
  { g, d, latent_dim: g.latent_dim }
}

///|
/// Numerically stable binary cross-entropy from logits. For label `y`
/// in {0, 1} the loss is:
///
///   -y * log(sigmoid(x)) - (1 - y) * log(1 - sigmoid(x))
///   = max(x, 0) - x * y + log(1 + exp(-|x|))
///
/// which avoids computing log(0) at either extreme.
pub fn bce_with_logits(x : Float, y : Float) -> Float {
  let ax = if x >= 0.0F { x } else { -x }
  let log_term = logf(1.0F + expf(-ax))
  let max_x = if x > 0.0F { x } else { 0.0F }
  max_x - x * y + log_term
}

///|
/// Draw a latent vector z ~ N(0, I) of length `latent_dim`.
pub fn dcgan_sample_latent(
  latent_dim : Int,
  rng : Xoshiro,
) -> Array[Float] {
  let z : Array[Float] = Array::make(latent_dim, 0.0F)
  let pairs = latent_dim / 2
  let mut i = 0
  while i < pairs * 2 {
    let (z1d, z2d) = box_muller(rng)
    z[i] = Float::from_double(z1d)
    if i + 1 < latent_dim {
      z[i + 1] = Float::from_double(z2d)
    }
    i = i + 2
  }
  if latent_dim % 2 == 1 {
    let (z1d, _) = box_muller(rng)
    z[latent_dim - 1] = Float::from_double(z1d)
  }
  z
}

///|
/// Discriminator loss on one real/fake pair:
///   BCE(D(real), 1) + BCE(D(fake), 0)
pub fn dcgan_d_loss(
  gan : DCGAN,
  real_image : Array[Float],
  fake_image : Array[Float],
) -> Float {
  let d_real = dcgan_discriminator_forward(gan.d, real_image)
  let d_fake = dcgan_discriminator_forward(gan.d, fake_image)
  bce_with_logits(d_real, 1.0F) + bce_with_logits(d_fake, 0.0F)
}

///|
/// Generator loss on one fake image: BCE(D(fake), 1). The generator
/// wants the discriminator to label its output as real.
pub fn dcgan_g_loss(gan : DCGAN, fake_image : Array[Float]) -> Float {
  let d_fake = dcgan_discriminator_forward(gan.d, fake_image)
  bce_with_logits(d_fake, 1.0F)
}

///|
/// One adversarial round on a single real image:
///   1. Sample z ~ N(0, I).
///   2. fake = G(z).
///   3. Compute the D loss and the G loss.
/// Returns (d_loss, g_loss, fake_image). Parameter updates are
/// deferred (the backward pass through the conv stack is a follow-up
/// batch).
pub fn dcgan_step(
  gan : DCGAN,
  real_image : Array[Float],
  rng : Xoshiro,
) -> (Float, Float, Array[Float]) {
  let z = dcgan_sample_latent(gan.latent_dim, rng)
  let fake = dcgan_generator_forward(gan.g, z)
  let dl = dcgan_d_loss(gan, real_image, fake)
  let gl = dcgan_g_loss(gan, fake)
  (dl, gl, fake)
}

///|
/// Generator output for a batch of latent vectors, returned as a list
/// of flattened images.
pub fn dcgan_generate_batch(
  gan : DCGAN,
  batch_size : Int,
  rng : Xoshiro,
) -> Array[Array[Float]] {
  let out : Array[Array[Float]] = Array::make(batch_size, Array::make(0, 0.0F))
  for b in 0..<batch_size {
    let z = dcgan_sample_latent(gan.latent_dim, rng)
    out[b] = dcgan_generator_forward(gan.g, z)
  }
  out
}