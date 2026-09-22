// analysis_weight_test.mbt — tests for analysis_weight.mbt.

///|
test "average_weight — empty intersection returns 0.0" {
  let pre_pop : Array[Int] = []
  let post_pop : Array[Int] = []
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let m = average_weight(pre_pop, post_pop, syn)
  if m != 0.0F { fail("empty intersection should return 0.0F, got \{m}") }
}

///|
test "average_weight — full connectivity uniform weight" {
  let pre_pop : Array[Int] = [0, 1, 2]
  let post_pop : Array[Int] = [0, 1]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(3, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  // spiking_connect uses 1-based pre, 1-based post.
  spiking_connect(syn, 1, 1, 1.5F)
  spiking_connect(syn, 1, 2, 1.5F)
  spiking_connect(syn, 2, 1, 1.5F)
  spiking_connect(syn, 2, 2, 1.5F)
  spiking_connect(syn, 3, 1, 1.5F)
  spiking_connect(syn, 3, 2, 1.5F)
  let m = average_weight(pre_pop, post_pop, syn)
  if (m - 1.5F).abs() > 1.0e-6 { fail("expected 1.5F, got \{m}") }
}

///|
test "average_weight — partial connectivity (1 of 4 edges)" {
  let pre_pop : Array[Int] = [0]
  let post_pop : Array[Int] = [0]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 0.8F)
  let m = average_weight(pre_pop, post_pop, syn)
  if (m - 0.8F).abs() > 1.0e-6 { fail("expected 0.8F, got \{m}") }
}

///|
test "average_weight — mixed pre-pop excludes non-listed pre" {
  let pre_pop : Array[Int] = [0]  // only pre=0
  let post_pop : Array[Int] = [0, 1]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(3, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  // pre=0 → 1.0, 2.0; pre=1 → 5.0, 6.0; pre=2 → 10.0, 11.0
  // (mean over pre=0 only = (1+2)/2 = 1.5)
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  spiking_connect(syn, 2, 1, 5.0F)
  spiking_connect(syn, 2, 2, 6.0F)
  spiking_connect(syn, 3, 1, 10.0F)
  spiking_connect(syn, 3, 2, 11.0F)
  let m = average_weight(pre_pop, post_pop, syn)
  if (m - 1.5F).abs() > 1.0e-6 { fail("expected 1.5F (pre=0 only), got \{m}") }
}

///|
test "average_weight — pre=full, post=subset (filters post)" {
  let pre_pop : Array[Int] = [0, 1]
  let post_pop : Array[Int] = [1]  // only post=1
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 4.0F)
  spiking_connect(syn, 2, 1, 3.0F)
  spiking_connect(syn, 2, 2, 2.0F)
  // Mean of edges to post=1: (4.0 + 2.0) / 2 = 3.0
  let m = average_weight(pre_pop, post_pop, syn)
  if (m - 3.0F).abs() > 1.0e-6 { fail("expected 3.0F, got \{m}") }
}

///|
test "average_weight — out-of-bounds post filter returns 0.0" {
  let pre_pop : Array[Int] = [0]
  let post_pop : Array[Int] = [5]  // out of bounds for post.n=2
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  let m = average_weight(pre_pop, post_pop, syn)
  // No edges match → 0.0F (count=0 returns 0.0F)
  if m != 0.0F { fail("expected 0.0F for out-of-bounds post filter, got \{m}") }
}

///|
test "average_weight_dynamics — uniform weights, 3 timesteps" {
  let pre_pop : Array[Int] = [0, 1]
  let post_pop : Array[Int] = [0]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)  // edge 0 (pre=0, post=0)
  spiking_connect(syn, 1, 2, 0.0F)  // edge 1 (pre=0, post=1)
  spiking_connect(syn, 2, 1, 0.5F)  // edge 2 (pre=1, post=0)
  spiking_connect(syn, 2, 2, 0.0F)  // edge 3 (pre=1, post=1)
  // record is 4×3 = 12 floats (row-major: edge × timestep)
  let record : Array[Float] = [
    1.0F, 1.1F, 1.2F,    // edge 0
    0.0F, 0.0F, 0.0F,    // edge 1
    0.5F, 0.6F, 0.7F,    // edge 2
    0.0F, 0.0F, 0.0F,    // edge 3
  ]
  let dyn = average_weight_dynamics(pre_pop, post_pop, syn, record, 3)
  if dyn.length() != 3 { fail("expected 3 elements, got \{dyn.length()}") }
  // t=0: (1.0 + 0.5) / 2 = 0.75
  // t=1: (1.1 + 0.6) / 2 = 0.85
  // t=2: (1.2 + 0.7) / 2 = 0.95
  if (dyn[0] - 0.75F).abs() > 1.0e-6 { fail("dyn[0] expected 0.75, got \{dyn[0]}") }
  if (dyn[1] - 0.85F).abs() > 1.0e-6 { fail("dyn[1] expected 0.85, got \{dyn[1]}") }
  if (dyn[2] - 0.95F).abs() > 1.0e-6 { fail("dyn[2] expected 0.95, got \{dyn[2]}") }
}

