// maml.mbt — MAML (Model-Agnostic Meta-Learning) inner/outer loop
// meta-update (v0.94.0).
//
// MAML (Finn et al. 2017) learns an initialization θ such that one or
// a few gradient steps on a new task's support set yields good
// performance on the query set:
//
//   For each meta-batch:
//     for each task T_i:
//       # Inner loop: adapt on T_i's support set
//       θ'_i = θ - α · ∇_θ L_sup(θ; T_i)        (one or more steps)
//     # Outer loop: meta-update θ using query losses at θ'_i
//     θ <- θ - β · ∇_θ Σ_i L_query(θ'_i; T_i)
//
// The full second-order MAML propagates gradients through the inner
// adaptation (chain rule through the inner SGD step). For v0.94.0 we
// use the FOMAML approximation — treat θ'_i as independent of θ for
// the outer gradient — which is the canonical practical variant used
// in most MAML implementations on small models.
//
// Scope of v0.94.0:
//   - mlp_model_mse_loss: per-batch MSE loss on a (model, inputs,
//     targets) tuple
//   - mlp_model_mse_grad_per_sample: per-sample analytic gradients of
//     MSE w.r.t. model weights (returned as gradient tuples)
//   - mlp_model_sgd_step: one SGD step on the model weights
//   - maml_meta_step: one MAML meta-step (inner loop on support +
//     outer FOMAML update on query)
//
// Reference: Finn et al. 2017 "Model-Agnostic Meta-Learning for Fast
// Adaptation of Deep Networks".

///|
/// Compute per-batch MSE loss for an MLPModel. `inputs` flat
/// `[batch × input_dim]`, `targets` flat `[batch × output_dim]`.
/// Returns scalar (1/N) · Σ (pred - target)² averaged over the batch.
pub fn mlp_model_mse_loss(
  model : MLPModel,
  inputs : Array[Float],
  targets : Array[Float],
  batch : Int,
) -> Float {
  if batch <= 0 {
    return 0.0F
  }
  let in_dim = model.input_dim
  let out_dim = model.output_dim
  let mut sum_sq = 0.0F
  for n in 0..<batch {
    let inp : Array[Float] = Array::make(in_dim, 0.0F)
    for k in 0..<in_dim {
      inp[k] = inputs[n * in_dim + k]
    }
    let target_off = n * out_dim
    let pred = mlp_model_forward(model, inp)
    for k in 0..<out_dim {
      let d = pred[k] - targets[target_off + k]
      sum_sq = sum_sq + d * d
    }
  }
  sum_sq / Float::from_int(batch)
}

///|
/// Tuple of MLPModel gradients (d_w1, d_b1, d_w2, d_b2). Used by both
/// the inner-loop SGD and the outer-loop meta-update.
pub struct MLPGrad {
  d_w1 : Array[Array[Float]]
  d_b1 : Array[Float]
  d_w2 : Array[Array[Float]]
  d_b2 : Array[Float]
}

