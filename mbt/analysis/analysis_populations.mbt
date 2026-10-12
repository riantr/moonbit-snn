// analysis_populations.mbt — port of
// `refs/SNNModels.jl/src/analysis/populations.jl`.
//
// Julia helpers we port:
//   - `population_indices(pops)` — assign non-overlapping 1-based index
//     ranges to a NamedTuple of populations.
//   - `filter_items(pops)` — remove populations whose name starts with
//     "noise" (Julia's default `condition`).
//   - `average_conn_strength(M, pops, μ)` — for each pair of pops (i, j),
//     compute the mean of the block `M[pops[i].range, pops[j].range]`
//     divided by μ.
//
// Container: MoonBit has no `NamedTuple`-of-pops container, so we use
// a flat `Array[(String, Int)]` ("label, neuron_count") list — the same
// shape as Julia's `(E = pop, I = pop)` would iterate via `pairs(pops)`
// to produce `(:E, pop)` / `(:I, pop)` tuples.

///|
/// A contiguous 1-based index range assigned to one population.
pub(all) struct PopIndex {
  // Population label (e.g. "E", "I").
  name : String
  // First index (1-based, inclusive).
  start : Int
  // Last index (1-based, inclusive).
  end_ : Int
}

///|
/// Number of neurons in this population (inclusive range length).
pub fn PopIndex::length(p : PopIndex) -> Int {
  p.end_ - p.start + 1
}

///|
/// Assign non-overlapping 1-based index ranges to a list of populations.
/// `pops` is `(label, neuron_count)` pairs in assignment order.
///
/// Julia source: `population_indices(pops)`. Behaviour:
///
///     pops = [(E, 10), (I, 5)]
///     → [PopIndex("E",  1, 10), PopIndex("I", 11, 15)]
///
/// The ranges are non-overlapping and the union covers 1..N_total.
/// We work with a flat `Array[(String, Int)]` rather than a NamedTuple
/// (MoonBit has no first-class NamedTuple type).
pub fn population_indices(pops : Array[(String, Int)]) -> Array[PopIndex] {
  let mut offset = 1
  let out : Array[PopIndex] = []
  for p in pops {
    let (name, n) = p
    let pi : PopIndex = { name, start: offset, end_: offset + n - 1 }
    out.push(pi)
    offset = offset + n
  }
  out
}

///|
/// Default `filter_items` predicate: drop populations whose name starts
/// with `"noise"` (matches Julia's `condition = hasproperty || "noise"...`
/// default).
fn is_noise_name(name : String) -> Bool {
  // String::starts_with exists in MoonBit core; we use prefix-match.
  name.has_prefix("noise")
}

///|
/// Filter a list of (label, neuron_count) pairs by the default rule:
/// remove any population whose label starts with `"noise"`.
pub fn filter_items(
  pops : Array[(String, Int)],
) -> Array[(String, Int)] {
  let out : Array[(String, Int)] = []
  for p in pops {
    let (name, _) = p
    if !is_noise_name(name) {
      out.push(p)
    }
  }
  out
}

///|
/// Filter a list of (label, neuron_count) pairs by a string predicate.
/// MoonBit lacks first-class function references, so we approximate the
/// Julia `condition = p -> p.N > 7` by a hardcoded set of named predicates:
///
///   - `FilterNeuronCount::Greater(7)` — keep pops with N > 7
///
/// (We use the enum pattern so tests can verify the filter behaviour
/// without needing a function reference.)
pub(all) enum FilterRule {
  /// Keep pops whose neuron count is strictly greater than `n`.
  Greater(Int)
  /// Keep pops whose neuron count is strictly less than `n`.
  Less(Int)
  /// Keep pops whose neuron count equals `n`.
  Equal(Int)
  /// Drop pops whose label starts with `"noise"` (default).
  DropNoise
}

///|
/// Apply a filter rule to a list of (label, neuron_count) pairs.
pub fn filter_items_with(
  pops : Array[(String, Int)],
  rule : FilterRule,
) -> Array[(String, Int)] {
  let out : Array[(String, Int)] = []
  for p in pops {
    let (name, n) = p
    let keep : Bool = match rule {
      Greater(th) => n > th
      Less(th) => n < th
      Equal(th) => n == th
      DropNoise => !is_noise_name(name)
    }
    if keep {
      out.push(p)
    }
  }
  out
}

///|
/// `average_conn_strength(M, pops, μ)` — for each pair (i, j), compute
/// the mean of the block `M[pops[i].range, pops[j].range]` divided by μ.
///
/// `M` is the dense weight matrix (a CSR would also work via
/// `m.get(i, j)` but the test uses dense).
/// `pops` is the array of `PopIndex` produced by `population_indices`.
/// `μ` is the per-synapse scale factor (the test divides by `μ=1.0`).
///
/// Returns an `(N_pops, N_pops)` matrix where each entry is the
/// mean block weight divided by μ. Empty blocks return `0.0F`.
pub fn average_conn_strength(
  m : Array[Array[Float]],
  pops : Array[PopIndex],
  mu : Float,
) -> Array[Array[Float]] {
  let n = pops.length()
  let out : Array[Array[Float]] = Array::make(n, [])
  let mut i = 0
  while i < n {
    let row : Array[Float] = Array::make(n, 0.0F)
    let mut j = 0
    while j < n {
      let pi = pops[i]
      let pj = pops[j]
      let mut sum = 0.0F
      let mut count = 0
      let mut ii = pi.start
      while ii <= pi.end_ {
        let mut jj = pj.start
        while jj <= pj.end_ {
          // Convert 1-based (PopIndex) to 0-based (MoonBit Array).
          let m_ii = ii - 1
          let m_jj = jj - 1
          if m_ii >= 0 && m_ii < m.length() && m_jj >= 0 && m_jj < m[0].length() {
            sum = sum + m[m_ii][m_jj]
            count = count + 1
          }
          jj = jj + 1
        }
        ii = ii + 1
      }
      let avg = if count > 0 { sum / Float::from_int(count) / mu } else { 0.0F }
      ignore(row.set(j, avg))
      j = j + 1
    }
    ignore(out.set(i, row))
    i = i + 1
  }
  out
}