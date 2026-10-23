// mlp_init_test.mbt -- coverage for the three MLP constructors that share
// the xavier-normal hidden stack: EnergyFunction, ScoreNetwork and
// EmbeddingNet.
//
// These three had no test at all until now, which is how the duplicated
// hidden-stack block survived the package split: with nothing constructing
// them, nothing noticed that three packages were carrying the same code.
// The bit patterns below were captured from the implementation as it stood
// before the stack was factored out into `rl::mlp_hidden_stack`, so this
// file is what proves that factoring it out changed no weight.
//
// The patterns are asserted as `Float::reinterpret_as_int` values rather
// than as decimal literals: the core's Float printer truncates to about six
// digits, so a decimal golden would silently accept a weight that drifted
// in its last bits.

///|
/// The hidden stack is identical in all three models -- same seed, same
/// hidden_dim, same weights -- which is the duplication the `dup` gate
/// reports. Asserting it here means the sharing is a documented fact rather
/// than a coincidence.
test "mlp constructors share one xavier-normal hidden stack" {
  let e = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  let s = @generative.ScoreNetwork::new(3, 2, 4, 3, 7UL)
  let m = @models.EmbeddingNet::new(3, 4, 2, 3, false, 7UL)
  let ef_bits = Float::reinterpret_as_int(e.hidden_w[0][0][0])
  let sn_bits = Float::reinterpret_as_int(s.hidden_w[0][0][0])
  let en_bits = Float::reinterpret_as_int(m.hidden_w[0][0][0])
  assert_eq(ef_bits, sn_bits)
  assert_eq(ef_bits, en_bits)
}

///|
/// The initialisation is pinned bit for bit. Every value here was read out
/// of the pre-refactor implementation; if the refactor changed a single bit
/// of a single weight, this test turns red.
test "mlp constructors: xavier-normal init is bit-exact" {
  let e = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  assert_eq(Float::reinterpret_as_int(e.w1[0][0]), 1085803181)
  assert_eq(Float::reinterpret_as_int(e.w1[0][1]), 1079510387)
  assert_eq(Float::reinterpret_as_int(e.b1[0]), 0)
  assert_eq(Float::reinterpret_as_int(e.hidden_w[0][0][0]), 1083267141)
  assert_eq(Float::reinterpret_as_int(e.hidden_w[0][0][1]), 1072265017)
  assert_eq(Float::reinterpret_as_int(e.hidden_w[1][0][0]), 1083264857)
  assert_eq(Float::reinterpret_as_int(e.hidden_b[0][0]), 0)
  assert_eq(Float::reinterpret_as_int(e.w_out[0][0]), 1084095891)

  let s = @generative.ScoreNetwork::new(3, 2, 4, 3, 7UL)
  assert_eq(Float::reinterpret_as_int(s.w_in[0][0]), 1083084511)
  assert_eq(Float::reinterpret_as_int(s.w_in[0][4]), -1122930419)
  assert_eq(Float::reinterpret_as_int(s.b_in[0]), 0)
  assert_eq(Float::reinterpret_as_int(s.hidden_w[0][0][0]), 1083267141)
  assert_eq(Float::reinterpret_as_int(s.hidden_w[0][0][1]), 1072265017)
  assert_eq(Float::reinterpret_as_int(s.hidden_w[1][0][0]), 1083264857)
  assert_eq(Float::reinterpret_as_int(s.hidden_b[0][0]), 0)
  assert_eq(Float::reinterpret_as_int(s.w_out[0][0]), 1084095891)

  let m = @models.EmbeddingNet::new(3, 4, 2, 3, false, 7UL)
  assert_eq(Float::reinterpret_as_int(m.w1[0][0]), 1085803181)
  assert_eq(Float::reinterpret_as_int(m.w1[0][1]), 1079510387)
  assert_eq(Float::reinterpret_as_int(m.b1[0]), 0)
  assert_eq(Float::reinterpret_as_int(m.hidden_w[0][0][0]), 1083267141)
  assert_eq(Float::reinterpret_as_int(m.hidden_w[0][0][1]), 1072265017)
  assert_eq(Float::reinterpret_as_int(m.hidden_w[1][0][0]), 1083264857)
  assert_eq(Float::reinterpret_as_int(m.hidden_b[0][0]), 0)
  assert_eq(Float::reinterpret_as_int(m.w_out[0][0]), 1084095891)
}

