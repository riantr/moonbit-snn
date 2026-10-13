// sum_tree.mbt — Segmented sum-tree for proportional prioritized sampling.
//
// Reference: Schaul et al. 2016, "Prioritized Experience Replay" (PER), ICLR.
//
// A sum-tree is a complete binary tree stored in a flat array:
//   - Indices [0..n_leaves)        hold the *leaf* priorities p_i (data slots)
//   - Indices [n_leaves..2*n-1)    hold internal nodes, each = sum(children)
//   - Index 0                      is the root, holding Σ p_i (total priority).
//
// Sampling in O(log N):
//   1. Draw u ~ Uniform(0, total)
//   3. Walk down from root: at each internal node, compare u with left child's
//      sum; if u <= left, descend left and keep u; else descend right with
//      u -= left_sum. The leaf reached is the sampled transition.
//
// Update is O(log N): propagate the priority delta up from the leaf to root.
//
// Tree layout (capacity = 4 leaves):
//
//        tree[0] = total
//       /        \
//   tree[1]      tree[2]
//   /   \       /    \
// t[3]  t[4]  t[5]  t[6]
//  p_0  p_1   p_2   p_3
//
// Capacity must be a power of two for a perfectly balanced tree, but the code
// below accepts any positive capacity by treating `tree` of size `2 * cap - 1`
// with explicit child index math.

///|
/// Flat-array sum tree. Holds up to `capacity` non-negative priorities.
pub struct SumTree {
  capacity : Int
  tree : Array[Float]
  mut data_count : Int
}

///|
/// Create an empty sum tree. All priorities start at 0.
pub fn SumTree::new(capacity : Int) -> SumTree {
  // Need 2*capacity - 1 slots: (capacity - 1) internal + capacity leaves.
  let n_total = if capacity <= 0 { 1 } else { 2 * capacity - 1 }
  let tree : Array[Float] = Array::make(n_total, 0.0F)
  { capacity, tree, data_count: 0 }
}

///|
/// Number of priority slots (capacity in leaves).
pub fn SumTree::len(self : SumTree) -> Int {
  self.data_count
}

///|
/// Underlying slot capacity (in leaves). Slots are filled lazily as data is added.
pub fn SumTree::capacity(self : SumTree) -> Int {
  self.capacity
}

///|
/// Total priority sum = root node.
pub fn SumTree::total(self : SumTree) -> Float {
  if self.tree.length() == 0 {
    0.0F
  } else {
    self.tree[0]
  }
}

///|
/// Get priority at a leaf slot index (NOT a tree index). Leaf i is stored at
/// tree index `capacity - 1 + i` in our layout.
pub fn SumTree::get_leaf(self : SumTree, slot : Int) -> Float {
  let ti = self.leaf_to_tree(slot)
  if ti < 0 || ti >= self.tree.length() {
    0.0F
  } else {
    self.tree[ti]
  }
}

///|
/// Internal helper: leaf slot → tree index.
fn SumTree::leaf_to_tree(self : SumTree, slot : Int) -> Int {
  self.capacity - 1 + slot
}

///|
/// Internal helper: tree index → leaf slot.
fn SumTree::tree_to_leaf(self : SumTree, tree_idx : Int) -> Int {
  tree_idx - (self.capacity - 1)
}

///|
/// Propagate a priority delta from a tree index up to the root.
fn SumTree::propagate(self : SumTree, tree_idx_in : Int, delta : Float) -> Unit {
  let mut tree_idx = tree_idx_in
  while tree_idx > 0 {
    let parent = (tree_idx - 1) / 2
    self.tree[parent] = self.tree[parent] + delta
    tree_idx = parent
  }
}

///|
/// Set priority at a tree index, propagating the delta up to the root.
/// Caller computes delta as (new - old) — this method does NOT read the
/// existing value, which keeps the cost O(log N) without an extra read.
pub fn SumTree::set_priority(
  self : SumTree,
  tree_idx : Int,
  new_priority : Float,
) -> Unit {
  let old = self.tree[tree_idx]
  let delta = new_priority - old
  self.tree[tree_idx] = new_priority
  if delta != 0.0F {
    self.propagate(tree_idx, delta)
  }
}

///|
/// Add a new priority to the next empty leaf slot (FIFO order). Returns the
/// tree index of the leaf, or -1 if the tree is at full capacity.
///
/// On overwrite (when data_count == capacity), the call is a no-op and
/// returns -1. Use `set_at(slot, ...)` to overwrite an existing slot.
pub fn SumTree::add(self : SumTree, priority : Float) -> Int {
  if self.data_count >= self.capacity {
    return -1
  }
  let slot = self.data_count
  let tree_idx = self.leaf_to_tree(slot)
  let old = self.tree[tree_idx]
  let delta = priority - old
  self.tree[tree_idx] = priority
  if delta != 0.0F {
    self.propagate(tree_idx, delta)
  }
  self.data_count = self.data_count + 1
  tree_idx
}

///|
/// Set priority at an existing leaf slot (used to update after TD-error updates).
pub fn SumTree::set_at(
  self : SumTree,
  slot : Int,
  new_priority : Float,
) -> Unit {
  let tree_idx = self.leaf_to_tree(slot)
  self.set_priority(tree_idx, new_priority)
}

///|
/// Cumulative-sum lookup: find the leaf whose prefix sum covers `target_sum`.
///
/// Algorithm (Schaul 2016, Algorithm 1, simplified):
///   parent = root (idx 0)
///
///   loop:
///     left  = 2*parent + 1
///     right = 2*parent + 2
///     if left >= len(tree):  // parent is a leaf
///       return (parent, tree[parent])
///     if target_sum <= tree[left]:
///       parent = left
///     else:
///       target_sum -= tree[left]
///       parent = right
///
/// Returns (tree_idx, priority_at_that_leaf). If `target_sum` exceeds the
/// total, the rightmost leaf is returned.
pub fn SumTree::find_prefix(self : SumTree, target_sum_in : Float) -> (Int, Float) {
  let n = self.tree.length()
  if n == 0 || self.data_count == 0 {
    return (0, 0.0F)
  }
  let mut parent = 0
  let mut target_sum = target_sum_in
  let total = self.tree[0]
  if target_sum > total {
    target_sum = total
  }
  if target_sum < 0.0F {
    target_sum = 0.0F
  }
  // Walk down until parent points at a leaf.
  while true {
    let left = 2 * parent + 1
    if left >= n {
      // parent is a leaf.
      return (parent, self.tree[parent])
    }
    let right = left + 1
    let left_sum = self.tree[left]
    if target_sum <= left_sum {
      parent = left
    } else {
      target_sum = target_sum - left_sum
      parent = right
    }
  }
  // Unreachable; the loop always returns from inside.
  (0, 0.0F)
}

///|
/// Batch cumulative-sum lookup: draw `n_segments` uniform segments over [0, sum)
/// and return one leaf per segment. Segments partition [0, total] evenly, so
/// over many calls each leaf's selection probability approaches p_i / Σp_j.
pub fn SumTree::find_prefix_batch(
  self : SumTree,
  segment_values : Array[Float],
) -> Array[(Int, Float)] {
  let n = segment_values.length()
  let out : Array[(Int, Float)] = Array::make(n, (0, 0.0F))
  for k in 0..<n {
    let (ti, p) = self.find_prefix(segment_values[k])
    out[k] = (ti, p)
  }
  out
}