// Tests for PoissonLayer and PoissonLayerStimulus.
//
// Mirrors SpikingNeuralNetworks.jl/test/stim/poisson_layer.jl but
// using MoonBit struct-literal syntax (since `PoissonLayer` is
// `pub(all)`).

///|
test "PoissonLayer::new: defaults" {
  let p = PoissonLayer::new(10.0F)
  if p.rate != 10.0F {
    fail("rate = \{p.rate}, expected 10.0")
  }
  if p.n_sources != 1 {
    fail("n_sources = \{p.n_sources}, expected 1")
  }
  if p.active.length() != 1 {
    fail("active.length = \{p.active.length()}, expected 1")
  }
  if !p.active[0] {
    fail("active[0] should default to true")
  }
  if p.dist != "Fixed" {
    fail("dist = \{p.dist}, expected Fixed")
  }
  if p.rule != "Fixed" {
    fail("rule = \{p.rule}, expected Fixed")
  }
}

///|
test "PoissonLayer::with_n: N sources all active" {
  let p = PoissonLayer::with_n(2.0F, 10)
  if p.n_sources != 10 {
    fail("n_sources = \{p.n_sources}, expected 10")
  }
  if p.active.length() != 10 {
    fail("active.length = \{p.active.length()}, expected 10")
  }
  for i in 0..<10 {
    if !p.active[i] {
      fail("active[\{i}] should be true by default")
    }
  }
}

///|
test "PoissonLayer::with_active: respects inactive flags" {
  let active : Array[Bool] = [false]
  let p = PoissonLayer::with_active(2.0F, 1, active)
  if p.active[0] {
    fail("active[0] should be false")
  }
}

///|
test "PoissonLayer::with_conn: full parameter set" {
  let active : Array[Bool] = [true, true, true]
  let p = PoissonLayer::with_conn(
    5.0F, 3, active, 2.5F, 1.0F, 0.4F, "Normal", "Fixed",
  )
  if p.rate != 5.0F {
    fail("rate = \{p.rate}, expected 5.0")
  }
  if p.n_sources != 3 {
    fail("n_sources = \{p.n_sources}, expected 3")
  }
  if p.mu != 2.5F {
    fail("mu = \{p.mu}, expected 2.5")
  }
  if p.sigma != 1.0F {
    fail("sigma = \{p.sigma}, expected 1.0")
  }
  if p.p != 0.4F {
    fail("p = \{p.p}, expected 0.4")
  }
  if p.dist != "Normal" {
    fail("dist = \{p.dist}, expected Normal")
  }
}

///|
test "PoissonLayerStimulus::new: weight/connectivity shapes" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(4, if_param, rng)
  let param = PoissonLayer::with_n(2.0F, 3)
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  if s.fire.length() != 3 {
    fail("fire.length = \{s.fire.length()}, expected 3")
  }
  if s.weights.length() != 4 * 3 {
    fail("weights.length = \{s.weights.length()}, expected 12 (4 post × 3 pre)")
  }
  if s.connectivity.length() != 4 * 3 {
    fail("connectivity.length = \{s.connectivity.length()}, expected 12")
  }
  if s.sym != "ge" {
    fail("sym = \{s.sym}, expected ge")
  }
}

///|
test "PoissonLayerStimulus::new: Fixed weights = μ" {
  // Fixed: weights must all equal mu where connected (no σ contribution).
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(5, if_param, rng)
  let param = PoissonLayer::with_conn(
    2.0F, 4, [true, true, true, true], 3.0F, 1.0F, 1.0F, "Fixed", "Fixed",
  )
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  // With p=1.0, all 20 connections exist, all weights = mu = 3.0
  for i in 0..<20 {
    if !s.connectivity[i] {
      fail("connection [\{i}] should exist (p=1)")
    }
    if (s.weights[i] - 3.0F).abs() > 1.0e-6F {
      fail("Fixed weight [\{i}] = \{s.weights[i]}, expected 3.0")
    }
  }
}

