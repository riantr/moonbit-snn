// Tests for SpikingSynapse + delay_dist support.

///|
test "SpikingSynapse::new: delays empty by default" {
  let rng = Xoshiro::default()
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  assert_eq(s.delays.length(), 0)
  assert_eq(s.pending_times.length(), 0)
}

///|
test "SpikingSynapse::set_constant_delay: populates delays" {
  let rng = Xoshiro::default()
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(s, 1, 1, 1.5F)
  @snn_mbt.spiking_connect(s, 2, 2, 2.5F)
  SpikingSynapse::set_constant_delay(s, 0.8F)
  assert_eq(s.delays.length(), 2)
  assert_eq(s.delays[0], 0.8F)
  assert_eq(s.delays[1], 0.8F)
}

///|
test "SpikingSynapse::random_with_delays: Normal distribution sampling" {
  let rng = Xoshiro::from_state(0xDEADBEEFUL, 0xCAFEBABEUL, 0x12345678UL, 0x9ABCDEF0UL)
  let pre = IF::new(3, IFParameter::new(), rng)
  let post = IF::new(3, IFParameter::new(), rng)
  let s = SpikingSynapse::random_with_delays(
    pre, post, "ge", 1.0F, 0.0F, 1.0F, rng, 0.8F, 0.0F,
  )
  // 3x3 all-to-all = 9 connections, all with delay = 0.8F (since std=0).
  assert_eq(s.delays.length(), 9)
  for k in 0..<9 {
    assert_eq(s.delays[k], 0.8F)
  }
}

///|
test "SpikingSynapse::forward_synapse: no-delay fast path applies immediately" {
  let rng = Xoshiro::default()
  let pre = IF::new(1, IFParameter::new(), rng)
  let post = IF::new(1, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(s, 1, 1, 1.5F)
  // No delays configured; forward should apply immediately to post.glu.
  pre.fire[0] = true
  forward_synapse(s, 0.0F)
  assert_eq(post.glu[0], 1.5F)
  assert_eq(s.pending_times.length(), 0)
}

///|
test "SpikingSynapse::forward_synapse with delay: schedules events" {
  let rng = Xoshiro::default()
  let pre = IF::new(1, IFParameter::new(), rng)
  let post = IF::new(1, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(s, 1, 1, 1.5F)
  SpikingSynapse::set_constant_delay(s, 0.8F)
  pre.fire[0] = true
  forward_synapse(s, 0.0F)
  // Spike scheduled for delivery at t = 0.0 + 0.8 = 0.8 ms.
  // post.glu should NOT yet be incremented.
  assert_eq(post.glu[0], 0.0F)
  assert_eq(s.pending_times.length(), 1)
  assert_eq(s.pending_times[0], 0.8F)
  assert_eq(s.pending_posts[0], 0)
  assert_eq(s.pending_weights[0], 1.5F)
}

///|
test "SpikingSynapse::deliver_pending_synapse: delivers only past-due events" {
  let rng = Xoshiro::default()
  let pre = IF::new(1, IFParameter::new(), rng)
  let post = IF::new(1, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(s, 1, 1, 1.5F)
  SpikingSynapse::set_constant_delay(s, 0.8F)
  pre.fire[0] = true
  forward_synapse(s, 0.0F)
  // t = 0.5 ms: event NOT yet delivered (delivery time 0.8 > 0.5).
  deliver_pending_synapse(s, 0.5F)
  assert_eq(post.glu[0], 0.0F)
  assert_eq(s.pending_times.length(), 1)
  // t = 0.8 ms: event delivered (delivery time 0.8 <= 0.8).
  deliver_pending_synapse(s, 0.8F)
  assert_eq(post.glu[0], 1.5F)
  assert_eq(s.pending_times.length(), 0)
}

///|
test "SpikingSynapse: delay_dist composition across steps" {
  // 2 pre, 1 post, identity synapse with 1.0 delay. Force-fire pre[0]
  // at t=0; expect post.glu[0] to receive the spike at t=1.0.
  let rng = Xoshiro::default()
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(1, IFParameter::new(), rng)
  let s = SpikingSynapse::new(pre, post, "ge")
  @snn_mbt.spiking_connect(s, 1, 1, 1.0F)
  @snn_mbt.spiking_connect(s, 2, 1, 1.0F)
  SpikingSynapse::set_constant_delay(s, 1.0F)
  // Step 1 (t=0): force-fire pre[0], propagate.
  pre.fire[0] = true
  forward_synapse(s, 0.0F)
  // post.glu[0] still 0 (event scheduled for t=1.0).
  assert_eq(post.glu[0], 0.0F)
  // Step 2 (t=0.125): nothing happens yet.
  pre.fire[0] = false
  pre.fire[1] = true
  deliver_pending_synapse(s, 0.125F)
  forward_synapse(s, 0.125F)
  assert_eq(post.glu[0], 0.0F)
  // Step 9 (t=1.0): pre[0]'s scheduled spike from t=0 is delivered.
  pre.fire[1] = false
  deliver_pending_synapse(s, 1.0F)
  forward_synapse(s, 1.0F)
  assert_eq(post.glu[0], 1.0F)
}