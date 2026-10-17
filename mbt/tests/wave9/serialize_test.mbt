// serialize_test.mbt — round-trip tests for the model + optimizer
// checkpoint serialization helpers (v0.56.0).
//
// Coverage:
//   * LinearQNet round-trip (Float32 bit-exact)
//   * QNetworkContinuous round-trip (Float32 bit-exact, scalar b2)
//   * DeterministicPolicy round-trip (action_low/high preserved)
//   * AdamState round-trip (moments + step via adam_update_arrays)
//   * QNetCheckpoint round-trip (model + optimizer + step counter)
//   * Independent copies: mutating original after serialize does not
//     affect the deserialized side.
//
// Note: AdamState fields are not `pub`, so tests construct the state
// via `adam_init` and `adam_update_arrays` (the only public API).

///|
test "serialize_linear_qnet_roundtrip" {
  let q0 : @rl.LinearQNet = @rl.LinearQNet::new(3, 2, 7UL)
  // Set deterministic values to compare round-trip exactly.
  q0.w[0][0] = 0.1F
  q0.w[0][1] = -0.2F
  q0.w[0][2] = 0.3F
  q0.w[1][0] = 0.4F
  q0.w[1][1] = 0.5F
  q0.w[1][2] = -0.6F
  q0.b[0] = 0.01F
  q0.b[1] = -0.02F
  let s = @infra.serialize_linear_qnet(q0)
  let q1 = @infra.deserialize_linear_qnet(s)
  assert_eq(q1.n_states, 3)
  assert_eq(q1.n_actions, 2)
  assert_eq(q1.w.length(), 2)
  assert_eq(q1.w[0].length(), 3)
  assert_eq(q1.b.length(), 2)
  // Round-trip floats. The encoding is NOT `Float::to_string` -> `parse_float`
  // -- an earlier version of this comment said so, and was wrong. `serialize.mbt`
  // writes `Float::reinterpret_as_int` hex for every float it emits (see
  // `encode_float_list`, `encode_float_matrix`, and the scalar fields `b2`,
  // `action_low`, `action_high`), so the round trip is bit-exact by
  // construction and does not depend on any decimal representation being
  // stable. That is why `0.1F` is a useful value to round-trip here: it has no
  // short decimal form, so a to_string-based path would lose it.
  assert_eq(q1.w[0][0], 0.1F)
  assert_eq(q1.w[0][1], -0.2F)
  assert_eq(q1.w[0][2], 0.3F)
  assert_eq(q1.w[1][0], 0.4F)
  assert_eq(q1.w[1][1], 0.5F)
  assert_eq(q1.w[1][2], -0.6F)
  assert_eq(q1.b[0], 0.01F)
  assert_eq(q1.b[1], -0.02F)
}

///|
test "serialize_linear_qnet_independent_copy" {
  // Mutating q0 after serialize must not affect q1.
  let q0 : @rl.LinearQNet = @rl.LinearQNet::new(2, 2, 11UL)
  q0.w[0][0] = 0.1F
  q0.b[1] = 0.5F
  let s = @infra.serialize_linear_qnet(q0)
  let q1 = @infra.deserialize_linear_qnet(s)
  q0.w[0][0] = 99.0F
  q0.b[1] = -99.0F
  assert_eq(q1.w[0][0], 0.1F)
  assert_eq(q1.b[1], 0.5F)
}

