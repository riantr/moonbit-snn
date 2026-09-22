// connection_receptor.mbt — per-edge receptor routing for IF→IF
// synapses with 4-receptor dynamics (AMPA, NMDA, GABAa, GABAb).
//
// Port of SNNModels.jl/src/populations/synapse/synapses/ReceptorSynapse.jl
// (subset — the parameter struct + per-edge routing, not the full
// ODE update which is in connection_receptor_tripod.mbt for the
// multi-compartment case).
//
// Julia's `ReceptorSynapse` has:
//   - `syn : Receptors`         — 4-receptor collection (AMPA, NMDA, GABAa, GABAb)
//   - `NMDA : NMDAVoltageDependency` — voltage gating for NMDA
//   - `glu_receptors : Array[Int]` — 1-based indices in `syn` that drive `glu` (default [1, 2])
//   - `gaba_receptors : Array[Int]` — 1-based indices that drive `gaba` (default [3, 4])
//
// In MoonBit (0-based): `glu_receptors = [0, 1]` (AMPA, NMDA),
// `gaba_receptors = [2, 3]` (GABAa, GABAb).
//
// We additionally track per-edge `target_receptor : Array[Int]` parallel
// to `matrix.colptr` so each edge can target one specific receptor.
// During `forward`, the per-edge target determines whether weight goes
// to `glu[k]` (if target_receptor in glu_receptors) or `gaba[k]` (if
// in gaba_receptors).

///|
/// Per-edge receptor routing for IF→IF synapses with 4-receptor
/// dynamics. Each edge selects one receptor in `[0, 3]`; based on
/// which receptor it targets, the weight is added to the post's
/// `glu` or `gaba` buffer at delivery time.
pub(all) struct ReceptorSynapse {
  pre : IF
  post : IF
  // CSR sparse matrix: rows = pre.n, cols = post.n
  matrix : SparseMatrixCSR
  // 4-receptor collection (AMPA, NMDA, GABAa, GABAb).
  syn : Receptors
  // Per-edge receptor index (parallel to matrix.colptr).
  // target_receptor[s] ∈ [0, 3] selects which receptor to feed.
  target_receptor : Array[Int]
  // Receptor indices that drive the post's glu buffer (default [0, 1]).
  glu_receptors : Array[Int]
  // Receptor indices that drive the post's gaba buffer (default [2, 3]).
  gaba_receptors : Array[Int]
  // NMDA voltage-dependence parameters.
  nmda_dep : NMDAVoltageDependency
  // Per-edge delays (parallel to matrix.colptr). Empty = no delay.
  delays : Array[Float]
  // Pre-allocated buffers: glu[k] / gaba[k] are the input buffers
  // for post neuron k (consumed by the receptor ODE update).
  glu : Array[Float]
  gaba : Array[Float]
}

///|
/// Construct a ReceptorSynapse with random connectivity. Defaults:
/// `glu_receptors = [0, 1]` (AMPA, NMDA), `gaba_receptors = [2, 3]`
/// (GABAa, GABAb). All edges initially target receptor 0 (AMPA).
/// Use `set_target_receptor` to set per-edge targeting later.
pub fn ReceptorSynapse::new(
  pre : IF,
  post : IF,
  syn : Receptors,
  nmda_dep : NMDAVoltageDependency,
  mu : Float,
  sigma : Float,
  p : Float,
  rng : Xoshiro,
) -> ReceptorSynapse {
  let m = SparseMatrixCSR::random(pre.n, post.n, mu, sigma, p, rng)
  let n = m.vals.length()
  // Default: all edges target receptor 0 (AMPA).
  let target_receptor : Array[Int] = Array::make(n, 0)
  let glu : Array[Float] = Array::make(post.n, 0.0F)
  let gaba : Array[Float] = Array::make(post.n, 0.0F)
  {
    pre,
    post,
    matrix: m,
    syn,
    target_receptor,
    glu_receptors: [0, 1],
    gaba_receptors: [2, 3],
    nmda_dep,
    delays: [],
    glu,
    gaba,
  }
}

///|
/// Set the per-edge receptor index for edge index `s` (parallel to
/// `matrix.colptr`). `r ∈ [0, 3]`. Edge targets must be in
/// `glu_receptors` or `gaba_receptors` for forward routing to work.
pub fn ReceptorSynapse::set_target_receptor(
  s : ReceptorSynapse,
  idx : Int,
  r : Int,
) -> Unit {
  s.target_receptor[idx] = r
}

///|
/// Forward pre-synaptic spikes into the per-edge `glu` / `gaba`
/// buffers. For each pre j that fires, walk its outgoing edges; for
/// each edge (j, k) with target receptor `r`:
///   - if `r` is in `glu_receptors`, add weight to `glu[k]`
///   - if `r` is in `gaba_receptors`, add weight to `gaba[k]`
///   - else, drop the weight (no routing configured for this receptor)
pub fn forward_receptor_synapse(s : ReceptorSynapse) -> Unit {
  let rows = s.matrix.rows
  let mut i = 0
  while i < rows {
    if s.pre.fire[i] {
      let start = s.matrix.rowptr[i]
      let end = s.matrix.rowptr[i + 1]
      let mut idx = start
      while idx < end {
        let post_idx = s.matrix.colptr[idx]
        let w = s.matrix.vals[idx]
        let r = s.target_receptor[idx]
        // Route based on receptor group.
        if contains_int(s.glu_receptors, r) {
          s.glu[post_idx] = s.glu[post_idx] + w
        } else if contains_int(s.gaba_receptors, r) {
          s.gaba[post_idx] = s.gaba[post_idx] + w
        }
        // else: drop (unrouted)
        idx = idx + 1
      }
    }
    i = i + 1
  }
}

///|
/// Helper: does `arr` contain `v`? Linear scan.
fn contains_int(arr : Array[Int], v : Int) -> Bool {
  let mut i = 0
  let n = arr.length()
  let mut found = false
  while i < n {
    if arr[i] == v {
      found = true
    }
    i = i + 1
  }
  found
}