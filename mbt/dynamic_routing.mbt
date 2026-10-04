// dynamic_routing.mbt -- Dynamic routing (v0.135.0).
//
// Dynamic routing is the mechanism that replaces pooling in a capsule
// network (Sabour et al. 2017). Instead of averaging features from
// nearby patches, the network *learns* which low-level capsule votes
// for which high-level capsule, by iteratively adjusting agreement
// coefficients `c_ij`:
//
//   1. initialise routing logits b_ij = 0
//   2. repeat r times:
//      a. w_ij = c_ij * u_ij              (weighted votes)
//      b. s_j  = squash( sum_i w_ij )     (high-level capsule output)
//      c. d_ij = dot(s_j, u_ij)           (agreement)
//      d. c_ij = softmax_i(b_ij + d_ij)   (routing coefficients)
//      e. b_ij = b_ij + d_ij               (logit update, in practice)
//
// The agreement `dot(s_j, u_ij)` is what makes the routing
// *dynamic*: a low-level capsule that predicts a pose matching the
// current high-level prediction gets its coefficient increased, so on
// the next iteration it votes more strongly. Capsules for absent
// entities converge to p ~ 0 because their votes disagree.
//
// Scope of v0.135.0:
//   - dynamic_routing: one full routing loop over low-level capsules.
//   - routing_coefficients: the final c_ij for a given input (useful
//     for visualisation / interpretability).
//
// Reference: Sabour et al. 2017, Algorithm 1.

///|
/// Dynamic routing from low-level capsules to a bank of high-level
/// capsules.
///
/// `low_caps` is the flat [n_low x dim] stack of squashed
/// low-level capsule outputs. `w` is the [n_high x n_low * dim]
/// transformation matrix (the "prediction weights" that turn a vote
/// into a predicted pose for a particular high-level capsule).
///
/// Returns the flat [n_high x dim] stack of high-level capsule
/// outputs.
pub fn dynamic_routing(
  low_caps : Array[Float],
  n_low : Int,
  w : Array[Array[Float]],
  n_high : Int,
  dim : Int,
  n_iter : Int,
) -> Array[Float] {
  // Routing logits b_ij, flat [n_high x n_low].
  let logits : Array[Float] = Array::make(n_high * n_low, 0.0F)
  // Predicted votes u_ij, flat [n_high x n_low * dim].
  let u : Array[Float] = Array::make(n_high * n_low * dim, 0.0F)
  // High-level outputs s_j, flat [n_high x dim].
  let s : Array[Float] = Array::make(n_high * dim, 0.0F)
  let votes : Array[Float] = Array::make(dim, 0.0F)
  let c : Array[Float] = Array::make(n_low, 0.0F)
  for _ in 0..<n_iter {
    // Step b/c: softmax over the logits for each high-level capsule j.
    for j in 0..<n_high {
      let lbase = j * n_low
      for i in 0..<n_low {
        logits[lbase + i] = logits[lbase + i]
      }
      // Numerical-stable softmax over b_i (the dot agreements are added
      // in the next iteration, per Algorithm 1 step c/d ordering).
      let mut m = logits[lbase]
      for i in 1..<n_low {
        if logits[lbase + i] > m {
          m = logits[lbase + i]
        }
      }
      let mut sum_exp = 0.0F
      for i in 0..<n_low {
        c[i] = expf(logits[lbase + i] - m)
        sum_exp = sum_exp + c[i]
      }
      for i in 0..<n_low {
        c[i] = c[i] / sum_exp
      }
      // Step a: weighted votes -> sum_i w_ij * u_ij, then squash.
      for d in 0..<dim {
        votes[d] = 0.0F
      }
      for i in 0..<n_low {
        let cij = c[i]
        if cij < 1.0e-12F {
          continue
        }
        // u_ij = W_ji * x_i
        let wrow = w[j]
        let ubase = (lbase + i) * dim
        for d in 0..<dim {
          let mut acc = 0.0F
          for t in 0..<dim {
            acc = acc + wrow[d * dim + t] * low_caps[i * dim + t]
          }
          u[ubase + d] = acc
        }
        for d in 0..<dim {
          votes[d] = votes[d] + cij * u[ubase + d]
        }
      }
      // Step b: squash the weighted sum.
      let squashed = capsule_squash(votes)
      for d in 0..<dim {
        s[j * dim + d] = squashed.v[d]
      }
      // Step c/d: agreement dot(s_j, u_ij), update logits for next iter.
      for i in 0..<n_low {
        let ubase = (lbase + i) * dim
        let mut dot = 0.0F
        for d in 0..<dim {
          dot = dot + s[j * dim + d] * u[ubase + d]
        }
        logits[lbase + i] = logits[lbase + i] + dot
      }
    }
  }
  s
}

///|
/// Final routing coefficients for the last iteration, flattened as
/// [n_high x n_low]. Useful for interpretability: it shows which
/// low-level capsules voted for which high-level capsule.
pub fn routing_coefficients(
  low_caps : Array[Float],
  n_low : Int,
  w : Array[Array[Float]],
  n_high : Int,
  dim : Int,
  n_iter : Int,
) -> Array[Float] {
  let out = Array::make(n_high * n_low, 0.0F)
  // Re-run routing, recording the coefficients on the last iteration.
  let logits : Array[Float] = Array::make(n_high * n_low, 0.0F)
  let s : Array[Float] = Array::make(n_high * dim, 0.0F)
  let votes : Array[Float] = Array::make(dim, 0.0F)
  let c : Array[Float] = Array::make(n_low, 0.0F)
  let u : Array[Float] = Array::make(n_low * dim, 0.0F)
  for it in 0..<n_iter {
    for j in 0..<n_high {
      let lbase = j * n_low
      let mut m = logits[lbase]
      for i in 1..<n_low {
        if logits[lbase + i] > m {
          m = logits[lbase + i]
        }
      }
      let mut sum_exp = 0.0F
      for i in 0..<n_low {
        c[i] = expf(logits[lbase + i] - m)
        sum_exp = sum_exp + c[i]
      }
      for i in 0..<n_low {
        c[i] = c[i] / sum_exp
      }
      if it == n_iter - 1 {
        for i in 0..<n_low {
          out[lbase + i] = c[i]
        }
      }
      for d in 0..<dim {
        votes[d] = 0.0F
      }
      for i in 0..<n_low {
        if c[i] < 1.0e-12F {
          continue
        }
        let wrow = w[j]
        for d in 0..<dim {
          let mut acc = 0.0F
          for t in 0..<dim {
            acc = acc + wrow[d * dim + t] * low_caps[i * dim + t]
          }
          u[i * dim + d] = acc
        }
        for d in 0..<dim {
          votes[d] = votes[d] + c[i] * u[i * dim + d]
        }
      }
      let squashed = capsule_squash(votes)
      for d in 0..<dim {
        s[j * dim + d] = squashed.v[d]
      }
      for i in 0..<n_low {
        let mut dot = 0.0F
        for d in 0..<dim {
          dot = dot + s[j * dim + d] * u[i * dim + d]
        }
        logits[lbase + i] = logits[lbase + i] + dot
      }
    }
  }
  out
}