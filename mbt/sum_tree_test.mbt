// sum_tree_test.mbt — Unit tests for the segmented sum tree.

///|
test "sum_tree_new_empty" {
  let t = SumTree::new(4)
  assert_eq(t.len(), 0)
  assert_eq(t.capacity(), 4)
  assert_eq(t.total(), 0.0F)
}

///|
test "sum_tree_add_basic" {
  let t = SumTree::new(4)
  let _ = t.add(1.0F)
  let _ = t.add(2.0F)
  let _ = t.add(3.0F)
  let _ = t.add(4.0F)
  assert_eq(t.len(), 4)
  let total = t.total()
  let total_expected = 10.0F
  let diff = if total > total_expected {
    total - total_expected
  } else {
    total_expected - total
  }
  assert_true(diff < 1.0e-5F)
}

///|
test "sum_tree_add_overflow_returns_neg_one" {
  let t = SumTree::new(2)
  let _ = t.add(1.0F)
  let _ = t.add(1.0F)
  let r = t.add(1.0F)
  assert_eq(r, -1)
  assert_eq(t.len(), 2)
}

///|
test "sum_tree_get_leaf_round_trip" {
  let t = SumTree::new(4)
  let _ = t.add(0.5F)
  let _ = t.add(1.5F)
  let _ = t.add(2.5F)
  let _ = t.add(3.5F)
  let p0 = t.get_leaf(0)
  let p1 = t.get_leaf(1)
  let p2 = t.get_leaf(2)
  let p3 = t.get_leaf(3)
  let diff0 = if p0 > 0.5F { p0 - 0.5F } else { 0.5F - p0 }
  let diff1 = if p1 > 1.5F { p1 - 1.5F } else { 1.5F - p1 }
  let diff2 = if p2 > 2.5F { p2 - 2.5F } else { 2.5F - p2 }
  let diff3 = if p3 > 3.5F { p3 - 3.5F } else { 3.5F - p3 }
  assert_true(diff0 < 1.0e-5F)
  assert_true(diff1 < 1.0e-5F)
  assert_true(diff2 < 1.0e-5F)
  assert_true(diff3 < 1.0e-5F)
}

///|
test "sum_tree_set_at_updates_total" {
  let t = SumTree::new(4)
  let _ = t.add(1.0F)
  let _ = t.add(1.0F)
  let _ = t.add(1.0F)
  let _ = t.add(1.0F)
  let total_before = t.total()
  let diff_before = if total_before > 4.0F {
    total_before - 4.0F
  } else {
    4.0F - total_before
  }
  assert_true(diff_before < 1.0e-5F)
  t.set_at(2, 5.0F)
  let total_after = t.total()
  let diff_after = if total_after > 8.0F {
    total_after - 8.0F
  } else {
    8.0F - total_after
  }
  assert_true(diff_after < 1.0e-5F)
  let p2 = t.get_leaf(2)
  let p2_diff = if p2 > 5.0F { p2 - 5.0F } else { 5.0F - p2 }
  assert_true(p2_diff < 1.0e-5F)
}

///|
test "sum_tree_find_prefix_returns_leaf" {
  let t = SumTree::new(4)
  let _ = t.add(1.0F)
  let _ = t.add(2.0F)
  let _ = t.add(3.0F)
  let _ = t.add(4.0F)
  // Total = 10. Cumulative prefix sums: [0, 1, 3, 6, 10).
  // Target 0.5 → first leaf (priority 1).
  let (ti0, p0) = t.find_prefix(0.5F)
  let _ = ti0
  let diff0 = if p0 > 1.0F { p0 - 1.0F } else { 1.0F - p0 }
  assert_true(diff0 < 1.0e-5F)
  // Target 1.5 → second leaf (priority 2).
  let (_, p1) = t.find_prefix(1.5F)
  let diff1 = if p1 > 2.0F { p1 - 2.0F } else { 2.0F - p1 }
  assert_true(diff1 < 1.0e-5F)
  // Target 5.0 → third leaf (priority 3).
  let (_, p2) = t.find_prefix(5.0F)
  let diff2 = if p2 > 3.0F { p2 - 3.0F } else { 3.0F - p2 }
  assert_true(diff2 < 1.0e-5F)
  // Target 9.9 → fourth leaf (priority 4).
  let (_, p3) = t.find_prefix(9.9F)
  let diff3 = if p3 > 4.0F { p3 - 4.0F } else { 4.0F - p3 }
  assert_true(diff3 < 1.0e-5F)
}

///|
test "sum_tree_find_prefix_clamps_target" {
  let t = SumTree::new(2)
  let _ = t.add(1.0F)
  let _ = t.add(1.0F)
  // Target > total should still return a valid leaf.
  let (_, p) = t.find_prefix(999.0F)
  let diff = if p > 1.0F { p - 1.0F } else { 1.0F - p }
  assert_true(diff < 1.0e-5F)
}

///|
test "sum_tree_find_prefix_empty_tree" {
  let t = SumTree::new(4)
  let (ti, p) = t.find_prefix(0.5F)
  assert_eq(ti, 0)
  assert_eq(p, 0.0F)
}

///|
test "sum_tree_find_prefix_batch_returns_n_results" {
  let t = SumTree::new(4)
  let _ = t.add(1.0F)
  let _ = t.add(2.0F)
  let _ = t.add(3.0F)
  let _ = t.add(4.0F)
  let segs : Array[Float] = [0.5F, 2.5F, 5.0F, 9.0F]
  let results = t.find_prefix_batch(segs)
  assert_eq(results.length(), 4)
  // Verify each returned a positive priority.
  for k in 0..<4 {
    let (_, p) = results[k]
    assert_true(p > 0.0F)
  }
}

///|
test "sum_tree_capacity_one" {
  let t = SumTree::new(1)
  let _ = t.add(7.5F)
  assert_eq(t.len(), 1)
  let total = t.total()
  let diff = if total > 7.5F { total - 7.5F } else { 7.5F - total }
  assert_true(diff < 1.0e-5F)
  let (_, p) = t.find_prefix(0.0F)
  let p_diff = if p > 7.5F { p - 7.5F } else { 7.5F - p }
  assert_true(p_diff < 1.0e-5F)
}