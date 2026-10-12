// Tests for SpikingSynapseIZ.

///|
test "SpikingSynapseIZ::new builds empty matrix" {
  let rng = @core.Xoshiro::default()
  let pre = @neuron.IZ::new(5, @neuron.IZParameter::rs(), rng)
  let post = @neuron.IZ::new(3, @neuron.IZParameter::rs(), rng)
  let s = @synapse.SpikingSynapseIZ::new(pre, post, "ge")
  assert_eq(s.matrix.nnz(), 0)
  assert_eq(s.matrix.rows, 5)
  assert_eq(s.matrix.cols, 3)
}

///|
test "SpikingSynapseIZ::random populates with non-zero weights" {
  let rng = @core.Xoshiro::from_state(1UL, 2UL, 3UL, 4UL)
  let pre = @neuron.IZ::new(10, @neuron.IZParameter::rs(), rng)
  let post = @neuron.IZ::new(10, @neuron.IZParameter::rs(), rng)
  let s = @synapse.SpikingSynapseIZ::random(pre, post, "ge", 1.0F, 0.0F, 0.5F, rng)
  // p=0.5 → expected ~50 connections (10 * 10 * 0.5)
  let n = s.matrix.nnz()
  if n < 30 || n > 70 {
    fail("SpikingSynapseIZ nnz = " + n.to_string() + " (expected ~50)")
  }
}

///|
test "SpikingSynapseIZ::forward deposits weight into post.ge on pre fire" {
  let rng = @core.Xoshiro::default()
  let pre = @neuron.IZ::new(2, @neuron.IZParameter::rs(), rng)
  let post = @neuron.IZ::new(2, @neuron.IZParameter::rs(), rng)
  let s = @synapse.SpikingSynapseIZ::new(pre, post, "ge")
  @synapse.iz_connect(s, 1, 1, 5.0F)  // pre[0] -> post[0] with w=5
  @synapse.iz_connect(s, 2, 2, 3.0F)  // pre[1] -> post[1] with w=3
  // Fire pre[0] only.
  pre.fire[0] = true
  pre.fire[1] = false
  @synapse.forward_iz_synapse(s)
  assert_eq(post.ge[0], 5.0F)
  assert_eq(post.ge[1], 0.0F)
  // Fire pre[1] too.
  pre.fire[1] = true
  @synapse.forward_iz_synapse(s)
  assert_eq(post.ge[0], 10.0F)
  assert_eq(post.ge[1], 3.0F)
}

///|
test "SpikingSynapseIZ::forward routes gi to post.gi" {
  let rng = @core.Xoshiro::default()
  let pre = @neuron.IZ::new(2, @neuron.IZParameter::rs(), rng)
  let post = @neuron.IZ::new(2, @neuron.IZParameter::rs(), rng)
  let s = @synapse.SpikingSynapseIZ::new(pre, post, "gi")
  @synapse.iz_connect(s, 1, 1, 4.0F)
  pre.fire[0] = true
  @synapse.forward_iz_synapse(s)
  assert_eq(post.gi[0], 4.0F)
  assert_eq(post.ge[0], 0.0F)  // not routed to ge
}