// SpikingSynapse — bit-exact port of SpikingSynapse + connect! helper.
//
// Stores connections in CSR (compressed sparse row) format using
// the SparseMatrixCSR type from sparse_matrix.mbt. The CSR layout
// gives O(out_degree) iteration for each pre-synaptic spike, which
// matches SNN's forward pass in SpikingSynapse.jl.
//
// Julia reference: src/connections/spiking_synapse.jl
//                  src/utils/sparse_matrix.jl
//                  src/connections/connections.jl (connect!)

///|
/// SpikingSynapse state — CSR sparse matrix of connection weights
/// plus the post-synaptic target's receptor (`ge` or `gi`).
///
/// If `delays` is non-empty, connections have per-edge delays (in
/// ms). When pre fires, the spike is scheduled for delivery at
/// `t + delays[s]` instead of being applied immediately. The
/// `pending_*` queues track scheduled events; `deliver_pending_synapse`
/// drains them when the current sim time reaches the delivery time.
///
/// If `rho` is non-empty, each connection has a per-edge weight
/// modifier (parallel to `matrix.vals`). The actual synaptic current
/// applied is `matrix.vals[s] * rho[s]`. This is used by Markram STP
/// to scale each spike by the pre-synaptic resource availability.
/// An empty `rho` means "no scaling" (rho defaults to 1.0F).
pub struct SpikingSynapse {
  pre : IF
  post : IF
  // Symbol: :ge or :gi
  sym : String
  // Optional human-readable label (mirrors Julia's `name` kwarg).
  // Defaults to "" if not set.
  name : String
  // CSR sparse matrix: rows = pre.n, cols = post.n
  matrix : SparseMatrixCSR
  // Per-connection delay in ms (parallel to matrix.vals). Empty
  // array means "no delay" — pre spikes deliver immediately.
  delays : Array[Float]
  // Per-connection weight modifier (parallel to matrix.vals). Empty
  // array means "no modifier" — full weight applied. When STP is
  // enabled, this is updated each step to reflect the Markram STP
  // scaling ρ = u * x for the connection's pre-synaptic neuron.
  rho : Array[Float]
  // Pending event queues (interleaved appends).
  pending_times : Array[Float]
  pending_posts : Array[Int]
  pending_weights : Array[Float]
}

///|
/// Construct an empty SpikingSynapse. Add connections with `connect!`
/// or `random_connections!`. No delays, no STP — pre spikes deliver
/// immediately at full weight.
pub fn SpikingSynapse::new(pre : IF, post : IF, sym : String) -> SpikingSynapse {
  let matrix = SparseMatrixCSR::empty(pre.n, post.n)
  {
    pre,
    post,
    sym,
    name: "",
    matrix,
    delays: [],
    rho: [],
    pending_times: [],
    pending_posts: [],
    pending_weights: [],
  }
}

///|
/// Set the human-readable label on a SpikingSynapse. Mirrors Julia's
/// `name = "..."` keyword argument. Returns a new struct (MoonBit fields
/// are immutable); the caller must rebind:
///
///     let s = s.with_name("my_syn")
pub fn SpikingSynapse::with_name(c : SpikingSynapse, name : String) -> SpikingSynapse {
  {
    pre: c.pre,
    post: c.post,
    sym: c.sym,
    name: name,
    matrix: c.matrix,
    delays: c.delays,
    rho: c.rho,
    pending_times: c.pending_times,
    pending_posts: c.pending_posts,
    pending_weights: c.pending_weights,
  }
}

///|
/// Add (or replace) a single connection `pre -> post` with weight `w`.
/// Matches Julia's `connect!(c, j, i, w)` (1-based post, 1-based pre)
/// but our API is 1-based pre, 1-based post to match SNN's
/// `connect!(EE, n, n+1, 50)` chain.jl usage.
pub fn spiking_connect(c : SpikingSynapse, pre : Int, post : Int, w : Float) -> Unit {
  let pre_idx = pre - 1
  let post_idx = post - 1
  c.matrix.set(pre_idx, post_idx, w)
}