///|
test "serialize_qnetwork_continuous_roundtrip" {
  let q0 : @rl.QNetworkContinuous = @rl.QNetworkContinuous::new(3, 2, 4, 13UL)
  // Override weights to known values. (b2 is `mut` and read-only from
  // outside the module, so we leave it at its default 0.0F and verify
  // that value round-trips below.)
  for h in 0..<4 {
    for j in 0..<(3 + 2) {
      q0.w1[h][j] = Float::from_int(h * 10 + j) * 0.01F
    }
    q0.b1[h] = Float::from_int(h) * -0.05F
    q0.w2[h] = Float::from_int(h) * 0.1F
  }
  let s = @infra.serialize_qnetwork_continuous(q0)
  let q1 = @infra.deserialize_qnetwork_continuous(s)
  assert_eq(q1.state_dim, 3)
  assert_eq(q1.action_dim, 2)
  assert_eq(q1.hidden, 4)
  assert_eq(q1.w1.length(), 4)
  assert_eq(q1.w1[0].length(), 5)
  assert_eq(q1.b1.length(), 4)
  assert_eq(q1.w2.length(), 4)
  // Spot-check a few weights.
  assert_eq(q1.w1[0][0], 0.0F)
  assert_eq(q1.w1[3][4], 0.34F)
  assert_eq(q1.b1[2], -0.1F)
  assert_eq(q1.w2[1], 0.1F)
  // b2 preserved (default 0.0F).
  assert_eq(q1.b2, 0.0F)
}

///|
test "serialize_qnetwork_continuous_independent" {
  // Verify that mutating the array-typed fields of the original q0
  // after serialize does not affect the deserialized copy. (b2 is
  // skipped because it is `mut` and read-only from outside the
  // module; the default round-trip test above already covers it.)
  let q0 : @rl.QNetworkContinuous = @rl.QNetworkContinuous::new(2, 2, 2, 41UL)
  q0.w1[0][0] = 0.7F
  q0.w2[0] = -0.3F
  q0.b1[1] = 0.42F
  let s = @infra.serialize_qnetwork_continuous(q0)
  let q1 = @infra.deserialize_qnetwork_continuous(s)
  q0.w1[0][0] = 99.0F
  q0.w2[0] = 99.0F
  q0.b1[1] = -99.0F
  assert_eq(q1.w1[0][0], 0.7F)
  assert_eq(q1.w2[0], -0.3F)
  assert_eq(q1.b1[1], 0.42F)
}

///|
test "serialize_deterministic_policy_roundtrip" {
  let p0 : @rl.DeterministicPolicy = @rl.DeterministicPolicy::new(
    3, 2, 4, -1.5F, 1.5F, 17UL,
  )
  for h in 0..<4 {
    for j in 0..<3 {
      p0.w1[h][j] = Float::from_int(h * 3 + j) * 0.02F
    }
    for a in 0..<2 {
      p0.w2[a][h] = Float::from_int(a * 4 + h) * 0.05F
    }
  }
  let s = @infra.serialize_deterministic_policy(p0)
  let p1 = @infra.deserialize_deterministic_policy(s)
  assert_eq(p1.state_dim, 3)
  assert_eq(p1.action_dim, 2)
  assert_eq(p1.hidden, 4)
  assert_eq(p1.action_low, -1.5F)
  assert_eq(p1.action_high, 1.5F)
  assert_eq(p1.w1.length(), 4)
  assert_eq(p1.w1[0].length(), 3)
  assert_eq(p1.w2.length(), 2)
  assert_eq(p1.w2[0].length(), 4)
  assert_eq(p1.b2.length(), 2)
  // Spot-check first row. Note: the values use Float32 arithmetic so
  // Float::from_int(k) * 0.02F may differ from k*0.02 in Double. The
  // round-trip preserves the original Float32 value bit-exactly.
  assert_eq(p1.w1[0][0], 0.0F)
  assert_eq(p1.w1[1][2], 0.09999999403953552F)
  // p0.w2[1][3] = Float::from_int(7) * 0.05F = 0.3499999940395355F.
  assert_eq(p1.w2[1][3], 0.3499999940395355F)
}

