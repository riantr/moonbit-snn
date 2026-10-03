// fomaml.mbt — FOMAML (First-Order MAML) variant (v0.95.0).
//
// FOMAML is the canonical first-order approximation to MAML: drop the
// second-order chain-rule term through the inner SGD step. For a
// single inner step:
//
//   θ' = θ - α · ∇_θ L_sup(θ)         (adapted on task support)
//   dθ_meta = ∇_θ L_query(θ')          (first-order: ∂θ'/∂θ ≈ I)
//
// In the canonical first-order MAML implementation, the gradient of
// L_query at the adapted point is computed and applied directly to θ
// (without backpropagating through the inner step). This avoids the
// expensive Hessian-vector products that full second-order MAML
// requires.
//
// FOMAML is O(n_tasks * n_inner_adaptations * |θ|) per meta-step vs
// the O(n_tasks * n_inner * |θ|²) of full second-order MAML — much
// cheaper for large models.
//
// Scope of v0.95.0:
//   - fomaml_meta_step: a streamlined FOMAML meta-step with single
//     inner step + explicit first-order gradient (no second-order).
//   - Reuses mlp_model_mse_grad_batch + mlp_model_sgd_step from
//     v0.94.0 MAML.
//
// Reference: Finn et al. 2017 "Model-Agnostic Meta-Learning" Section
// 3.1; 1st-order approximation is described in the original paper
// as "MAML with first-order approximation" (FOMAML).

///|
/// One FOMAML meta-step. Samples a meta-batch, performs a SINGLE
/// inner SGD step on each task's support set starting from the meta
/// model, then computes the first-order meta-gradient (treats θ' as
/// independent of θ) and applies it to the meta model.
///
/// Returns (updated_TaskFamily, mean_query_loss).
pub fn fomaml_meta_step(
  family : TaskFamily,
  n_tasks : Int,
  n_support : Int,
  n_query : Int,
  inner_lr : Float,
  outer_lr : Float,
  seed : UInt64,
) -> (TaskFamily, Float) {
  // FOMAML = MAML with n_inner = 1 + first-order approximation.
  // Reuse maml_meta_step (which already implements the FOMAML
  // approximation; see the comment in v0.94.0 maml.mbt).
  maml_meta_step(
    family, n_tasks, n_support, n_query, 1,
    inner_lr, outer_lr, seed,
  )
}

///|
/// Variant of FOMAML with multiple inner steps + first-order
/// approximation. Equivalent to MAML with n_inner ≥ 1 (since v0.94.0's
/// MAML already uses the FOMAML approximation). Provided here as a
/// standalone entry point for callers that prefer the FOMAML naming.
pub fn fomaml_meta_step_multi(
  family : TaskFamily,
  n_tasks : Int,
  n_support : Int,
  n_query : Int,
  n_inner : Int,
  inner_lr : Float,
  outer_lr : Float,
  seed : UInt64,
) -> (TaskFamily, Float) {
  maml_meta_step(
    family, n_tasks, n_support, n_query, n_inner,
    inner_lr, outer_lr, seed,
  )
}