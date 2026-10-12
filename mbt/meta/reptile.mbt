// reptile.mbt — Reptile (Nichol et al. 2018) meta-update (v0.96.0).
//
// Reptile is the simplest first-order meta-learning algorithm: after
// adapting the meta parameters on a task's training set via SGD, the
// meta parameters are interpolated toward the adapted parameters:
//
//   θ_adapted = θ - inner_lr · ∇_θ L_train(θ)
//   θ_new     = (1 - ε) · θ + ε · θ_adapted
//
// No second-order gradient through the inner adaptation. No
// query-set evaluation needed at the meta-step. Just n_tasks different
// adapted parameters, averaged via the interpolation.
//
// Reptile is competitive with FOMAML on simple task distributions and
// even faster (no outer gradient computation). The interpolation
// step `ε` is the meta-step size; typically 0.1 - 0.5.
//
// Scope of v0.96.0:
//   - reptile_meta_step: one Reptile meta-step — sample n_tasks
//     tasks, adapt on each task's support set via n_inner SGD steps
//     starting from the meta model, then interpolate the meta model
//     toward the AVERAGE adapted parameters.
//
// Reference: Nichol et al. 2018 "On First-Order Meta-Learning
// Algorithms".

///|
/// One Reptile meta-step. Samples a meta-batch, adapts the meta
/// model on each task's support set via n_inner SGD steps, then
/// updates the meta model via:
///   θ <- (1 - ε) · θ + ε · θ_adapted
/// where θ_adapted is averaged across tasks.
///
/// Returns (updated_TaskFamily, mean_inner_loss).
pub fn reptile_meta_step(
  family : TaskFamily,
  n_tasks : Int,
  n_support : Int,
  n_inner : Int,
  inner_lr : Float,
  meta_step_size : Float,
  seed : UInt64,
) -> (TaskFamily, Float) {
  let tasks = sample_meta_batch(family, n_tasks, n_support, n_support, seed)
  // Aggregated adapted parameters (averaged across tasks).
  let meta = family.meta_model
  let in_dim = meta.input_dim
  let out_dim = meta.output_dim
  let h_dim = meta.hidden_dim
  let agg_w1 : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let agg_b1 : Array[Float] = Array::make(h_dim, 0.0F)
  let agg_w2 : Array[Array[Float]] = Array::make(
    out_dim, Array::make(h_dim, 0.0F),
  )
  let agg_b2 : Array[Float] = Array::make(out_dim, 0.0F)
  let mut total_inner_loss = 0.0F
  for i in 0..<n_tasks {
    let task = tasks[i]
    let mut adapted = meta
    let mut inner_loss = 0.0F
    for _step in 0..<n_inner {
      let grad = mlp_model_mse_grad_batch(
        adapted, task.support_inputs, task.support_targets, task.n_support,
      )
      inner_loss = mlp_model_mse_loss(
        adapted, task.support_inputs, task.support_targets, task.n_support,
      )
      adapted = mlp_model_sgd_step(adapted, grad, inner_lr)
    }
    total_inner_loss = total_inner_loss + inner_loss
    // Aggregate adapted parameters.
    for i2 in 0..<h_dim {
      agg_b1[i2] = agg_b1[i2] + adapted.b1[i2]
      for k in 0..<in_dim {
        agg_w1[i2][k] = agg_w1[i2][k] + adapted.w1[i2][k]
      }
    }
    for i2 in 0..<out_dim {
      agg_b2[i2] = agg_b2[i2] + adapted.b2[i2]
      for k in 0..<h_dim {
        agg_w2[i2][k] = agg_w2[i2][k] + adapted.w2[i2][k]
      }
    }
  }
  // Average across tasks.
  let inv_n = 1.0F / Float::from_int(n_tasks)
  for i2 in 0..<h_dim {
    agg_b1[i2] = agg_b1[i2] * inv_n
    for k in 0..<in_dim {
      agg_w1[i2][k] = agg_w1[i2][k] * inv_n
    }
  }
  for i2 in 0..<out_dim {
    agg_b2[i2] = agg_b2[i2] * inv_n
    for k in 0..<h_dim {
      agg_w2[i2][k] = agg_w2[i2][k] * inv_n
    }
  }
  // Interpolation: θ <- (1 - ε) · θ + ε · θ_adapted_avg
  let keep = 1.0F - meta_step_size
  let new_w1 : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let new_b1 : Array[Float] = Array::make(h_dim, 0.0F)
  let new_w2 : Array[Array[Float]] = Array::make(
    out_dim, Array::make(h_dim, 0.0F),
  )
  let new_b2 : Array[Float] = Array::make(out_dim, 0.0F)
  for i2 in 0..<h_dim {
    new_b1[i2] = keep * meta.b1[i2] + meta_step_size * agg_b1[i2]
    for k in 0..<in_dim {
      new_w1[i2][k] = keep * meta.w1[i2][k] +
        meta_step_size * agg_w1[i2][k]
    }
  }
  for i2 in 0..<out_dim {
    new_b2[i2] = keep * meta.b2[i2] + meta_step_size * agg_b2[i2]
    for k in 0..<h_dim {
      new_w2[i2][k] = keep * meta.w2[i2][k] +
        meta_step_size * agg_w2[i2][k]
    }
  }
  let new_meta : MLPModel = {
    input_dim: meta.input_dim,
    hidden_dim: meta.hidden_dim,
    output_dim: meta.output_dim,
    w1: new_w1,
    b1: new_b1,
    w2: new_w2,
    b2: new_b2,
  }
  let new_family = task_family_set_meta_model(family, new_meta)
  let mean_loss = total_inner_loss * inv_n
  (new_family, mean_loss)
}