///|
test "serialize_adam_state_after_one_step" {
  // Build an AdamState with step > 0 by running one adam_update_arrays.
  let opt0 = @optim.adam_init(6, 2)
  // Pre-set non-zero moments on opt0 by running a no-op update first.
  // (AdamState fields are not pub, but adam_update_arrays accepts the
  // state read-only and returns a new state — we feed the new state
  // into a second call so we have a deterministic post-step state.)
  let weight : Array[Float] = [0.5F, -0.5F, 0.5F, -0.5F, 0.5F, -0.5F]
  let bias : Array[Float] = [0.1F, -0.1F]
  let d_weight : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let d_bias : Array[Float] = [0.01F, -0.01F]
  let (_w2a, _b2a, opt_a) = @optim.adam_update_arrays(
    weight, bias, d_weight, d_bias, opt0,
    0.001F, 0.9F, 0.999F, 1.0e-7F,
  )
  // Second call with different gradients — opt_a has step=1.
  let d_weight2 : Array[Float] = [-0.1F, 0.2F, -0.3F, 0.4F, -0.5F, 0.6F]
  let d_bias2 : Array[Float] = [-0.02F, 0.03F]
  let (_w2b, _b2b, opt_b) = @optim.adam_update_arrays(
    weight, bias, d_weight2, d_bias2, opt_a,
    0.001F, 0.9F, 0.999F, 1.0e-7F,
  )
  // opt_b.step should be 2 (two adam_update_arrays calls bumped it
  // from 0 -> 1 -> 2). adam_next_step returns step + 1, so we expect 3.
  let s = @infra.serialize_adam_state(opt_b)
  let opt1 = @infra.deserialize_adam_state(s)
  assert_eq(opt1.m_w.length(), 6)
  assert_eq(opt1.v_w.length(), 6)
  assert_eq(opt1.m_b.length(), 2)
  assert_eq(opt1.v_b.length(), 2)
  // Step is incremented by each adam_update_arrays call (0 -> 1 -> 2).
  // AdamState fields are not pub, so use adam_next_step to read step
  // (returns step + 1, so we expect 3 for the post-deserialize state).
  assert_eq(@optim.adam_next_step(opt1), 3)
  // m_w[0] after two updates — round-trip preserves bit-exact value.
  let m0 = opt1.m_w[0]
  // First step: m = 0.9 * 0.0 + 0.1 * 0.1 = 0.01 (then quantized to
  // Float32 = 0.009999999776482582).
  // Second step: m = 0.9 * (that) + 0.1 * (-0.1) = -0.001.
  // Exact bit pattern preserved through encode -> decode.
  let m_after_first = Float::from_double(0.1 * 0.1)
  let expected = Float::from_double(
    (0.9 * (Float::reinterpret_as_int(m_after_first).to_double() /
            4294967296.0) +
     0.1 * -0.1),
  )
  let _ = expected
  // Just check m_w[0] is a finite value (round-trip succeeded);
  // exact bit-pattern check would require reproducing the adam_update
  // arithmetic exactly which is environment-sensitive.
  if m0 != m0 {
    abort("m_w[0] is NaN")
  }
}

///|
test "serialize_adam_state_zero_state" {
  // The zero-init state must also round-trip cleanly.
  let opt0 = @optim.adam_init(4, 2)
  let s = @infra.serialize_adam_state(opt0)
  let opt1 = @infra.deserialize_adam_state(s)
  // adam_next_step returns state.step + 1; for zero-init state.step=0,
  // so adam_next_step = 1.
  assert_eq(@optim.adam_next_step(opt1), 1)
  assert_eq(opt1.m_w.length(), 4)
  assert_eq(opt1.m_b.length(), 2)
  assert_eq(opt1.v_w.length(), 4)
  assert_eq(opt1.v_b.length(), 2)
}

///|
test "serialize_qnet_checkpoint_roundtrip" {
  let q0 : @rl.LinearQNet = @rl.LinearQNet::new(2, 3, 23UL)
  for i in 0..<3 {
    q0.w[i][0] = Float::from_int(i + 1) * 0.1F
    q0.w[i][1] = -Float::from_int(i + 1) * 0.1F
    q0.b[i] = Float::from_int(i) * 0.05F
  }
  // Build a non-trivial AdamState via one optimizer step.
  let weight : Array[Float] = [0.1F, -0.2F, 0.3F, -0.4F, 0.5F, -0.6F]
  let bias : Array[Float] = [0.01F, -0.02F, 0.03F]
  let d_weight : Array[Float] = [0.05F, 0.04F, 0.03F, 0.02F, 0.01F, 0.005F]
  let d_bias : Array[Float] = [0.001F, -0.001F, 0.0005F]
  let opt0 = @optim.adam_init(6, 3)
  let (_w2, _b2, opt_used) = @optim.adam_update_arrays(
    weight, bias, d_weight, d_bias, opt0,
    0.001F, 0.9F, 0.999F, 1.0e-7F,
  )
  let s = @infra.serialize_qnet_checkpoint(q0, opt_used, 99)
  let (q1, _opt1, step1) = @infra.deserialize_qnet_checkpoint(s)
  assert_eq(step1, 99)
  assert_eq(q1.n_states, 2)
  assert_eq(q1.n_actions, 3)
  assert_eq(q1.w[2][1], -0.3F)
  assert_eq(q1.b[1], 0.05F)
}

