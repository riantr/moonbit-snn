// task_sampler.mbt — Task sampler for MAML / FOMAML / Reptile (v0.93.0).
//
// Meta-learning tasks are sampled from a task family. Each task is a
// small regression problem: given an input x ∈ R^{input_dim}, predict
// y ∈ R^{output_dim}. The task-specific parameters (an MLP's w1, b1,
// w2, b2) are drawn from a distribution centered on a "meta" model
// (which itself holds the meta-learned parameters θ).
//
// Scope of v0.93.0:
//   - MLPModel struct + constructor (small 2-layer MLP, xavier init)
//   - mlp_forward: forward through an MLPModel
//   - TaskFamily struct + sampler: produces meta-batches of tasks
//   - Each task has its own (slightly randomized) MLPModel + a
//     support set + a query set (input/target pairs)
//   - TaskFamily::sample_meta_batch(n_tasks, n_support, n_query, seed)
//     returns an array of Tasks
//
// Reference: Finn et al. 2017 "Model-Agnostic Meta-Learning for Fast
// Adaptation of Deep Networks".

///|
/// A small 2-layer MLP. The meta-learned parameters θ are the weights
/// of this MLP; task-specific parameters are perturbations of θ.
pub struct MLPModel {
  input_dim : Int
  hidden_dim : Int
  output_dim : Int
  // Linear1: (hidden_dim × input_dim) + bias of length hidden_dim
  w1 : Array[Array[Float]]
  b1 : Array[Float]
  // Linear2: (output_dim × hidden_dim) + bias of length output_dim
  w2 : Array[Array[Float]]
  b2 : Array[Float]
}

///|
/// Build a fresh MLPModel with xavier-normal init.
pub fn MLPModel::new(
  input_dim : Int,
  hidden_dim : Int,
  output_dim : Int,
  seed : UInt64,
) -> MLPModel {
  let rng1 = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  let std1 = sqrtf(2.0F / Float::from_int(input_dim))
  let w1 = xavier_normal(hidden_dim, input_dim, std1, rng1)
  let b1 : Array[Float] = Array::make(hidden_dim, 0.0F)
  let rng2 = Xoshiro::from_state(seed + 4UL, seed + 5UL, seed + 6UL, seed + 7UL)
  let std2 = sqrtf(2.0F / Float::from_int(hidden_dim))
  let w2 = xavier_normal(output_dim, hidden_dim, std2, rng2)
  let b2 : Array[Float] = Array::make(output_dim, 0.0F)
  { input_dim, hidden_dim, output_dim, w1, b1, w2, b2 }
}

///|
/// Forward through an MLPModel. Returns the output vector (length
/// output_dim).
pub fn mlp_model_forward(model : MLPModel, input : Array[Float]) -> Array[Float] {
  // hidden = tanh(w1 · input + b1)
  let hidden : Array[Float] = Array::make(model.hidden_dim, 0.0F)
  for i in 0..<model.hidden_dim {
    let mut s = model.b1[i]
    for k in 0..<model.input_dim {
      s = s + model.w1[i][k] * input[k]
    }
    hidden[i] = tanhf(s)
  }
  // output = w2 · hidden + b2
  let output : Array[Float] = Array::make(model.output_dim, 0.0F)
  for i in 0..<model.output_dim {
    let mut s = model.b2[i]
    for k in 0..<model.hidden_dim {
      s = s + model.w2[i][k] * hidden[k]
    }
    output[i] = s
  }
  output
}

///|
/// One meta-learning task: a task-specific MLPModel + support set +
/// query set. All flat row-major.
pub struct MetaTask {
  model : MLPModel
  // Support set: n_support pairs.
  support_inputs : Array[Float]   // [n_support × input_dim]
  support_targets : Array[Float]  // [n_support × output_dim]
  // Query set: n_query pairs.
  query_inputs : Array[Float]     // [n_query × input_dim]
  query_targets : Array[Float]    // [n_query × output_dim]
  n_support : Int
  n_query : Int
}

///|
/// Task family: a distribution over meta-tasks parameterized by a
/// meta-learned MLPModel + per-task Gaussian perturbation of the
/// weights (sigma = task_noise_std) + random Gaussian inputs +
/// model-defined targets.
pub struct TaskFamily {
  meta_model : MLPModel
  task_noise_std : Float
  input_noise_std : Float
}

///|
/// Build a TaskFamily around a meta-learned MLPModel. `task_noise_std`
/// controls how much each task's parameters deviate from the meta
/// model (typical: 0.1 to 1.0). `input_noise_std` controls input
/// perturbation (typical: 0.0 to 0.5).
pub fn TaskFamily::new(
  meta_model : MLPModel,
  task_noise_std : Float,
  input_noise_std : Float,
) -> TaskFamily {
  { meta_model, task_noise_std, input_noise_std }
}

