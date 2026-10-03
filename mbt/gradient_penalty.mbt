// gradient_penalty.mbt -- WGAN-GP gradient penalty (v0.127.0).
//
// WGAN-GP (Gulrajani et al. 2018 "Improved Training of Wasserstein
// GANs") enforces the 1-Lipschitz constraint on the critic with a
// gradient penalty instead of weight clipping:
//
//   L_GP = E_{x_hat ~ p_interp} [ (|| grad_x f_w(x_hat) ||_2 - 1)^2 ]
//
// where x_hat is a point sampled on the line segment between a real
// sample and a generated sample:
//
//   x_hat = eps * x_real + (1 - eps) * x_fake,  eps ~ U(0, 1)
//
// The gradient grad_x f_w(x_hat) is the gradient of the critic output
// with respect to the input image. As with the EBM finite-difference
// gradient (v0.118.0), the analytic input gradient through the conv
// stack is deferred, so this file uses central finite differences.
//
// Cost: 2 * (c_in * H * W) forward passes per penalty evaluation,
// which is significant at 32x32x3 = 3072. Use a small `fd_eps` and
// consider restricting the penalty to a random pixel subset for
// production use.
//
// Reference: Gulrajani et al. 2018.

///|
/// Sample an interpolation coefficient eps ~ U(0, 1).
pub fn gp_sample_eps(rng : Xoshiro) -> Float {
  next_f32(rng)
}

///|
/// Build the interpolated sample x_hat = eps * real + (1 - eps) * fake.
pub fn gp_interpolate(
  real_image : Array[Float],
  fake_image : Array[Float],
  eps : Float,
) -> Array[Float] {
  let n = real_image.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = eps * real_image[i] + (1.0F - eps) * fake_image[i]
  }
  out
}

///|
/// Central finite-difference input gradient of the critic output with
/// respect to the image. `fd_eps` is the perturbation size.
pub fn gp_input_gradient(
  c : WCritic,
  image : Array[Float],
  fd_eps : Float,
) -> Array[Float] {
  let n = image.length()
  let grad : Array[Float] = Array::make(n, 0.0F)
  let d : DCGANDiscriminator = c.d
  for i in 0..<n {
    let x_plus : Array[Float] = Array::make(n, 0.0F)
    let x_minus : Array[Float] = Array::make(n, 0.0F)
    for k in 0..<n {
      x_plus[k] = image[k]
      x_minus[k] = image[k]
    }
    x_plus[i] = x_plus[i] + fd_eps
    x_minus[i] = x_minus[i] - fd_eps
    let s_plus = dcgan_discriminator_forward(d, x_plus)
    let s_minus = dcgan_discriminator_forward(d, x_minus)
    grad[i] = (s_plus - s_minus) / (2.0F * fd_eps)
  }
  grad
}

///|
/// L2 norm of a gradient vector.
pub fn gp_grad_norm(grad : Array[Float]) -> Float {
  let n = grad.length()
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + grad[i] * grad[i]
  }
  sqrtf(sum)
}

///|
/// The WGAN-GP penalty on one real/fake pair:
///   (|| grad f_w(x_hat) ||_2 - 1)^2
pub fn gradient_penalty(
  c : WCritic,
  real_image : Array[Float],
  fake_image : Array[Float],
  fd_eps : Float,
  rng : Xoshiro,
) -> Float {
  let eps = gp_sample_eps(rng)
  let x_hat = gp_interpolate(real_image, fake_image, eps)
  let grad = gp_input_gradient(c, x_hat, fd_eps)
  let norm = gp_grad_norm(grad)
  let diff = norm - 1.0F
  diff * diff
}

///|
/// The full WGAN-GP critic objective on one real/fake pair:
///   L = E[f_w(real)] - E[f_w(fake)] + lambda * L_GP
/// `lambda` is the gradient-penalty coefficient (the paper uses 10).
pub fn wgan_gp_critic_loss(
  c : WCritic,
  real_image : Array[Float],
  fake_image : Array[Float],
  lambda_gp : Float,
  fd_eps : Float,
  rng : Xoshiro,
) -> Float {
  let w_loss = w_critic_loss(c, real_image, fake_image)
  let gp = gradient_penalty(c, real_image, fake_image, fd_eps, rng)
  w_loss + lambda_gp * gp
}

///|
/// Diagnostic: report the mean gradient norm on a batch of
/// real/fake pairs. A healthy critic at convergence sits near 1.0.
pub fn gradient_penalty_diagnostic(
  c : WCritic,
  real_batch : Array[Array[Float]],
  fake_batch : Array[Array[Float]],
  fd_eps : Float,
  rng : Xoshiro,
) -> (Float, Float) {
  let m = real_batch.length()
  let mut norm_sum = 0.0F
  let mut gp_sum = 0.0F
  for i in 0..<m {
    let eps = gp_sample_eps(rng)
    let x_hat = gp_interpolate(real_batch[i], fake_batch[i], eps)
    let grad = gp_input_gradient(c, x_hat, fd_eps)
    let norm = gp_grad_norm(grad)
    norm_sum = norm_sum + norm
    let diff = norm - 1.0F
    gp_sum = gp_sum + diff * diff
  }
  let n = Float::from_int(m)
  (norm_sum / n, gp_sum / n)
}