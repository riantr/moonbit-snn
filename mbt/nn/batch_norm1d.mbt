// batch_norm1d.mbt — 1D Batch Normalisation for NCW tensors.
//
// WHY THIS EXISTS, given that `batch_norm2d` already covers the maths.
//
// A BatchNorm1d over `[n, c, w]` IS a BatchNorm2d with `h = 1`: the per-channel
// mean in `batch_norm2d_forward` is taken over `(n, h, w)`, which at `h = 1` is
// `(n, w)` -- the batch axis and the length axis together. There is no separate
// formula to get wrong, so this file is a named entry point with the shape
// contract stated and checked, not a second implementation.
//
// THE BATCH AXIS IS THE POINT. `h = 1` is the only BatchNorm shape whose
// reduction spans the batch, so the per-channel statistics are a function of
// every sample in the batch rather than of the sample being normalised. That
// makes `d_input` carry two extra terms the per-sample form does not have: a
// mean term and an `xhat`-correlation term. They are 98% invisible on a
// fixture whose per-channel variance is near 1, which is exactly the fixture
// shape this project kept reaching for.
//
// WHERE IT IS USED. TARNet's attentive statistics pooling (Terraf & Iraqi,
// ICME 2026, arXiv:2605.07735) follows the asteroid ASPHead reference:
//
//     Conv1d(3C -> att_hidden, k=1) -> ReLU -> BatchNorm1d(att_hidden)
//       -> Tanh -> Conv1d(att_hidden -> C, k=1) -> Softmax(dim=2)
//
// so the BatchNorm1d runs on `[n, att_hidden, t]` and mixes all `n` frames of a
// utterance into every frame's statistics. `att_hidden` is 256 in the
// reference's default configuration.
//
// NOT YET WIRED INTO `asp_forward`. See `tarnet/tarnet_asp.mbt` for why: that
// module's `d_x` is a measured-open defect, and adding a layer whose defining
// property is cross-sample coupling on top of it would produce a red gate with
// no way to attribute the failure. This file makes the capability available and
// gated; the wiring is a separate step with its own diagnosis.
//
// Layout: NCW row-major flat `Array[Float]`.
//   Input / Output: [n, c, w]    length = n * c * w
//   Gamma / Beta:   [c]          length = c
//
// Conventions match `batch_norm2d`: population variance (no Bessel correction),
// Float32 throughout, and PyTorch's eps default of 1e-5.

///|
/// BatchNorm1d forward pass over `[n, c, w]`.
///
/// Delegates to `batch_norm2d_forward` with `h = 1` after checking the shape.
/// The check is the reason this is not a bare alias: `batch_norm2d_forward`
/// does not validate `input.length()`, so a caller that passes the wrong shape
/// gets either an out-of-bounds read or -- worse, for an over-long input --
/// silently correct-looking output over a prefix. This module has already
/// produced one stride defect of exactly that family, in the ASP context
/// buffer, so the guard is worth its three lines.
///
/// In training mode the statistics come from the batch and the backward pass
/// must apply the coupling terms; in inference mode they come from the running
/// averages and are constants. See `BatchNormCache::coupled`.
pub fn batch_norm1d_forward(
  input : Array[Float],
  n : Int,
  c : Int,
  w : Int,
  bn : BatchNorm2d,
) -> (Array[Float], BatchNormCache) {
  if n <= 0 || c <= 0 || w <= 0 {
    abort("batch_norm1d_forward: n, c and w must all be positive")
  }
  let expected = n * c * w
  if input.length() != expected {
    abort(
      "batch_norm1d_forward: input length \{input.length()} does not match [n,c,w] = [\{n},\{c},\{w}] = \{expected}",
    )
  }
  if bn.gamma.length() != c || bn.beta.length() != c {
    abort(
      "batch_norm1d_forward: gamma/beta length \{bn.gamma.length()}/\{bn.beta.length()} does not match c = \{c}",
    )
  }
  batch_norm2d_forward(input, n, c, 1, w, bn)
}

///|
/// BatchNorm1d backward pass, taking the cache from `batch_norm1d_forward`.
///
/// `d_output` is `[n, c, w]` and must be the same length as the forward input.
/// Returns `(d_input, d_gamma, d_beta)` with `d_gamma` / `d_beta` laid out
/// `[c]`, matching `batch_norm2d_backward`.
///
/// The cache carries `h = 1`, so its `nhw` is `n * w` and the sums inside the
/// backward span the batch. That is correct here and is the whole reason this
/// function exists as its own name: reading `cache.nhw` as `w` would drop the
/// batch coupling and leave a gradient that is self-consistent with nothing.
pub fn batch_norm1d_backward(
  d_output : Array[Float],
  cache : BatchNormCache,
  bn : BatchNorm2d,
) -> (Array[Float], Array[Float], Array[Float]) {
  if cache.h != 1 {
    abort("batch_norm1d_backward: cache came from a h != 1 forward (h = \{cache.h})")
  }
  let expected = cache.n * cache.c * cache.w
  if d_output.length() != expected {
    abort(
      "batch_norm1d_backward: d_output length \{d_output.length()} does not match n*c*w = \{expected}",
    )
  }
  batch_norm2d_backward(d_output, cache, bn)
}

///|
/// Number of values a `[n, c, w]` BatchNorm1d buffer holds.
pub fn batch_norm1d_size(n : Int, c : Int, w : Int) -> Int {
  n * c * w
}