///|
/// Set the meta-model parameters (used by MAML/Reptile to update θ
/// after each meta-step). Returns an updated family.
pub fn task_family_set_meta_model(
  family : TaskFamily,
  meta_model : MLPModel,
) -> TaskFamily {
  { ..family, meta_model }
}

///|
/// Sample one meta-task. The task-specific MLPModel is the meta model
/// plus Gaussian noise (task_noise_std) on each weight and bias. The
// support and query inputs are Gaussian (input_noise_std around 0);
// the targets are computed by passing the inputs through the
/// task-specific model.
pub fn sample_task(
  family : TaskFamily,
  n_support : Int,
  n_query : Int,
  seed : UInt64,
) -> MetaTask {
  let meta = family.meta_model
  let input_dim = meta.input_dim
  let output_dim = meta.output_dim
  let rng = Xoshiro::from_state(seed, seed + 1UL, seed + 2UL, seed + 3UL)
  // 1. Perturb the meta-model weights to get a task-specific model.
  let task_w1 : Array[Array[Float]] = Array::make(
    meta.hidden_dim, Array::make(meta.input_dim, 0.0F),
  )
  let task_b1 : Array[Float] = Array::make(meta.hidden_dim, 0.0F)
  let task_w2 : Array[Array[Float]] = Array::make(
    meta.output_dim, Array::make(meta.hidden_dim, 0.0F),
  )
  let task_b2 : Array[Float] = Array::make(meta.output_dim, 0.0F)
  let task_input_noise : Float = family.input_noise_std
  for i in 0..<meta.hidden_dim {
    task_b1[i] = meta.b1[i] +
      family.task_noise_std * gaussian_noise(rng)
    for k in 0..<meta.input_dim {
      task_w1[i][k] = meta.w1[i][k] +
        family.task_noise_std * gaussian_noise(rng)
    }
  }
  for i in 0..<meta.output_dim {
    task_b2[i] = meta.b2[i] +
      family.task_noise_std * gaussian_noise(rng)
    for k in 0..<meta.hidden_dim {
      task_w2[i][k] = meta.w2[i][k] +
        family.task_noise_std * gaussian_noise(rng)
    }
  }
  let task_model : MLPModel = {
    input_dim: meta.input_dim,
    hidden_dim: meta.hidden_dim,
    output_dim: meta.output_dim,
    w1: task_w1,
    b1: task_b1,
    w2: task_w2,
    b2: task_b2,
  }
  let _ = task_input_noise  // currently unused — reserved for future input-noise application
  // 3. Sample support + query inputs (Gaussian), then compute targets.
  let support_inputs : Array[Float] = Array::make(
    n_support * input_dim, 0.0F,
  )
  let support_targets : Array[Float] = Array::make(
    n_support * output_dim, 0.0F,
  )
  let query_inputs : Array[Float] = Array::make(
    n_query * input_dim, 0.0F,
  )
  let query_targets : Array[Float] = Array::make(
    n_query * output_dim, 0.0F,
  )
  for n in 0..<n_support {
    let inp : Array[Float] = Array::make(input_dim, 0.0F)
    for k in 0..<input_dim {
      inp[k] = gaussian_noise(rng)
      support_inputs[n * input_dim + k] = inp[k]
    }
    let target = mlp_model_forward(task_model, inp)
    for k in 0..<output_dim {
      support_targets[n + k] = target[k]
    }
  }
  for n in 0..<n_query {
    let inp : Array[Float] = Array::make(input_dim, 0.0F)
    for k in 0..<input_dim {
      inp[k] = gaussian_noise(rng)
      query_inputs[n * input_dim + k] = inp[k]
    }
    let target = mlp_model_forward(task_model, inp)
    for k in 0..<output_dim {
      query_targets[n + k] = target[k]
    }
  }
  {
    model: task_model,
    support_inputs,
    support_targets,
    query_inputs,
    query_targets,
    n_support,
    n_query,
  }
}

///|
/// Sample a meta-batch of `n_tasks` tasks.
pub fn sample_meta_batch(
  family : TaskFamily,
  n_tasks : Int,
  n_support : Int,
  n_query : Int,
  seed : UInt64,
) -> Array[MetaTask] {
  let tasks : Array[MetaTask] = Array::make(n_tasks, sample_task(
    family, n_support, n_query, seed,
  ))
  for i in 0..<n_tasks {
    tasks[i] = sample_task(
      family, n_support, n_query,
      seed + (i + 1).to_uint64() * 1000UL,
    )
  }
  tasks
}

///|
/// Helper: standard normal noise via Box-Muller (mean 0, std 1).
fn gaussian_noise(r : Xoshiro) -> Float {
  let (z1, _) = box_muller(r)
  Float::from_double(z1)
}