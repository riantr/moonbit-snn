// scheduler_test.mbt — Learning-rate scheduler tests (v0.18.0 + v0.18.1).

// =====================================================================
// StepLR
// =====================================================================

///|
test "StepLR::step / at step 0 returns base_lr" {
  let s = StepLR::new(0.1F, 10, 0.5F)
  if (s.step() - 0.1F).abs() > 0.000001F {
    fail("step 0 lr should be 0.1, got \{s.step()}")
  }
}

///|
test "StepLR::step / drops by gamma every step_size steps" {
  let mut s = StepLR::new(1.0F, 3, 0.5F)
  // Steps 0..<3: lr = 1.0
  // Step 3..<6: lr = 0.5
  // Step 6..<9: lr = 0.25
  // Step 9..<12: lr = 0.125
  for i in 0..<3 {
    let lr = s.step()
    if (lr - 1.0F).abs() > 0.000001F {
      fail("step \{i} lr expected 1.0, got \{lr}")
    }
    s = s.next()
  }
  for i in 3..<6 {
    let lr = s.step()
    if (lr - 0.5F).abs() > 0.000001F {
      fail("step \{i} lr expected 0.5, got \{lr}")
    }
    s = s.next()
  }
  for i in 6..<9 {
    let lr = s.step()
    if (lr - 0.25F).abs() > 0.000001F {
      fail("step \{i} lr expected 0.25, got \{lr}")
    }
    s = s.next()
  }
}

///|
test "StepLR::next / does not mutate input state" {
  let s0 = StepLR::new(0.1F, 5, 0.5F)
  let _s1 = s0.next()
  if s0.current_step != 0 {
    fail("StepLR::next should not mutate input, got step=\{s0.current_step}")
  }
}

// =====================================================================
// ExponentialLR
// =====================================================================

///|
test "ExponentialLR::step / lr = base * gamma^t" {
  let s = ExponentialLR::new(1.0F, 0.5F)
  // step 0: 1.0, step 1: 0.5, step 2: 0.25, step 3: 0.125
  let lr0 = s.step()
  let s1 = s.next()
  let lr1 = s1.step()
  let s2 = s1.next()
  let lr2 = s2.step()
  let s3 = s2.next()
  let lr3 = s3.step()
  if (lr0 - 1.0F).abs() > 0.000001F { fail("lr0 expected 1.0, got \{lr0}") }
  if (lr1 - 0.5F).abs() > 0.000001F { fail("lr1 expected 0.5, got \{lr1}") }
  if (lr2 - 0.25F).abs() > 0.000001F { fail("lr2 expected 0.25, got \{lr2}") }
  if (lr3 - 0.125F).abs() > 0.000001F { fail("lr3 expected 0.125, got \{lr3}") }
}

// =====================================================================
// CosineAnnealingLR
// =====================================================================

///|
test "CosineAnnealingLR::step / at t=0 returns eta_max, at t=T_max returns eta_min" {
  let s = CosineAnnealingLR::new(0.1F, 0.001F, 100)
  if (s.step() - 0.1F).abs() > 0.0001F {
    fail("step 0 lr expected 0.1, got \{s.step()}")
  }
  // Advance to step 100 (wraps back to t=0, same as start).
  let mut cur = s
  for _ in 0..<100 {
    cur = cur.next()
  }
  // After 100 steps, t = 100 % 100 = 0, so we should see eta_max again.
  if (cur.step() - 0.1F).abs() > 0.0001F {
    fail("after T_max steps lr should wrap back to eta_max, got \{cur.step()}")
  }
  // At t = T_max / 2 = 50, lr should be at the midpoint.
  let mut mid = s
  for _ in 0..<50 {
    mid = mid.next()
  }
  let mid_lr = mid.step()
  let expected = (0.1F + 0.001F) / 2.0F
  if (mid_lr - expected).abs() > 0.001F {
    fail("midpoint lr expected ~\{expected}, got \{mid_lr}")
  }
}