///|
/// Set per-connection delays (parallel to matrix.vals, in ms).
/// Used for `delay_dist` support. After this call, pre spikes
/// are scheduled for delivery at `t + delays[s]` instead of
/// applied immediately.
pub fn SpikingSynapse::set_delays(c : SpikingSynapse, delays : Array[Float]) -> Unit {
  c.delays.clear()
  for d in delays {
    c.delays.push(d)
  }
}

///|
/// Set a constant delay (in ms) for every stored connection.
/// Mirrors Julia's `delay_dist = Normal(d_mean, 0)` constant case.
pub fn SpikingSynapse::set_constant_delay(c : SpikingSynapse, d : Float) -> Unit {
  let n = c.matrix.vals.length()
  c.delays.clear()
  for _ in 0..<n {
    c.delays.push(d)
  }
}

///|
/// Build a random connectivity matrix with mean weight mu, weight
/// std sigma, and connection probability p. Matches Julia's
/// `SpikingSynapse(pre, post, sym; conn = (mu, sigma, p))`.
/// Default rule is Bernoulli.
pub fn SpikingSynapse::random(
  pre : IF,
  post : IF,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> SpikingSynapse {
  let matrix = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  {
    pre,
    post,
    sym,
    name: "",
    matrix,
    delays: [],
    rho: [],
    pending_times: [],
    pending_posts: [],
    pending_weights: [],
  }
}

///|
/// Build a random connectivity matrix with an explicit connection
/// rule. Mirrors Julia's `SpikingSynapse(pre, post, sym; conn =
/// (mu, sigma, p, rule=:FixedIn))`. See `ConnectRule` for the semantics.
pub fn SpikingSynapse::random_with_rule(
  pre : IF,
  post : IF,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rule : ConnectRule,
  rng : Xoshiro,
) -> SpikingSynapse {
  let matrix = SparseMatrixCSR::random_with_rule(
    pre.n, post.n, mu, sigma, p, rule, rng,
  )
  {
    pre,
    post,
    sym,
    name: "",
    matrix,
    delays: [],
    rho: [],
    pending_times: [],
    pending_posts: [],
    pending_weights: [],
  }
}

///|
/// Build a random connectivity matrix with per-connection delays
/// sampled from a Normal distribution `(d_mean, d_std)`. Matches
/// Julia's `SpikingSynapse(...; delay_dist = Normal(d_mean, d_std))`.
///
/// If `d_std == 0.0F`, all delays are exactly `d_mean`. Delays are
/// clamped at >= 0 ms.
pub fn SpikingSynapse::random_with_delays(
  pre : IF,
  post : IF,
  sym : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
  d_mean : Float,
  d_std : Float,
) -> SpikingSynapse {
  let syn = SpikingSynapse::random(pre, post, sym, mu, sigma, p, rng)
  let n = syn.matrix.vals.length()
  syn.delays.clear()
  let mut k = 0
  while k < n {
    let (z1, _) = box_muller(rng)
    let d = d_mean + d_std * Float::from_double(z1)
    let clamped = if d < 0.0F { 0.0F } else { d }
    syn.delays.push(clamped)
    k = k + 1
  }
  syn
}

///|
/// Allocate the per-connection `rho` array (parallel to `matrix.vals`)
/// and initialise all entries to 1.0F. Call this once after building
/// the connectivity, before enabling STP. With `rho` non-empty,
/// `forward_synapse` will multiply each weight by `rho[s]` when
/// applying it to the post-synaptic receptor.
pub fn SpikingSynapse::init_rho(c : SpikingSynapse) -> Unit {
  let n = c.matrix.vals.length()
  c.rho.clear()
  let mut k = 0
  while k < n {
    c.rho.push(1.0F)
    k = k + 1
  }
}

///|
/// Forward a single time-step's spikes through the synapse. For each
/// pre-synaptic neuron that fired, add `w` to the post-synaptic
/// neuron's `glu` (for :ge) or `gaba` (for :gi) field.
///
/// If the synapse has non-empty `delays`, each spike is scheduled
/// for delivery at `t_now + delays[s]` instead of being applied
/// immediately. Use `deliver_pending_synapse` to drain pending events
/// when their delivery time arrives.
///
/// If the synapse has non-empty `rho`, each spike is scaled by
/// `rho[s]` (per-connection weight modifier, used by Markram STP).
pub fn forward_synapse(c : SpikingSynapse, t_now : Float) -> Unit {
  let use_delay = c.delays.length() > 0
  let use_rho = c.rho.length() > 0
  if use_delay {
    // Schedule events for each (pre-fire, outgoing-conn) pair.
    let n_pre = c.pre.fire.length()
    let mut j = 0
    while j < n_pre {
      if c.pre.fire[j] {
        let start = c.matrix.rowptr[j]
        let end = c.matrix.rowptr[j + 1]
        let mut s = start
        while s < end {
          let post_idx = c.matrix.colptr[s]
          let w = c.matrix.vals[s]
          let d = c.delays[s]
          // Apply rho scaling if STP is active.
          let w_scaled = if use_rho { w * c.rho[s] } else { w }
          if d == 0.0F {
            // No delay: deliver immediately.
            apply_weight(c, post_idx, w_scaled)
          } else {
            // Schedule for delivery.
            c.pending_times.push(t_now + d)
            c.pending_posts.push(post_idx)
            c.pending_weights.push(w_scaled)
          }
          s = s + 1
        }
      }
      j = j + 1
    }
  } else {
    // No delay: apply immediately (fast path).
    let target = if c.sym == "ge" { c.post.glu } else { c.post.gaba }
    if use_rho {
      // Manual loop with rho scaling (slower than matrix.forward).
      let n_pre = c.pre.fire.length()
      let mut j = 0
      while j < n_pre {
        if c.pre.fire[j] {
          let start = c.matrix.rowptr[j]
          let end = c.matrix.rowptr[j + 1]
          let mut s = start
          while s < end {
            let post_idx = c.matrix.colptr[s]
            let w_scaled = c.matrix.vals[s] * c.rho[s]
            target[post_idx] = target[post_idx] + w_scaled
            s = s + 1
          }
        }
        j = j + 1
      }
    } else {
      c.matrix.forward(c.pre.fire, target)
    }
  }
}

///|
/// Drain the pending event queue: apply weights for events whose
/// delivery time has arrived (<= t_now). Removes them from the
/// queue. Events are processed in insertion order; since the
/// compose sim loop advances `t_now` monotonically and each step
/// adds events at `t_now + d` for some `d > 0`, the queue is
/// effectively FIFO when processed each step.
pub fn deliver_pending_synapse(c : SpikingSynapse, t_now : Float) -> Unit {
  let n = c.pending_times.length()
  if n == 0 {
    return
  }
  let mut kept : Int = 0
  let mut k : Int = 0
  while k < n {
    if c.pending_times[k] <= t_now {
      // Deliver.
      apply_weight(c, c.pending_posts[k], c.pending_weights[k])
    } else {
      // Keep this event; shift it left if we've delivered earlier ones.
      if kept != k {
        c.pending_times[kept] = c.pending_times[k]
        c.pending_posts[kept] = c.pending_posts[k]
        c.pending_weights[kept] = c.pending_weights[k]
      }
      kept = kept + 1
    }
    k = k + 1
  }
  // Truncate the queue to only the un-delivered events.
  let mut drop : Int = n - kept
  while drop > 0 {
    let _ = c.pending_times.pop()
    let _ = c.pending_posts.pop()
    let _ = c.pending_weights.pop()
    drop = drop - 1
  }
}

///|
/// Helper: apply a single weight to the post-synaptic receptor.
fn apply_weight(c : SpikingSynapse, post_idx : Int, w : Float) -> Unit {
  if c.sym == "ge" {
    c.post.glu[post_idx] = c.post.glu[post_idx] + w
  } else {
    c.post.gaba[post_idx] = c.post.gaba[post_idx] + w
  }
}