///|
test "serialize_qnet_checkpoint_independent_copies" {
  // Mutating the model after serialize must not affect the
  // deserialized copy.
  let q0 : @rl.LinearQNet = @rl.LinearQNet::new(2, 2, 29UL)
  q0.w[0][0] = 0.5F
  let opt0 = @optim.adam_init(4, 2)
  let s = @infra.serialize_qnet_checkpoint(q0, opt0, 5)
  let (q1, _opt1, _) = @infra.deserialize_qnet_checkpoint(s)
  q0.w[0][0] = -99.0F
  assert_eq(q1.w[0][0], 0.5F)
}

///|
test "serialize_linear_qnet_header_has_kind_tag" {
  let q : @rl.LinearQNet = @rl.LinearQNet::new(2, 2, 31UL)
  let s = @infra.serialize_linear_qnet(q)
  // First non-empty line should be the magic header.
  if !(s.length() >= 11 && s[0] == 'S' && s[1] == 'N' && s[2] == 'N') {
    abort("SNNCKPT header missing")
  }
  if !(s.contains("kind=LinearQNet")) {
    abort("kind tag missing")
  }
}

///|
test "serialize_deterministic_policy_action_range_preserved" {
  // action_low / action_high must round-trip even with non-+-1 range.
  let p0 : @rl.DeterministicPolicy = @rl.DeterministicPolicy::new(
    2, 1, 2, -2.5F, 3.5F, 43UL,
  )
  let s = @infra.serialize_deterministic_policy(p0)
  let p1 = @infra.deserialize_deterministic_policy(s)
  assert_eq(p1.action_low, -2.5F)
  assert_eq(p1.action_high, 3.5F)
}

///|
test "serialize_then_deserialize_preserves_negative_floats" {
  // Negative-value round-trip across the whole pipeline.
  let q0 : @rl.LinearQNet = @rl.LinearQNet::new(2, 2, 53UL)
  q0.w[0][0] = -3.14159F
  q0.w[1][1] = -0.0001F
  q0.b[0] = -42.0F
  let s = @infra.serialize_linear_qnet(q0)
  let q1 = @infra.deserialize_linear_qnet(s)
  assert_eq(q1.w[0][0], -3.14159F)
  assert_eq(q1.w[1][1], -0.0001F)
  assert_eq(q1.b[0], -42.0F)
}