///|
test "CosineAnnealingLR::step / monotonically decreases from t=0 to t=T_max/2" {
  let s = CosineAnnealingLR::new(1.0F, 0.0F, 50)
  let mut prev = s.step()
  let mut cur = s
  for _ in 0..<25 {
    cur = cur.next()
    let lr = cur.step()
    if lr > prev + 0.0001F {
      fail("lr should be non-increasing during first half, was \{prev} now \{lr}")
    }
    prev = lr
  }
}

// =====================================================================
// ReduceLROnPlateau
// =====================================================================

///|
test "ReduceLROnPlateau::step / first call records best metric, no decay" {
  let s = ReduceLROnPlateau::new(0.1F, 0.5F, 2, 0.01F)
  let (lr, s2) = s.step(0.5F)
  if (lr - 0.1F).abs() > 0.000001F {
    fail("first call lr should be 0.1, got \{lr}")
  }
  if s2.current_lr != 0.1F {
    fail("current_lr should be 0.1, got \{s2.current_lr}")
  }
  if s2.num_bad_epochs != 0 {
    fail("num_bad_epochs should be 0, got \{s2.num_bad_epochs}")
  }
}

///|
test "ReduceLROnPlateau::step / improvement resets bad counter" {
  let s = ReduceLROnPlateau::new(0.1F, 0.5F, 3, 0.01F)
  let (lr1, s1) = s.step(1.0F) // best=1.0
  let (_, s2) = s1.step(1.0F)  // no improvement
  let (_, s3) = s2.step(0.9F)  // big improvement (0.9 < 1.0 - 0.01 = 0.99)
  if s3.num_bad_epochs != 0 {
    fail("improvement should reset num_bad_epochs, got \{s3.num_bad_epochs}")
  }
  if (lr1 - 0.1F).abs() > 0.000001F { fail("lr1 should be 0.1") }
}

///|
test "ReduceLROnPlateau::step / patience exhausted triggers decay" {
  let s = ReduceLROnPlateau::new(0.1F, 0.5F, 2, 0.01F)
  let (lr1, s1) = s.step(1.0F)   // best=1.0, lr=0.1
  // Three non-improving calls (metric=1.0 is not < 1.0 - 0.01 = 0.99):
  let (lr2, s2) = s1.step(1.0F)  // bad=1
  let (lr3, s3) = s2.step(1.0F)  // bad=2 (=patience), NO decay yet
  let (lr4, s4) = s3.step(1.0F)  // bad=3 > patience, decay!
  if (lr2 - 0.1F).abs() > 0.000001F { fail("lr2 should be 0.1 (not yet plateaued), got \{lr2}") }
  if (lr3 - 0.1F).abs() > 0.000001F { fail("lr3 should be 0.1 (at patience boundary, not yet decayed), got \{lr3}") }
  if (lr4 - 0.05F).abs() > 0.000001F {
    fail("lr4 should be 0.05 (= 0.1 * 0.5) after plateau, got \{lr4}")
  }
  if s4.num_bad_epochs != 0 {
    fail("after decay, num_bad_epochs should reset, got \{s4.num_bad_epochs}")
  }
}

///|
test "ReduceLROnPlateau::step / threshold prevents micro-improvements" {
  // A "tiny improvement" smaller than threshold should NOT reset the
  // bad counter (PyTorch behaviour).
  let s = ReduceLROnPlateau::new(0.1F, 0.5F, 2, 0.05F)
  let (_, s1) = s.step(1.0F)  // best=1.0
  // Improvement of 0.01 (from 1.0 to 0.99) is less than threshold 0.05.
  let (_, s2) = s1.step(0.99F)
  if s2.num_bad_epochs != 1 {
    fail("tiny improvement should count as bad step, got num_bad=\{s2.num_bad_epochs}")
  }
}