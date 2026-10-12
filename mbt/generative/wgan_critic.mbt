// wgan_critic.mbt -- Wasserstein critic for WGAN (v0.126.0).
//
// WGAN (Arjovsky et al. 2017 "Wasserstein GAN") replaces the
// discriminator's binary classification loss with a Wasserstein
// distance estimate. Rather than classifying real/fake, the critic
// learns a 1-Lipschitz function f_w that scores real samples high and
// fake samples low, and the loss becomes:
//
//   L_critic = E_{x~data}[f_w(x)] - E_{z~N(0,I)}[f_w(G(z))]
//
// The critic is *minimised* (unlike a discriminator, which is
// maximised). The generator then minimises -E[f_w(G(z))], i.e. pushes
// its samples toward higher critic scores.
//
// The 1-Lipschitz constraint is enforced either by weight clipping
// (WGAN) or by a gradient penalty (WGAN-GP, Gulrajani et al. 2018);
// both are shipped in this project.
//
// Scope of v0.126.0:
//   - WCritic struct: wraps a DCGANDiscriminator as a critic.
//   - w_critic_score: raw critic output f_w(x) for one image.
//   - w_critic_score_batch: critic outputs for a batch.
//   - w_critic_loss: E[real] - E[fake] on one real/fake pair.
//   - w_critic_clip_weights: in-place Lipschitz weight clipping.
//
// Reference: Arjovsky et al. 2017; Gulrajani et al. 2018.

///|
/// WCritic: a Wasserstein critic. Structurally identical to a
/// DCGANDiscriminator (same conv stack) but trained with a different
/// objective, so it is modelled as a thin wrapper rather than a new
/// conv stack.
pub struct WCritic {
  d : DCGANDiscriminator
}

///|
/// Build a fresh WCritic from an existing discriminator-shaped stack.
pub fn WCritic::new(d : DCGANDiscriminator) -> WCritic {
  { d, }
}

///|
/// Raw critic score f_w(x). In WGAN this is a real-valued score (not a
/// probability), so we bypass the sigmoid that the discriminator uses.
pub fn w_critic_score(c : WCritic, image : Array[Float]) -> Float {
  dcgan_discriminator_forward(c.d, image)
}

///|
/// Critic scores for a batch of images.
pub fn w_critic_score_batch(
  c : WCritic,
  batch : Array[Array[Float]],
) -> Array[Float] {
  let m = batch.length()
  let out : Array[Float] = Array::make(m, 0.0F)
  for i in 0..<m {
    out[i] = w_critic_score(c, batch[i])
  }
  out
}

///|
/// Wasserstein critic loss on one real/fake pair:
///   L = E[f_w(real)] - E[f_w(fake)]
/// The critic minimises this; the generator maximises the second term.
pub fn w_critic_loss(
  c : WCritic,
  real_image : Array[Float],
  fake_image : Array[Float],
) -> Float {
  w_critic_score(c, real_image) - w_critic_score(c, fake_image)
}

///|
/// In-place weight clipping to enforce the 1-Lipschitz constraint of
/// the critic (the original WGAN approach; WGAN-GP uses a gradient
/// penalty instead). Clips every weight and bias to [-clip, clip].
pub fn w_critic_clip_weights(c : WCritic, clip : Float) -> Unit {
  let lo = -clip
  let hi = clip
  // Conv 1.
  for o in 0..<c.d.conv1_w.length() {
    for i in 0..<c.d.conv1_w[o].length() {
      let v = c.d.conv1_w[o][i]
      c.d.conv1_w[o][i] = if v < lo { lo } else { if v > hi { hi } else { v } }
    }
  }
  for i in 0..<c.d.conv1_b.length() {
    let v = c.d.conv1_b[i]
    c.d.conv1_b[i] = if v < lo { lo } else { if v > hi { hi } else { v } }
  }
  // Conv 2.
  for o in 0..<c.d.conv2_w.length() {
    for i in 0..<c.d.conv2_w[o].length() {
      let v = c.d.conv2_w[o][i]
      c.d.conv2_w[o][i] = if v < lo { lo } else { if v > hi { hi } else { v } }
    }
  }
  for i in 0..<c.d.conv2_b.length() {
    let v = c.d.conv2_b[i]
    c.d.conv2_b[i] = if v < lo { lo } else { if v > hi { hi } else { v } }
  }
  // Conv 3.
  for o in 0..<c.d.conv3_w.length() {
    for i in 0..<c.d.conv3_w[o].length() {
      let v = c.d.conv3_w[o][i]
      c.d.conv3_w[o][i] = if v < lo { lo } else { if v > hi { hi } else { v } }
    }
  }
  for i in 0..<c.d.conv3_b.length() {
    let v = c.d.conv3_b[i]
    c.d.conv3_b[i] = if v < lo { lo } else { if v > hi { hi } else { v } }
  }
  // Classifier.
  for k in 0..<c.d.fc_w[0].length() {
    let v = c.d.fc_w[0][k]
    c.d.fc_w[0][k] = if v < lo { lo } else { if v > hi { hi } else { v } }
  }
  c.d.fc_b = if c.d.fc_b < lo {
    lo
  } else {
    if c.d.fc_b > hi { hi } else { c.d.fc_b }
  }
}

///|
/// Access the underlying discriminator (for sampling / param counting).
pub fn w_critic_disc(c : WCritic) -> DCGANDiscriminator {
  c.d
}
