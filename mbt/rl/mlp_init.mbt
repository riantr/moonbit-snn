// mlp_init.mbt -- the xavier-normal hidden stack that the MLP constructors
// share.
//
// EnergyFunction, ScoreNetwork and EmbeddingNet each build the same thing:
// `num_layers - 1` hidden layers of width `hidden_dim`, every one of them
// initialised with xavier_normal from a per-layer RNG stream seeded off the
// model's own seed. That block used to be copied into all three
// constructors, in two different packages, which is what the `dup` gate
// reports -- and it survived the package split because none of the three
// models had a test that constructed them.
//
// Only the hidden stack is shared. The first projection (input -> hidden)
// and the output projection differ per model in shape, in name and in the
// dimension they produce, so they stay with the model that owns them.
//
// The helper lives here because `xavier_normal` lives here, and `rl` is the
// only package that both `generative/` and `models/` can reach without a
// dependency cycle: `nn` is below `rl`, so `nn` cannot use `xavier_normal`,
// and `core` is below `nn`.

///|
/// Build the hidden weight and bias stack of an MLP: `num_layers - 1`
/// hidden layers of width `hidden_dim`, initialised with xavier_normal.
///
/// Layer `l` draws from its own Xoshiro stream, seeded from `seed` with the
/// same per-layer offsets in every model that calls this, so the weights are
/// reproducible from the seed alone and are identical across models that use
/// the same `hidden_dim`, `num_layers` and `seed`.
///
/// `num_layers` counts the layers *before* the output projection, so the
/// stack holds `num_layers - 1` matrices. The constructors' own doc comments
/// used to call these "num_layers hidden layers", which is one too many.
pub fn mlp_hidden_stack(
  hidden_dim : Int,
  num_layers : Int,
  seed : UInt64,
) -> (Array[Array[Array[Float]]], Array[Array[Float]]) {
  let hidden_w : Array[Array[Array[Float]]] = Array::make(
    num_layers - 1, Array::make(0, Array::make(0, 0.0F)),
  )
  let hidden_b : Array[Array[Float]] = Array::make(
    num_layers - 1, Array::make(0, 0.0F),
  )
  for l in 0..<(num_layers - 1) {
    let rng_l = @core.Xoshiro::from_state(
      seed + (l + 100).to_uint64() * 7UL,
      seed + (l + 100).to_uint64() * 11UL,
      seed + (l + 100).to_uint64() * 13UL,
      seed + (l + 100).to_uint64() * 17UL,
    )
    let std_l = @core.sqrtf(2.0F / Float::from_int(hidden_dim))
    hidden_w[l] = xavier_normal(hidden_dim, hidden_dim, std_l, rng_l)
    hidden_b[l] = Array::make(hidden_dim, 0.0F)
  }
  (hidden_w, hidden_b)
}
