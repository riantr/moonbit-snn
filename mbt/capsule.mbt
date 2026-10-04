// capsule.mbt -- Capsule primitives (v0.133.0).
//
// A capsule (Sabour et al. 2017 "Dynamic Routing Between Capsules")
// is a group of neurons that outputs a vector, not a scalar. The
// vector encodes a "pose" (for an image) of an entity. The norm of
// the output vector is the existence probability p, and the direction
// carries the pose.
//
// Capsules must be *squashed* so that p is in [0, 1):
//
//   v_hat = v / ||v||_2
//   p    = 1 / (1 + exp(-||v||_2))
//   s(v) = (p / ||v||_2) * v_hat
//
// so s(v) = v / (1 + ||v||^2), which is the numerically convenient
// form used here.
//
// Scope of v0.133.0:
//   - Capsule struct: a vector-valued neuron group.
//   - capsule_squash: the squash activation.
//   - capsule_norm: L2 norm of a capsule vector.
//   - capsule_dot: dot product of two capsule vectors.
//   - capsule_num_params: counting helper.
//
// Reference: Sabour et al. 2017; Hinton et al. 2018 "The Matrix Capsule
// Networks".

///|
/// Capsule: a vector-valued output with a squashing activation.
/// `dim` is the length of the pose vector.
pub struct Capsule {
  dim : Int
  // Squashed output vector of length `dim`.
  v : Array[Float]
  // Existence probability (the norm of the squashed output).
  prob : Float
}

///|
/// The squash activation: s(v) = v / (1 + ||v||^2).
///
/// This is the closed-form simplification of the two-step
/// (normalise -> scale by p) definition, and it is the version used
/// throughout the capsule literature because it avoids two
/// intermediate arrays and a division.
pub fn capsule_squash(v : Array[Float]) -> Capsule {
  let n = v.length()
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + v[i] * v[i]
  }
  let sq = sum
  let denom = 1.0F + sq
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = v[i] / denom
  }
  // The norm of the squashed output is the existence probability.
  let mut nsum = 0.0F
  for i in 0..<n {
    nsum = nsum + out[i] * out[i]
  }
  { dim: n, v: out, prob: sqrtf(nsum) }
}

///|
/// L2 norm of a capsule vector.
pub fn capsule_norm(v : Array[Float]) -> Float {
  let n = v.length()
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + v[i] * v[i]
  }
  sqrtf(sum)
}

///|
/// Dot product of two capsule vectors of equal length.
pub fn capsule_dot(a : Array[Float], b : Array[Float]) -> Float {
  let n = a.length()
  let mut sum = 0.0F
  for i in 0..<n {
    sum = sum + a[i] * b[i]
  }
  sum
}

///|
/// Scale a capsule vector by a scalar.
pub fn capsule_scale(v : Array[Float], s : Float) -> Array[Float] {
  let n = v.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = v[i] * s
  }
  out
}

///|
/// Add a scalar bias to every component of a capsule vector. Capsules
/// use a bias added to the pose before squashing, which is how they
/// express "there is no entity here" (bias pushes the vector toward
/// zero, giving a small existence probability).
pub fn capsule_add_scalar(v : Array[Float], b : Float) -> Array[Float] {
  let n = v.length()
  let out : Array[Float] = Array::make(n, 0.0F)
  for i in 0..<n {
    out[i] = v[i] + b
  }
  out
}

///|
/// Margin loss (Hinton et al. 2018): the capsule-based replacement for
/// the softmax cross-entropy used in a conventional classifier.
///
///   L_k = T_k * max(0, m+ - ||v_k||)^2
///       + 0.9 * (1 - T_k) * max(0, ||v_k|| - m-)^2
///
/// `caps` is the flat row-major [num_capsules x dim] stack of squashed
/// capsule outputs, `target` is the index of the present class,
/// `m_pos` = 0.9 and `m_neg` = 0.1 are the margins, and
/// `absent_weight` = 0.9 down-weights absent classes so the network
/// does not simply squashed everything to zero.
pub fn capsule_margin_loss(
  caps : Array[Float],
  num_capsules : Int,
  dim : Int,
  target : Int,
  m_pos : Float,
  m_neg : Float,
  absent_weight : Float,
) -> Float {
  let mut total = 0.0F
  for k in 0..<num_capsules {
    // Length of the k-th capsule vector.
    let base = k * dim
    let mut sq = 0.0F
    for i in 0..<dim {
      let v = caps[base + i]
      sq = sq + v * v
    }
    let len = sqrtf(sq)
    if k == target {
      // Present class: push the length above m_pos.
      let diff = m_pos - len
      if diff > 0.0F {
        total = total + diff * diff
      }
    } else {
      // Absent class: push the length below m_neg.
      let diff = len - m_neg
      if diff > 0.0F {
        total = total + absent_weight * diff * diff
      }
    }
  }
  total
}