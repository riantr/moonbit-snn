// realnvp.mbt — Real NVP normalizing flow (v0.102.0): stack of
// AffineCouplingLayers with fixed permutation between layers.
//
// Real NVP (Dinh et al. 2016) chains multiple affine coupling layers,
// with a fixed permutation of the input dimensions between layers so
// that different "halves" of the input get transformed at each
// layer. The composition of bijections is a bijection, and the total
// log |det J| is the sum of the per-layer log |det| terms.
//
// Reference: Dinh et al. 2016 "Density Estimation with Real NVP".

///|
/// Real NVP flow: N stacked affine coupling layers with a fixed
/// permutation between layers.
pub struct RealNVP {
  n_layers : Int
  layers : Array[AffineCouplingLayer]
  // Permutation applied between layers. Default: reverse the input
  // dimensions so that x₁ becomes x₂ in the next layer.
  perm : Array[Int]
  // Input dim (must equal 2 × half_dim for all layers).
  dim : Int
}

///|
/// Build a fresh Real NVP flow with `n_layers` coupling layers + a
/// default reverse permutation between layers. Each layer has its
/// own seed (offset).
pub fn RealNVP::new(
  dim : Int,
  n_layers : Int,
  seed : UInt64,
) -> RealNVP {
  let half = dim / 2
  let layers : Array[AffineCouplingLayer] = Array::make(
    n_layers, AffineCouplingLayer::new(half, seed),
  )
  for i in 0..<n_layers {
    layers[i] = AffineCouplingLayer::new(
      half, seed + (i + 1).to_uint64() * 100UL,
    )
  }
  // Default permutation: reverse the input dim.
  let perm : Array[Int] = Array::make(dim, 0)
  for i in 0..<dim {
    perm[i] = dim - 1 - i
  }
  { n_layers, layers, perm, dim }
}

///|
/// Apply a permutation to a vector (in-place copy).
fn apply_perm(x : Array[Float], perm : Array[Int]) -> Array[Float] {
  let out : Array[Float] = Array::make(x.length(), 0.0F)
  for i in 0..<x.length() {
    out[i] = x[perm[i]]
  }
  out
}

///|
/// Forward pass through the flow: chain coupling_forward + perm
/// across all layers. Returns `(y, total_log_det)`.
pub fn realnvp_forward(
  flow : RealNVP,
  x : Array[Float],
) -> (Array[Float], Float) {
  let mut cur = x.copy()
  let mut total_log_det = 0.0F
  for i in 0..<flow.n_layers {
    let (y, log_det) = coupling_forward(flow.layers[i], cur)
    cur = y
    total_log_det = total_log_det + log_det
    // Permute before next layer (except after last layer).
    if i + 1 < flow.n_layers {
      cur = apply_perm(cur, flow.perm)
    }
  }
  (cur, total_log_det)
}

///|
/// Inverse pass through the flow: chain perm (reversed) +
/// coupling_inverse across all layers. Returns `x`.
pub fn realnvp_inverse(
  flow : RealNVP,
  y : Array[Float],
) -> Array[Float] {
  let mut cur = y.copy()
  // Walk layers in reverse.
  let mut i = flow.n_layers - 1
  while i >= 0 {
    // Reverse-permute first (since coupling_inverse expects the
    // permuted layout).
    if i >= 0 {
      // Apply the inverse permutation: if perm maps i → perm[i], then
      // the inverse permutation maps perm[i] → i. Build it once.
      let inv_perm : Array[Int] = Array::make(flow.dim, 0)
      for j in 0..<flow.dim {
        inv_perm[flow.perm[j]] = j
      }
      cur = apply_perm(cur, inv_perm)
    }
    cur = coupling_inverse(flow.layers[i], cur)
    i = i - 1
  }
  cur
}

///|
/// Compute the total log |det J| of the forward flow at `x`.
pub fn realnvp_log_det(
  flow : RealNVP,
  x : Array[Float],
) -> Float {
  let mut cur = x.copy()
  let mut total_log_det = 0.0F
  for i in 0..<flow.n_layers {
    let log_det = coupling_log_det(flow.layers[i], cur)
    total_log_det = total_log_det + log_det
    if i + 1 < flow.n_layers {
      cur = apply_perm(cur, flow.perm)
    }
  }
  total_log_det
}