///|
/// `num_layers` counts the layers *before* the output projection, so the
/// hidden stack holds `num_layers - 1` matrices and the whole network holds
/// `num_layers + 1` weight matrices. The constructors' own doc comments
/// used to say "num_layers hidden layers", which is one too many; these
/// assertions pin the count the code actually produces.
test "mlp constructors: layer counts match the dimensions asked for" {
  let e = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  assert_eq(e.hidden_w.length(), 2)
  assert_eq(e.hidden_b.length(), 2)
  assert_eq(e.hidden_w[0].length(), 4)
  assert_eq(e.hidden_w[0][0].length(), 4)
  assert_eq(e.hidden_b[0].length(), 4)
  assert_eq(e.w1.length(), 4)
  assert_eq(e.w1[0].length(), 3)
  assert_eq(e.w_out.length(), 1)
  assert_eq(e.w_out[0].length(), 4)

  let s = @generative.ScoreNetwork::new(3, 2, 4, 3, 7UL)
  assert_eq(s.hidden_w.length(), 2)
  assert_eq(s.hidden_w[0].length(), 4)
  // The first projection sees input_dim + time_dim columns.
  assert_eq(s.w_in[0].length(), 5)
  assert_eq(s.w_out.length(), 3)
  assert_eq(s.w_out[0].length(), 4)

  let m = @models.EmbeddingNet::new(3, 4, 2, 3, false, 7UL)
  assert_eq(m.hidden_w.length(), 2)
  assert_eq(m.w1.length(), 4)
  assert_eq(m.w1[0].length(), 3)
  assert_eq(m.w_out.length(), 2)
  assert_eq(m.w_out[0].length(), 4)
  // w1 + (num_layers - 1) hidden layers + w_out.
  assert_eq(@models.embedding_net_num_params(m), 4 * 3 + 4 + 2 * (4 * 4 + 4) + 2 * 4 + 2)
}

///|
/// The whole point of a seeded init: same seed, same weights; different
/// seed, different weights.
test "mlp constructors: the seed alone decides the weights" {
  let a = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  let b = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  let c = @generative.EnergyFunction::new(3, 4, 3, 8UL)
  assert_eq(
    Float::reinterpret_as_int(a.hidden_w[0][0][0]),
    Float::reinterpret_as_int(b.hidden_w[0][0][0]),
  )
  assert_eq(
    Float::reinterpret_as_int(a.w1[0][0]),
    Float::reinterpret_as_int(b.w1[0][0]),
  )
  assert_true(
    Float::reinterpret_as_int(a.hidden_w[0][0][0]) !=
    Float::reinterpret_as_int(c.hidden_w[0][0][0]),
  )
}

///|
/// The forward passes have to work, not just the constructor.
test "mlp constructors: the forward passes produce finite values" {
  let x = [0.5F, -0.25F, 1.0F]

  let e = @generative.EnergyFunction::new(3, 4, 3, 7UL)
  let energy = @generative.energy_function_predict(e, x)
  assert_false(energy.is_nan())
  assert_false(energy.is_inf())

  let s = @generative.ScoreNetwork::new(3, 2, 4, 3, 7UL)
  let eps = @generative.score_network_predict(s, x, 3)
  assert_eq(eps.length(), 3)
  for v in eps {
    assert_false(v.is_nan())
    assert_false(v.is_inf())
  }

  let m = @models.EmbeddingNet::new(3, 4, 2, 3, false, 7UL)
  let emb = @models.embedding_net_forward(m, x)
  assert_eq(emb.length(), 2)
  for v in emb {
    assert_false(v.is_nan())
    assert_false(v.is_inf())
  }
}

///|
/// With `normalize` set, the embedding lands on the unit sphere; without
/// it, the raw projection comes through.
test "embedding_net: normalize puts the embedding on the unit sphere" {
  let x = [0.5F, -0.25F, 1.0F]
  let raw = @models.EmbeddingNet::new(3, 4, 2, 3, false, 7UL)
  let unit = @models.EmbeddingNet::new(3, 4, 2, 3, true, 7UL)
  let raw_emb = @models.embedding_net_forward(raw, x)
  let unit_emb = @models.embedding_net_forward(unit, x)
  let norm = unit_emb[0] * unit_emb[0] + unit_emb[1] * unit_emb[1]
  assert_true(norm > 0.999F && norm < 1.001F)
  // Normalising changes the vector but keeps its direction.
  assert_true(Float::reinterpret_as_int(raw_emb[0]) != Float::reinterpret_as_int(unit_emb[0]))
}