///|
test "average_weight_dynamics — no matched edges returns zeros" {
  let pre_pop : Array[Int] = []
  let post_pop : Array[Int] = []
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  let record : Array[Float] = [1.0F, 2.0F, 3.0F, 4.0F]
  let dyn = average_weight_dynamics(pre_pop, post_pop, syn, record, 2)
  if dyn.length() != 2 { fail("expected 2 elements, got \{dyn.length()}") }
  if dyn[0] != 0.0F { fail("dyn[0] expected 0.0F, got \{dyn[0]}") }
  if dyn[1] != 0.0F { fail("dyn[1] expected 0.0F, got \{dyn[1]}") }
}

///|
test "weights_indices — full connectivity" {
  let pre_pop : Array[Int] = [0, 1]
  let post_pop : Array[Int] = [0, 1]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  spiking_connect(syn, 2, 1, 3.0F)
  spiking_connect(syn, 2, 2, 4.0F)
  let indices = weights_indices(pre_pop, post_pop, syn)
  if indices.length() != 4 { fail("expected 4 indices, got \{indices.length()}") }
}

///|
test "weights_indices — partial filter (pre=0 only)" {
  let pre_pop : Array[Int] = [0]
  let post_pop : Array[Int] = [0, 1]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 1, 2, 2.0F)
  spiking_connect(syn, 2, 1, 3.0F)
  spiking_connect(syn, 2, 2, 4.0F)
  let indices = weights_indices(pre_pop, post_pop, syn)
  // Only edges from pre=0 (post=0,1): 2 edges.
  if indices.length() != 2 { fail("expected 2 indices, got \{indices.length()}") }
}

///|
test "update_weight — multiplies matched edges by factor" {
  let pre_pop : Array[Int] = [0]
  let post_pop : Array[Int] = [0, 1]
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)  // edge 0
  spiking_connect(syn, 1, 2, 2.0F)  // edge 1
  spiking_connect(syn, 2, 1, 3.0F)  // edge 2
  spiking_connect(syn, 2, 2, 4.0F)  // edge 3
  update_weight(pre_pop, post_pop, 2.0F, syn)
  // Edges 0, 1 (from pre=0) are multiplied by 2; edges 2, 3 unchanged.
  if syn.matrix.vals[0] != 2.0F { fail("edge 0 expected 2.0, got \{syn.matrix.vals[0]}") }
  if syn.matrix.vals[1] != 4.0F { fail("edge 1 expected 4.0, got \{syn.matrix.vals[1]}") }
  if syn.matrix.vals[2] != 3.0F { fail("edge 2 expected 3.0 (unchanged), got \{syn.matrix.vals[2]}") }
  if syn.matrix.vals[3] != 4.0F { fail("edge 3 expected 4.0 (unchanged), got \{syn.matrix.vals[3]}") }
}

///|
test "update_weight — no matched edges is no-op" {
  let pre_pop : Array[Int] = []
  let post_pop : Array[Int] = []
  let rng = Xoshiro::new(42UL)
  let pre = IF::new(2, IFParameter::new(), rng)
  let post = IF::new(2, IFParameter::new(), rng)
  let syn = SpikingSynapse::new(pre, post, "ge")
  spiking_connect(syn, 1, 1, 1.0F)
  spiking_connect(syn, 2, 2, 4.0F)
  update_weight(pre_pop, post_pop, 10.0F, syn)
  // Both edges unchanged.
  if syn.matrix.vals[0] != 1.0F { fail("edge 0 should be unchanged") }
  if syn.matrix.vals[1] != 4.0F { fail("edge 1 should be unchanged") }
}