///|
/// Compute analytic gradients of MSE loss w.r.t. model weights,
/// batched. For each sample in the batch:
///
///   d_loss/d_w2[i,k] += (2/N) · (pred - target)[i] · hidden[k]
///   d_loss/d_b2[i]   += (2/N) · (pred - target)[i]
///   d_loss/d_w1[i,k] += (2/N) · (1 - hidden²)[i] · (Σ_j d_out[j][i] · w2[j][i]) · input[k]
///   d_loss/d_b1[i]   += (2/N) · (1 - hidden²)[i] · (Σ_j d_out[j][i] · w2[j][i])
///
/// where d_out[i] = (pred[i] - target[i]).
pub fn mlp_model_mse_grad_batch(
  model : MLPModel,
  inputs : Array[Float],
  targets : Array[Float],
  batch : Int,
) -> MLPGrad {
  let in_dim = model.input_dim
  let out_dim = model.output_dim
  let h_dim = model.hidden_dim
  let d_w1 : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let d_b1 : Array[Float] = Array::make(h_dim, 0.0F)
  let d_w2 : Array[Array[Float]] = Array::make(
    out_dim, Array::make(h_dim, 0.0F),
  )
  let d_b2 : Array[Float] = Array::make(out_dim, 0.0F)
  if batch <= 0 {
    return { d_w1, d_b1, d_w2, d_b2 }
  }
  let scale = 2.0F / Float::from_int(batch)
  for n in 0..<batch {
    let inp : Array[Float] = Array::make(in_dim, 0.0F)
    for k in 0..<in_dim {
      inp[k] = inputs[n * in_dim + k]
    }
    let target_off = n * out_dim
    // Forward pass.
    let hidden : Array[Float] = Array::make(h_dim, 0.0F)
    for i in 0..<h_dim {
      let mut s = model.b1[i]
      for k in 0..<in_dim {
        s = s + model.w1[i][k] * inp[k]
      }
      hidden[i] = tanhf(s)
    }
    let pred : Array[Float] = Array::make(out_dim, 0.0F)
    for i in 0..<out_dim {
      let mut s = model.b2[i]
      for k in 0..<h_dim {
        s = s + model.w2[i][k] * hidden[k]
      }
      pred[i] = s
    }
    // d_out[i] = (pred[i] - target[i])
    let d_out : Array[Float] = Array::make(out_dim, 0.0F)
    for i in 0..<out_dim {
      d_out[i] = pred[i] - targets[target_off + i]
    }
    // Output layer gradients.
    for i in 0..<out_dim {
      let g = scale * d_out[i]
      d_b2[i] = d_b2[i] + g
      for k in 0..<h_dim {
        d_w2[i][k] = d_w2[i][k] + g * hidden[k]
      }
    }
    // d_hidden[i] = (1 - hidden[i]²) · Σ_j d_out[j] · w2[j][i]
    let d_hidden : Array[Float] = Array::make(h_dim, 0.0F)
    for i in 0..<h_dim {
      let mut s = 0.0F
      for j in 0..<out_dim {
        s = s + d_out[j] * model.w2[j][i]
      }
      let one_minus_h_sq = 1.0F - hidden[i] * hidden[i]
      d_hidden[i] = scale * one_minus_h_sq * s
    }
    // Input layer gradients.
    for i in 0..<h_dim {
      d_b1[i] = d_b1[i] + d_hidden[i]
      for k in 0..<in_dim {
        d_w1[i][k] = d_w1[i][k] + d_hidden[i] * inp[k]
      }
    }
  }
  { d_w1, d_b1, d_w2, d_b2 }
}

///|
/// One SGD step on MLPModel weights.
pub fn mlp_model_sgd_step(
  model : MLPModel,
  grad : MLPGrad,
  lr : Float,
) -> MLPModel {
  let (new_w1, new_b1) = sgd_update_arrays(
    flatten_2d(model.w1), model.b1, flatten_2d(grad.d_w1), grad.d_b1,
    lr,
  )
  let (new_w2, new_b2) = sgd_update_arrays(
    flatten_2d(model.w2), model.b2, flatten_2d(grad.d_w2), grad.d_b2,
    lr,
  )
  {
    ..model,
    w1: unflatten_2d(new_w1, model.hidden_dim, model.input_dim),
    b1: new_b1,
    w2: unflatten_2d(new_w2, model.output_dim, model.hidden_dim),
    b2: new_b2,
  }
}

