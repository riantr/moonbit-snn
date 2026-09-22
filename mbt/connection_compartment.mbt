// connection_compartment — compartment-targeted SpikingSynapse for
// multi-compartment neurons (BallAndStick, Tripod).
//
// Mirrors SNNModels.jl's `TripodSomaSynapse` / `TripodDendSynapse`:
// same CSR matrix structure as `SpikingSynapse`, but routes incoming
// weights to a specific compartment (:soma or :d for BallAndStick;
// :soma, :d1, or :d2 for Tripod) instead of the IF population's
// `glu`/`gaba` buffer.
//
// Bit-exact note: this only handles the "single compartment target,
// single receptor" case. The Julia multi-receptor variants (AMPA +
// NMDA, GABA_A + GABA_B) are not yet ported; those would require
// per-synapse conductance-pair objects.

///|
/// CompartmentSynapseBall — targets one compartment (:soma or :d)
/// of a BallAndStick neuron. Same semantics as `SpikingSynapse`
/// (random sparse with Bernoulli p and weight ~ Normal/Fixed).
pub struct CompartmentSynapseBall {
  pre : IF
  post : BallAndStick
  sym : String
  target : String  // "soma" or "d"
  matrix : SparseMatrixCSR
  rho : Array[Float]
  delays : Array[Float]
  pending_times : Array[Float]
  pending_posts : Array[Int]
  pending_weights : Array[Float]
}

///|
pub fn CompartmentSynapseBall::random(
  pre : IF,
  post : BallAndStick,
  sym : String,
  target : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> CompartmentSynapseBall {
  let m = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  { pre, post, sym, target, matrix: m, rho: [], delays: [],
    pending_times: [], pending_posts: [], pending_weights: [] }
}

///|
/// CompartmentSynapseTripod — targets one compartment (:soma, :d1,
/// or :d2) of a Tripod neuron.
pub struct CompartmentSynapseTripod {
  pre : IF
  post : Tripod
  sym : String
  target : String  // "soma" | "d1" | "d2"
  matrix : SparseMatrixCSR
  rho : Array[Float]
  delays : Array[Float]
  pending_times : Array[Float]
  pending_posts : Array[Int]
  pending_weights : Array[Float]
}

///|
pub fn CompartmentSynapseTripod::random(
  pre : IF,
  post : Tripod,
  sym : String,
  target : String,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> CompartmentSynapseTripod {
  let m = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  { pre, post, sym, target, matrix: m, rho: [], delays: [],
    pending_times: [], pending_posts: [], pending_weights: [] }
}

///|
/// Add a per-connection delay (in ms) — enables scheduled delivery
/// via `deliver_pending_compartment_ball`.
pub fn CompartmentSynapseBall::set_delays(
  c : CompartmentSynapseBall,
  delays : Array[Float],
) -> Unit {
  c.delays.clear()
  for d in delays {
    c.delays.push(d)
  }
}

///|
/// Same as above, Tripod variant.
pub fn CompartmentSynapseTripod::set_delays(
  c : CompartmentSynapseTripod,
  delays : Array[Float],
) -> Unit {
  c.delays.clear()
  for d in delays {
    c.delays.push(d)
  }
}

///|
/// Apply a single weight to the post-synaptic compartment buffer.
/// Internal helper — mirrors `apply_weight` in connection_spiking.mbt.
fn apply_compartment_weight_ball(
  c : CompartmentSynapseBall,
  post_idx : Int,
  w : Float,
) -> Unit {
  let is_ge = c.sym == "ge"
  let buf : Array[Float] = match (c.target, is_ge) {
    ("soma", true) => c.post.glu_s
    ("soma", false) => c.post.gaba_s
    ("d", true) => c.post.glu_d
    ("d", false) => c.post.gaba_d
    _ => c.post.glu_s
  }
  buf[post_idx] = buf[post_idx] + w
}

///|
fn apply_compartment_weight_tripod(
  c : CompartmentSynapseTripod,
  post_idx : Int,
  w : Float,
) -> Unit {
  let is_ge = c.sym == "ge"
  let buf : Array[Float] = match (c.target, is_ge) {
    ("soma", true) => c.post.glu_s
    ("soma", false) => c.post.gaba_s
    ("d1", true) => c.post.glu_d1
    ("d1", false) => c.post.gaba_d1
    ("d2", true) => c.post.glu_d2
    ("d2", false) => c.post.gaba_d2
    _ => c.post.glu_s
  }
  buf[post_idx] = buf[post_idx] + w
}

///|
/// Forward a single time-step's pre-synaptic spikes through the
/// BallAndStick-targeted compartment synapse. For each (pre-fire,
/// outgoing-conn) pair, add `w` (× ρ if STP is active) to the
/// post-synaptic compartment's `glu` (for :ge) or `gaba` (for :gi).
///
/// If `delays` is non-empty, spikes are scheduled for delivery at
/// `t_now + delays[s]` instead of immediate application. Use
/// `deliver_pending_compartment_ball` to drain them.
pub fn forward_compartment_ball(
  c : CompartmentSynapseBall,
  t_now : Float,
) -> Unit {
  let use_delay = c.delays.length() > 0
  let use_rho = c.rho.length() > 0
  if use_delay {
    let n_pre = c.pre.fire.length()
    let mut j = 0
    while j < n_pre {
      if c.pre.fire[j] {
        let start = c.matrix.rowptr[j]
        let end = c.matrix.rowptr[j + 1]
        let mut s = start
        while s < end {
          let post_idx = c.matrix.colptr[s]
          let w = if use_rho {
            c.matrix.vals[s] * c.rho[s]
          } else {
            c.matrix.vals[s]
          }
          let d = c.delays[s]
          if d == 0.0F {
            apply_compartment_weight_ball(c, post_idx, w)
          } else {
            c.pending_times.push(t_now + d)
            c.pending_posts.push(post_idx)
            c.pending_weights.push(w)
          }
          s = s + 1
        }
      }
      j = j + 1
    }
  } else {
    let buf = if c.sym == "ge" {
      if c.target == "soma" {
        c.post.glu_s
      } else {
        c.post.glu_d
      }
    } else {
      if c.target == "soma" {
        c.post.gaba_s
      } else {
        c.post.gaba_d
      }
    }
    if use_rho {
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
            buf[post_idx] = buf[post_idx] + w_scaled
            s = s + 1
          }
        }
        j = j + 1
      }
    } else {
      c.matrix.forward(c.pre.fire, buf)
    }
  }
}