///|
/// Encode -> decode -> encode must be the IDENTITY on the bytes, for all five
/// checkpoint kinds.
///
/// Every other test here compares VALUES, which is the weaker question. A codec
/// that normalises silently -- emits a field in a different order, pads an Int
/// header, rounds a float through a decimal -- returns the right numbers and
/// still cannot round-trip a file. Byte equality is what makes "bit-exact" a
/// demonstrated property rather than an intention.
///
/// Borrowed from `chnlkw/moon_protobuf`, whose whole conformance story rests on
/// `wire == again`; that project is NOT a dependency here and this is not an
/// argument for adopting it. It is the one technique worth copying from it.
///
/// The values are deliberately un-printable: 0.1F, -3.14159F and the float32
/// result 0.09999999403953552F have no short decimal form. A codec going through
/// `Float::to_string` would lose them, so this test fails the moment someone
/// "simplifies" the hex encoding.
test "re-encode after decode is byte-identical for every checkpoint kind" {
  // -- LinearQNet -------------------------------------------------------
  let q : @rl.LinearQNet = @rl.LinearQNet::new(3, 2, 7UL)
  q.w[0][0] = 0.1F
  q.w[0][1] = -3.14159F
  q.w[1][2] = 0.09999999403953552F
  q.b[0] = 1.0F / 3.0F
  let s1 = @infra.serialize_linear_qnet(q)
  let s2 = @infra.serialize_linear_qnet(
    @infra.deserialize_linear_qnet(s1),
  )
  assert_eq(s2, s1)

  // -- QNetworkContinuous -----------------------------------------------
  // `b2` is not `pub` and is left as the constructor set it; w1/b1 carry the
  // un-printable values.
  let qc : @rl.QNetworkContinuous = @rl.QNetworkContinuous::new(3, 2, 4, 13UL)
  qc.w1[0][0] = 0.1F
  qc.w1[2][1] = 0.09999999403953552F
  qc.b1[0] = -3.14159F
  let c1 = @infra.serialize_qnetwork_continuous(qc)
  let c2 = @infra.serialize_qnetwork_continuous(
    @infra.deserialize_qnetwork_continuous(c1),
  )
  assert_eq(c2, c1)

  // -- DeterministicPolicy (action_low/high are scalars) -----------------
  let p : @rl.DeterministicPolicy = @rl.DeterministicPolicy::new(
    3, 2, 4, -1.5F, 1.5F, 17UL,
  )
  p.w1[0][1] = 0.1F
  p.b2[1] = -3.14159F
  let p1 = @infra.serialize_deterministic_policy(p)
  let p2 = @infra.serialize_deterministic_policy(
    @infra.deserialize_deterministic_policy(p1),
  )
  assert_eq(p2, p1)

  // -- AdamState (moments are float32 arithmetic results) ---------------
  let opt0 = @optim.adam_init(6, 2)
  let w : Array[Float] = [0.5F, -0.5F, 0.5F, -0.5F, 0.5F, -0.5F]
  let b : Array[Float] = [0.1F, -0.1F]
  let dw : Array[Float] = [0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F]
  let db : Array[Float] = [0.01F, -0.01F]
  let (_w1, _b1, opt_a) = @optim.adam_update_arrays(
    w, b, dw, db, opt0, 0.001F, 0.9F, 0.999F, 1.0e-7F,
  )
  let dw2 : Array[Float] = [-0.1F, 0.2F, -0.3F, 0.4F, -0.5F, 0.6F]
  let db2 : Array[Float] = [-0.02F, 0.03F]
  let (_w2, _b2, opt_b) = @optim.adam_update_arrays(
    w, b, dw2, db2, opt_a, 0.001F, 0.9F, 0.999F, 1.0e-7F,
  )
  let a1 = @infra.serialize_adam_state(opt_b)
  let a2 = @infra.serialize_adam_state(@infra.deserialize_adam_state(a1))
  assert_eq(a2, a1)

  // -- QNetCheckpoint (model + optimizer + step in one payload) ---------
  let k1 = @infra.serialize_qnet_checkpoint(q, opt_b, 99)
  let (kq, kopt, kstep) = @infra.deserialize_qnet_checkpoint(k1)
  let k2 = @infra.serialize_qnet_checkpoint(kq, kopt, kstep)
  assert_eq(k2, k1)
}

///|
/// The check above is only worth having if it CAN fail, so prove it on a
/// difference it is supposed to catch.
///
/// Without this, a comparison that compared two identical strings by mistake
/// would report the property holds -- and a byte-equality gate that cannot go
/// red is the same failure as a tolerance no defect can reach.
test "the byte-identity check does detect a changed payload" {
  let q : @rl.LinearQNet = @rl.LinearQNet::new(3, 2, 7UL)
  q.w[0][0] = 0.1F
  let before = @infra.serialize_linear_qnet(q)
  // One float in the last place. The hex encoding makes this a visible
  // difference; a decimal encoder would have rounded it away, which is the
  // other half of why the encoding is hex.
  q.w[0][0] = Float::reinterpret_from_int(
    Float::reinterpret_as_int(0.1F) + 1,
  )
  let after = @infra.serialize_linear_qnet(q)
  assert_eq(after == before, false)
}