///|
/// One MAML meta-step. Samples a meta-batch of tasks, runs `n_inner`
/// SGD steps on each task's support set starting from the meta model,
// / then computes the FOMAML meta-gradient (treats the adapted θ'_i
/// as independent of θ). Applies the meta-update to the family's meta
/// model. Returns the updated TaskFamily + the mean meta-loss.
///
/// FOMAML approximation: instead of propagating gradients through the
/// inner adaptation (full second-order MAML), we compute the gradient
/// of L_query at the adapted point and apply it directly to θ. This
/// is the canonical practical MAML variant for small models; full
/// second-order requires either autograd or finite-difference Hessian
/// (deferred).
pub fn maml_meta_step(
  family : TaskFamily,
  n_tasks : Int,
  n_support : Int,
  n_query : Int,
  n_inner : Int,
  inner_lr : Float,
  outer_lr : Float,
  seed : UInt64,
) -> (TaskFamily, Float) {
  let tasks = sample_meta_batch(family, n_tasks, n_support, n_query, seed)
  // Aggregated outer gradient (FOMAML — sum of adapted-model query
  // gradients, treated as if they were gradients w.r.t. θ).
  let meta = family.meta_model
  let in_dim = meta.input_dim
  let out_dim = meta.output_dim
  let h_dim = meta.hidden_dim
  let agg_d_w1 : Array[Array[Float]] = Array::make(
    h_dim, Array::make(in_dim, 0.0F),
  )
  let agg_d_b1 : Array[Float] = Array::make(h_dim, 0.0F)
  let agg_d_w2 : Array[Array[Float]] = Array::make(
    out_dim, Array::make(h_dim, 0.0F),
  )
  let agg_d_b2 : Array[Float] = Array::make(out_dim, 0.0F)
  let mut total_loss = 0.0F
  for i in 0..<n_tasks {
    let task = tasks[i]
    // Inner loop: adapt the meta model on the support set.
    let mut adapted = meta
    for _step in 0..<n_inner {
      let grad = mlp_model_mse_grad_batch(
        adapted, task.support_inputs, task.support_targets, task.n_support,
      )
      adapted = mlp_model_sgd_step(adapted, grad, inner_lr)
    }
    // Outer gradient: gradient of the query loss w.r.t. the ADAPTED
    // parameters, applied directly to θ (FOMAML).
    let outer_grad = mlp_model_mse_grad_batch(
      adapted, task.query_inputs, task.query_targets, task.n_query,
    )
    for i2 in 0..<h_dim {
      agg_d_b1[i2] = agg_d_b1[i2] + outer_grad.d_b1[i2]
      for k in 0..<in_dim {
        agg_d_w1[i2][k] = agg_d_w1[i2][k] + outer_grad.d_w1[i2][k]
      }
    }
    for i2 in 0..<out_dim {
      agg_d_b2[i2] = agg_d_b2[i2] + outer_grad.d_b2[i2]
      for k in 0..<h_dim {
        agg_d_w2[i2][k] = agg_d_w2[i2][k] + outer_grad.d_w2[i2][k]
      }
    }
    // Track mean query loss for monitoring.
    let q_loss = mlp_model_mse_loss(
      adapted, task.query_inputs, task.query_targets, task.n_query,
    )
    total_loss = total_loss + q_loss
  }
  // Mean outer gradient + mean loss.
  let inv_n = 1.0F / Float::from_int(n_tasks)
  for i2 in 0..<h_dim {
    agg_d_b1[i2] = agg_d_b1[i2] * inv_n
    for k in 0..<in_dim {
      agg_d_w1[i2][k] = agg_d_w1[i2][k] * inv_n
    }
  }
  for i2 in 0..<out_dim {
    agg_d_b2[i2] = agg_d_b2[i2] * inv_n
    for k in 0..<h_dim {
      agg_d_w2[i2][k] = agg_d_w2[i2][k] * inv_n
    }
  }
  let mean_loss = total_loss * inv_n
  // Apply outer SGD step to the meta model.
  let outer_grad : MLPGrad = {
    d_w1: agg_d_w1,
    d_b1: agg_d_b1,
    d_w2: agg_d_w2,
    d_b2: agg_d_b2,
  }
  let new_meta = mlp_model_sgd_step(meta, outer_grad, outer_lr)
  let new_family = task_family_set_meta_model(family, new_meta)
  (new_family, mean_loss)
}