///|
/// Tripod variant of `forward_compartment_ball`.
pub fn forward_compartment_tripod(
  c : CompartmentSynapseTripod,
  t_now : Float,
) -> Unit {
  let use_delay = c.delays.length() > 0
  let use_rho = c.rho.length() > 0
  if use_delay {
    let n_pre = c.pre.fire.length()
    let mut j = 0
    while j < n_pre {
      if c.pre.fire[j] {
        let start = c.matrix.rowptr[j]
        let end = c.matrix.rowptr[j + 1]
        let mut s = start
        while s < end {
          let post_idx = c.matrix.colptr[s]
          let w = if use_rho {
            c.matrix.vals[s] * c.rho[s]
          } else {
            c.matrix.vals[s]
          }
          let d = c.delays[s]
          if d == 0.0F {
            apply_compartment_weight_tripod(c, post_idx, w)
          } else {
            c.pending_times.push(t_now + d)
            c.pending_posts.push(post_idx)
            c.pending_weights.push(w)
          }
          s = s + 1
        }
      }
      j = j + 1
    }
  } else {
    let buf = if c.sym == "ge" {
      match c.target {
        "soma" => c.post.glu_s
        "d1" => c.post.glu_d1
        "d2" => c.post.glu_d2
        _ => c.post.glu_s
      }
    } else {
      match c.target {
        "soma" => c.post.gaba_s
        "d1" => c.post.gaba_d1
        "d2" => c.post.gaba_d2
        _ => c.post.gaba_s
      }
    }
    if use_rho {
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
            buf[post_idx] = buf[post_idx] + w_scaled
            s = s + 1
          }
        }
        j = j + 1
      }
    } else {
      c.matrix.forward(c.pre.fire, buf)
    }
  }
}

///|
/// Drain pending events for BallAndStick synapses whose delivery
/// time has arrived.
pub fn deliver_pending_compartment_ball(
  c : CompartmentSynapseBall,
  t_now : Float,
) -> Unit {
  let n = c.pending_times.length()
  if n == 0 {
    return
  }
  let mut kept : Int = 0
  let mut k : Int = 0
  while k < n {
    if c.pending_times[k] <= t_now {
      apply_compartment_weight_ball(
        c,
        c.pending_posts[k],
        c.pending_weights[k],
      )
    } else {
      if kept != k {
        c.pending_times[kept] = c.pending_times[k]
        c.pending_posts[kept] = c.pending_posts[k]
        c.pending_weights[kept] = c.pending_weights[k]
      }
      kept = kept + 1
    }
    k = k + 1
  }
  while c.pending_times.length() > kept {
    let _ = c.pending_times.pop()
    let _ = c.pending_posts.pop()
    let _ = c.pending_weights.pop()
  }
}

///|
/// Tripod variant.
pub fn deliver_pending_compartment_tripod(
  c : CompartmentSynapseTripod,
  t_now : Float,
) -> Unit {
  let n = c.pending_times.length()
  if n == 0 {
    return
  }
  let mut kept : Int = 0
  let mut k : Int = 0
  while k < n {
    if c.pending_times[k] <= t_now {
      apply_compartment_weight_tripod(
        c,
        c.pending_posts[k],
        c.pending_weights[k],
      )
    } else {
      if kept != k {
        c.pending_times[kept] = c.pending_times[k]
        c.pending_posts[kept] = c.pending_posts[k]
        c.pending_weights[kept] = c.pending_weights[k]
      }
      kept = kept + 1
    }
    k = k + 1
  }
  while c.pending_times.length() > kept {
    let _ = c.pending_times.pop()
    let _ = c.pending_posts.pop()
    let _ = c.pending_weights.pop()
  }
}