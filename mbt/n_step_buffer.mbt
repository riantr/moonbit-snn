// n_step_buffer.mbt — Fixed-window buffer for n-step return computation.
//
// An N-step transition packs together the first state and action of a window
// of `n` consecutive environment transitions, with the discounted sum of all
// rewards and the (n+1)-th state as the lookahead. When the lookahead corresponds
// to a terminal state, the bootstrap term is zero (done = true).
//
// This buffer holds up to `n` consecutive (s, a, r, s', done) tuples in a
// FIFO ring. After each push, if it contains at least `n` tuples, the caller
// can call `make_n_step_transition(...)` to extract the n-step transition whose
// root is `n` pushes back.

///|
/// Per-step transition tuple stored in the n-step buffer.
pub struct StepRecord {
  s : Int
  a : Int
  r : Float
  s_next : Int
  done : Bool
}

///|
/// Circular buffer of recent step records.
pub struct NStepBuffer {
  n : Int
  records : Array[StepRecord]
  mut count : Int
  mut head : Int // index of oldest record
}

///|
/// Create an empty N-step buffer holding at most `n` records.
pub fn NStepBuffer::new(n : Int) -> NStepBuffer {
  if n < 1 {
    abort("NStepBuffer::new: n must be >= 1")
  }
  let recs : Array[StepRecord] = []
  for _k in 0..<n {
    recs.push({ s: 0, a: 0, r: 0.0F, s_next: 0, done: false })
  }
  { n, records: recs, count: 0, head: 0 }
}

///|
/// Number of records currently in the buffer (≤ n).
pub fn NStepBuffer::len(self : NStepBuffer) -> Int {
  self.count
}

///|
/// Window size (capacity).
pub fn NStepBuffer::capacity(self : NStepBuffer) -> Int {
  self.n
}

///|
/// Push a new step record, overwriting the oldest if the buffer is full.
/// Returns true if the buffer now has `n` records (i.e., a complete n-step
/// window is available for extraction).
pub fn NStepBuffer::push(
  self : NStepBuffer,
  s : Int,
  a : Int,
  r : Float,
  s_next : Int,
  done : Bool,
) -> Bool {
  // Write into the slot at (head + count) % n (oldest if full, else tail).
  let write_at = if self.count < self.n {
    self.head + self.count
  } else {
    self.head
  }
  // Modulo write_at into [0, n).
  let write_idx = write_at - (write_at / self.n) * self.n
  self.records[write_idx] = { s, a, r, s_next, done }
  if self.count < self.n {
    self.count = self.count + 1
  } else {
    // Buffer full: advance head to drop the oldest.
    self.head = self.head + 1
    let _ = self.head - (self.head / self.n) * self.n
    self.head = self.head - (self.head / self.n) * self.n
  }
  // Return whether we now hold a full window.
  self.count == self.n
}

///|
/// Read the i-th record in chronological order (0 = oldest, count-1 = newest).
/// Returns a copy of the record; the caller cannot mutate the buffer via it.
pub fn NStepBuffer::at(self : NStepBuffer, i : Int) -> StepRecord {
  let idx = self.head + i
  let wrapped = idx - (idx / self.n) * self.n
  self.records[wrapped]
}

///|
/// Empty the buffer (used at episode boundaries when we flush remaining
/// transitions to the replay buffer with shorter effective horizons).
pub fn NStepBuffer::reset(self : NStepBuffer) -> Unit {
  self.count = 0
  self.head = 0
}

///|
/// Compute the n-step return for the transition rooted at record index `root`
/// (0 = oldest in the buffer).
///
/// Given records R_root, R_root+1, ..., R_root+n-1, the n-step return is
///
///     G_n = Σ_{k=0..n-1} γ^k · r_{root+k}
///
/// If any intermediate step in the window is terminal (`done = true`), the
/// sum is truncated at the terminal step and the bootstrap is suppressed (the
/// Q-value of s_{terminal+1} is treated as zero).
///
/// Also returns the effective horizon `h` (number of rewards included, 1..=n)
/// and the terminal flag — the caller uses these to decide whether to use the
/// standard n-step bootstrap (h == n, !done) or the truncated form (h < n).
pub fn NStepBuffer::n_step_return(
  self : NStepBuffer,
  root : Int,
  gamma : Float,
) -> (Float, Int, Int, Int, Bool) {
  // Returns: (G_n, h_eff, s_root, s_lookahead, truncated_at_terminal)
  let s_root = self.records[(self.head + root) - ((self.head + root) / self.n) * self.n].s
  let mut g = 0.0F
  let mut disc = 1.0F
  let mut truncated = false
  let mut h_eff = self.n
  let mut s_lookahead = s_root
  // Only iterate over the actual records currently in the buffer (count),
  // not the underlying capacity (which may contain stale zero-initialised
  // records when the buffer is partial).
  let upper = if self.count < self.n { self.count } else { self.n }
  for k in 0..<upper {
    let idx_raw = self.head + root + k
    let idx = idx_raw - (idx_raw / self.n) * self.n
    let rec = self.records[idx]
    g = g + disc * rec.r
    disc = disc * gamma
    s_lookahead = rec.s_next
    if rec.done {
      h_eff = k + 1
      truncated = true
      break
    }
  }
  // h_eff defaults to the number of available records when not truncated.
  if !truncated && h_eff > upper {
    h_eff = upper
  }
  (g, h_eff, s_root, s_lookahead, truncated)
}