///|
test "PoissonLayerStimulus::new: Normal weights have mean ≈ μ" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(10, if_param, rng)
  let active : Array[Bool] = {
    let a : Array[Bool] = []
    for _ in 0..<20 {
      a.push(true)
    }
    a
  }
  // Box-Muller samples are independent N(0,1), so 200 samples should
  // give a sample mean within ≈ σ/sqrt(N) = 0.5/sqrt(200) ≈ 0.035 of μ.
  let param = PoissonLayer::with_conn(
    2.0F * hz, 20, active, 2.0F, 0.5F, 1.0F, "Normal", "Fixed",
  )
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  let mut sum = 0.0F
  let mut count = 0
  for i in 0..<s.weights.length() {
    if s.connectivity[i] {
      sum = sum + s.weights[i]
      count = count + 1
    }
  }
  if count != 200 {
    fail("expected 200 connections (p=1), got \{count}")
  }
  let mean = sum / Float::from_int(count)
  // Box-Muller gives a clean N(0,1). Tolerate 0.1 (~3σ with σ=0.5).
  if (mean - 2.0F).abs() > 0.1F {
    fail("Normal mean = \{mean}, expected ~2.0 (tol 0.1)")
  }
}

///|
test "stimulate_layer: zero rate → no fire, no conductance change" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(3, if_param, rng)
  let param = PoissonLayer::with_n(0.0F, 5)
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  stimulate_layer(s, 0.0F, 0.125F)
  for i in 0..<5 {
    if s.fire[i] {
      fail("fire[\{i}] should be false with rate=0")
    }
  }
  for j in 0..<3 {
    if post.glu[j] != 0.0F {
      fail("post.glu[\{j}] = \{post.glu[j]}, expected 0")
    }
  }
}

///|
test "stimulate_layer: inactive source → no fire" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(2, if_param, rng)
  let active : Array[Bool] = [false, true]
  // 10 kHz on source 1 (high enough to fire ~every step at dt=0.125ms).
  // Rate is in internal units: 10000 Hz × hz_unit = 10000 × 0.001 = 10.0
  let param = PoissonLayer::with_active(10000.0F * hz, 2, active)
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  let mut total_fires = 0
  for _ in 0..<100 {
    stimulate_layer(s, 0.0F, 0.125F)
    if s.fire[0] {
      fail("inactive source 0 should not fire")
    }
    if s.fire[1] {
      total_fires = total_fires + 1
    }
  }
  if total_fires == 0 {
    fail("active source 1 should fire sometimes at high rate")
  }
}

///|
test "stimulate_layer: high rate fires, weight accumulates on post" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(1, if_param, rng)
  // 1 kHz → rate_internal = 1.0, lambda = 1.0 × 0.125 = 0.125 spikes/step.
  // Over 1000 steps (~125 ms) expect ~125 fires × weight 2.0 = ~250.
  let param = PoissonLayer::with_conn(
    1000.0F * hz, 1, [true], 2.0F, 0.0F, 1.0F, "Fixed", "Fixed",
  )
  let s = PoissonLayerStimulus::new(param, post, "ge", rng)
  let mut total_added = 0.0F
  let steps = 1000
  for _ in 0..<steps {
    let before = post.glu[0]
    stimulate_layer(s, 0.0F, 0.125F)
    if s.fire[0] {
      let diff = post.glu[0] - before
      if (diff - 2.0F).abs() > 1.0e-5F {
        fail("per-fire delta = \{diff}, expected 2.0")
      }
      total_added = total_added + diff
    } else {
      if post.glu[0] != before {
        fail("non-fire step changed glu (\{before} → \{post.glu[0]})")
      }
    }
  }
  // Loose bounds: expect ~250 ± 100.
  if total_added < 150.0F {
    fail("total_added = \{total_added}, expected ~250 (rate × weight × time)")
  }
  if total_added > 400.0F {
    fail("total_added = \{total_added}, suspiciously high")
  }
}

///|
test "stimulate_layer: writes to gaba for :gi sym" {
  let rng = Xoshiro::default()
  let if_param = IFParameter::new()
  let post = IF::new(2, if_param, rng)
  // 1 kHz on 2 sources → ~12.5% fire rate each.
  let param = PoissonLayer::with_conn(
    1000.0F * hz, 2, [true, true], 1.5F, 0.0F, 1.0F, "Fixed", "Fixed",
  )
  let s = PoissonLayerStimulus::new(param, post, "gi", rng)
  let mut any_gaba_change = false
  for _ in 0..<200 {
    let g0_before = post.gaba[0]
    let g1_before = post.gaba[1]
    stimulate_layer(s, 0.0F, 0.125F)
    if post.gaba[0] != g0_before || post.gaba[1] != g1_before {
      any_gaba_change = true
      break
    }
  }
  if !any_gaba_change {
    fail("gaba should accumulate for :gi stim at high rate")
  }
  // Make sure glu stayed zero (since we're writing to gaba).
  for j in 0..<2 {
    if post.glu[j] != 0.0F {
      fail("glu[\{j}] = \{post.glu[j]}, should stay 0 for :gi stim")
    }
  }
}
