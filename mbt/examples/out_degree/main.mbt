// out_degree.mbt — port of out_degree.jl (simplified).
//
// Original Julia:
//   for rule in [:Fixed, :Bernoulli, :PowerLaw]:
//     E = IF(N=4000, ...)
//     E_to_E = SpikingSynapse(E, E, :ge; conn=(p=0.05, μ=2nS, rule=rule))
//     # plot out-degree histogram
//
// MoonBit (v0.5.0): small version (40 IF neurons, p=0.1).
// Iterates FixedIn / Bernoulli / FixedOut and prints the mean and
// std of the per-row out-degree, plus the min/max.

///|
fn summarize(label : String, m : @snn_mbt.SparseMatrixCSR) -> Unit {
  let rows = m.rows
  let mut total = 0
  let mut mn = 1000000
  let mut mx = -1
  let mut nnz_actual = 0
  for i in 0..<rows {
    let n = m.row_nnz(i)
    nnz_actual = nnz_actual + n
    if n < mn { mn = n }
    if n > mx { mx = n }
    total = total + n
  }
  let mean = Float::from_int(total) / Float::from_int(rows)
  // std: sum (n - mean)^2
  let mut ss : Float = 0.0F
  for i in 0..<rows {
    let n = Float::from_int(m.row_nnz(i))
    let d = n - mean
    ss = ss + d * d
  }
  let variance = ss / Float::from_int(rows)
  let std = variance.sqrt()
  println(
    "  " + label + ": nnz=" + nnz_actual.to_string() + " mean_out=" +
    mean.to_string() + " std=" + std.to_string() + " min=" + mn.to_string() +
    " max=" + mx.to_string(),
  )
}

///|
fn main {
  let rng = @snn_mbt.Xoshiro::default()
  let n_pre = 40
  let n_post = 40
  let p = 0.1F
  let mu = 2.0F
  let sigma = 0.0F

  println("out_degree.mbt: comparing connect rules")
  println("  pre=" + n_pre.to_string() + " post=" + n_post.to_string() + " p=" + p.to_string())

  // FixedIn: same in-degree for every post.
  let m_fixedin = @snn_mbt.SparseMatrixCSR::random_with_rule(
    n_pre, n_post, mu, sigma, p, @snn_mbt.FixedIn, rng,
  )
  summarize("FixedIn ", m_fixedin)

  // Bernoulli: per-edge independent.
  let m_bernoulli = @snn_mbt.SparseMatrixCSR::random_with_rule(
    n_pre, n_post, mu, sigma, p, @snn_mbt.Bernoulli, rng,
  )
  summarize("Bernoulli", m_bernoulli)

  // FixedOut: same out-degree for every pre.
  let m_fixedout = @snn_mbt.SparseMatrixCSR::random_with_rule(
    n_pre, n_post, mu, sigma, p, @snn_mbt.FixedOut, rng,
  )
  summarize("FixedOut", m_fixedout)

  println("out_degree.mbt: done")
}