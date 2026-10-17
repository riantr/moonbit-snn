// tarnet_asp.mbt -- Attentive Statistics Pooling, the temporal aggregator that
// turns frame-level features into the utterance-level speaker embedding.
//
// Reference: arXiv:2605.07735 (ICME 2026); authors' implementation
// `tarnet/models/tarnet.py`, class `ASPHead`; originally Okabe et al.,
// Interspeech 2018.
//
//   mean_t = mean over t
//   std_t  = sqrt(var over t, population) + 1e-4 under the root
//   ctx    = [x, mean_t broadcast, std_t broadcast]      -> [n, 3C, T]
//   alpha  = softmax over t of  Conv1d(att,C,k=1)(tanh(BN(ReLU(Conv1d(3C,att,k=1)(ctx)))))
//   mu     = sum_t x * alpha
//   second = sum_t x^2 * alpha
//   var    = max(second - mu^2, 1e-4)
//   out    = [mu, sqrt(var)]                            -> [n, 2C]
//
// TWO CLAMPS, and both have to be reflected in the derivative or the gate
// silently stops learning: `sqrt(var + 1e-4)` in the CONTEXT (a variance
// floor, and its derivative is NOT zero even when the variance is large) and
// `clamp_min(second - mu^2, 1e-4)` on the OUTPUT variance (a dead zone --
// inside it the gradient really is zero, unlike the context one).
//
// KNOWN DEVIATION, and the original justification for it was WRONG.
//
// The reference's attention net is Conv1d -> ReLU -> BatchNorm1d -> Tanh ->
// Conv1d -> Softmax(dim=2). This port omits the BatchNorm1d. The comment that
// used to sit here said it was omitted "deliberately: BatchNorm's gradient
// depends on the batch through its running statistics, which makes a
// per-sample gradient check meaningless." That is not true. In training mode
// BatchNorm uses BATCH statistics, which are a function of the input itself and
// are differentiated exactly by `batch_norm2d_backward` -- the same function
// that already carries this model's bn_seg and bn_out, and which is gated. A
// stated limitation whose reason is false is worse than no comment, because the
// next reader assumes the question was settled.
//
// Implementing it is NOT blocked by the BatchNorm being hard to differentiate.
// It was attempted, and the attempt is reproducible. Four things are now
// established about the failure:
//
//  1. IT IS A REAL BUG, not precision. At the worst coordinate (index 59) the
//     relative error holds at about -17% while the finite-difference step drops
//     32-fold, from 0.012 to 0.000375:
//         h=0.012    rel=-0.16803
//         h=0.006    rel=-0.16756
//         h=0.003    rel=-0.16756
//         h=0.0015   rel=-0.16568
//         h=0.00075  rel=-0.16568
//     Truncation error scales as h^2 and would have collapsed by ~1000x, so no
//     tolerance derived from float32 precision can explain this. This is the
//     opposite of block coordinate 145, which WAS a float32 artefact.
//
//  2. The gradient carries TOO MUCH, not too little: fd=0.0704 against
//     an=0.0846. So a contribution is being added, not dropped. A missing path
//     would show the opposite sign.
//
//  3. `n = 1` is exactly clean while `n = 2` and `n = 3` are not, so the
//     discrepancy lives in the cross-sample coupling -- which is the BatchNorm's
//     whole purpose here.
//
//  4. The BatchNorm is exonerated: `batch_norm2d_backward`'s d_input is correct
//     at this exact shape (c=6, h=1, w=7), with `with_gamma_beta` as well as
//     `new`, and its forward does not mutate its input. And `d_relu` is consumed
//     exactly once (the ReLU mask below), so double-counting through a second
//     consumer is excluded.
//
// So the fault is in this module's own attention backward. And it is now
// pinned to ONE link, by decomposing d_x into its two sources.
//
// d_x gets contributions from exactly two places:
//
//   (a) DIRECT, from mu and second:
//       d_x[k] += gm*alpha_k + gs*2*x_k*alpha_k
//       gm = g_mu - 2*mu*gs/(2*sigma),  gs = g_sigma*0.5/sigma
//       (alpha is treated as constant here on purpose -- its x-dependence is
//        what the ctx path carries.)
//   (b) CTX, through alpha -> pre -> conv1 -> ctx -> x. Tiny in magnitude but
//       this is where alpha's dependence on x lives.
//
// Everything (a) needs is in the cache (alpha, mu, var_out, x) so it can be
// rebuilt outside the library and compared against the converged FD. At the
// worst coordinate, i = 59 (batch 1, channel 3, time 3):
//
//     fd     = 0.06866455078125     <- converged, stable across a 32x step cut
//     total  = 0.08458627015352249  <- what asp_backward returns
//     direct = 0.08287138491868973  <- (a) alone
//     ctx    = 0.0017148852348327637 <- (b) alone
//
//     |fd - total| = 0.0159   |fd - direct| = 0.0142   |fd - ctx| = 0.0669
//
// TWO CONCLUSIONS. d_x here is ~98% DIRECT, and the direct path is right to
// within float32 -- so `d_mu` / `d_var` / `alpha` / the variance floor are all
// exonerated. The whole discrepancy sits in the ctx path: its true
// cross-sample contribution is about -0.0159 while this code computes +0.0017,
// i.e. it is wrong by roughly ten times its own magnitude.
//
// Within that path the only operation that mixes samples is the BatchNorm, so
// the suspect is the thread
//     d_alpha -> softmax -> conv2 -> tanh -> BN backward -> ReLU mask -> conv1
//     -> ctx split -> d_x
// and specifically what `batch_norm2d_backward` returns as d_input when that
// gradient itself already carries cross-sample content. The change is not
// committed: an unexplained red gate is not progress.
//
// ---------------------------------------------------------------------------
// SOLVED, AND NOT WHERE ANY OF THE ABOVE LOOKED. 2026-10-16.
//
// Every measurement above is sound and every conclusion drawn from it was
// wrong, because all of them were taken against a FINITE DIFFERENCE of this
// module's own forward. A difference of a forward is blind to a forward that is
// wrong: it faithfully reports the gradient of whatever the forward actually
// computes. The gate in `tests/wave2/tarnet_test.mbt` that replaced the removed
// one runs at rtol 3e-2 / atol 2e-3 with h = 1.2e-2, and is green -- but a
// float32 central difference of an objective summed over ~170 terms was
// measured carrying an absolute noise floor of order 0.1 at a 1e-2 step
// elsewhere in this project, so that gate was below its own instrument's noise.
//
// `tests/wave2/asp_oracle_test.mbt` compares against a PyTorch autograd
// reference, which has no step size and cannot agree with a wrong
// implementation by construction. It found this on the first run.
//
// THE BUG WAS IN THE FORWARD, NOT THE BACKWARD, AND NOT IN THE BATCHNORM.
//
// `asp_forward` wrote the context's x-slot at `base + ti`, i.e. at X's stride
// of `c`, while the mean and sd slots correctly used ctx's stride of `3c`.
// Batch b's x-slot therefore lands on `[b*c*t, b*c*t + c*t)`, and batch b-1's
// mean-slot lands on `[b*3c*t + c*t, ... + c*t)`. Those are the same range.
// At n=3, c=5, t=7:
//
//     [0,  35)  batch 0 x          correct
//     [35, 70)  batch 1 x          CLOBBERS batch 0's mean slot
//     [70,105)  batch 2 x          CLOBBERS batch 0's sd slot
//
// So for batch 0 the attention was reading OTHER SAMPLES' activations as its
// own context. Its "standard deviation" slot held raw waveform values,
// including negatives -- which no square root can produce, and which is exactly
// what the dump showed before the cause was understood.
//
// The write was self-consistent (`ctx[i0] = x[i0]` reads and writes one index),
// which is why nothing downstream complained: this is the third stride defect
// in this module and the same failure mode as the two already recorded above.
// `asp_backward` was meanwhile reading the x-slot at the correct stride
// `(b*3*c + ch)*t + ti`, so the forward and the backward disagreed about the
// buffer's layout. That is what "the chain written in `asp_backward` is not the
// chain `asp_forward` computes" meant -- literally, and it was one index.
//
// WHAT THE INVESTIGATION GOT WRONG, since the wrong turns are the useful part.
// It exonerated the BatchNorm correctly, and it was right that d_x is ~98%
// DIRECT. It then concluded the ctx path was a small term that could not carry
// the discrepancy -- and then spent its remaining effort looking for a large
// discrepancy INSIDE that small term. The ctx path was small; it did not need to
// explain anything. Meanwhile `alpha` itself was being computed from a corrupt
// context, which perturbs the DIRECT path too, and no amount of decomposing the
// backward would have shown that, because the decomposition assumed a correct
// forward and compared against a difference of the same incorrect one.
//
// THE LESSON, in the form it should have taken at the time: when a gradient
// disagrees with a finite difference and the algebra checks out, the next
// question is not "which term of the backward is wrong" but "is the reference
// trustworthy". A second, independent implementation of the forward settles it
// in one run; re-deriving the backward by hand settles nothing.

///|
/// Parameters of the ASP attention network. Both convolutions are 1x1, so
/// they are the `LinearParam` pair the time axis as batch axis gives.
///
/// `att_bn_gamma` / `att_bn_beta` are the per-channel affine of the
/// BatchNorm1d the reference puts between ReLU and tanh, one entry per
/// attention unit. They are stored here rather than inside a `BatchNorm2d` so
/// that the network's parameter flatten/unflatten keeps them inside the Adam
/// state -- a normalisation whose affine is not in the optimiser's list would
/// never be trained, and would look fine in every forward check.
pub(all) struct AspParam {
  w1 : Array[Float]
  b1 : Array[Float]
  att_bn_gamma : Array[Float]
  att_bn_beta : Array[Float]
  w2 : Array[Float]
  b2 : Array[Float]
  channels : Int
  att_hidden : Int
}

///|
pub fn AspParam::new(channels : Int, att_hidden : Int, seed : UInt64) -> AspParam {
  let rng = @core.Xoshiro::new(seed)
  {
    w1: tn_randn(att_hidden * 3 * channels, 0.05F, rng),
    b1: Array::make(att_hidden, 0.0F),
    // gamma = 1, beta = 0, the PyTorch default for a fresh BatchNorm1d.
    att_bn_gamma: Array::make(att_hidden, 1.0F),
    att_bn_beta: Array::make(att_hidden, 0.0F),
    w2: tn_randn(channels * att_hidden, 0.05F, rng),
    b2: Array::make(channels, 0.0F),
    channels,
    att_hidden,
  }
}

///|
/// Everything the backward pass needs.
pub(all) struct AspCache {
  x : Array[Float]
  ctx : Array[Float]
  pre : Array[Float]
  hidden : Array[Float]
  alpha : Array[Float]
  mu : Array[Float]
  var_out : Array[Float]
  // The BatchNorm1d state. `bn` carries gamma/beta and the training flag;
  // `bn_cache` carries the statistics the forward actually used, which is what
  // makes `d_input` cross-sample coupled in training mode and not in inference.
  att_bn : @nn.BatchNorm2d
  att_bn_cache : @nn.BatchNormCache
}

///|
/// The variance floor the reference adds under the root of the context
/// standard deviation. Distinct from the output-side `clamp_min` below.
pub const ASP_STD_FLOOR : Float = 1.0e-4F

///|
/// The output variance's dead zone. Inside it the derivative is exactly zero.
pub const ASP_VAR_FLOOR : Float = 1.0e-4F

///|
/// ASP forward. `x` is `[n, c, t]`, returns `[n, 2c]`.
pub fn asp_forward(
  x : Array[Float],
  n : Int,
  p : AspParam,
) -> (Array[Float], AspCache) {
  let c = p.channels
  let t = x.length() / (n * c)
  let inv_t = 1.0F / Float::from_int(t)
  let ctx : Array[Float] = Array::make(n * 3 * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut mean = 0.0F
      let mut sq = 0.0F
      for ti in 0..<t {
        let v = x[base + ti]
        mean = mean + v
        sq = sq + v * v
      }
      mean = mean * inv_t
      // `var` is a MoonBit keyword, hence `variance` rather than `var`
      let variance = sq * inv_t - mean * mean
      let sd = @core.sqrtf(variance + ASP_STD_FLOOR)
      for ti in 0..<t {
        // `i0` must use ctx's OWN stride of `3c`, not x's stride of `c`.
        //
        // BUG, found by the PyTorch autograd oracle in
        // `tests/wave2/asp_oracle_test.mbt`. With `i0 = base + ti`, i.e.
        // x's stride, batch b's x-slot occupies `[b*c*t, b*c*t + c*t)`.
        // Batch b-1's mean-slot occupies `[b*3c*t + c*t, ... + c*t)`, and
        // those are THE SAME RANGE: for n=3, c=5, t=7 batch 1's x overwrote
        // batch 0's mean slot at [35,70) and batch 2's x overwrote batch 0's
        // sd slot at [70,105).
        //
        // It was invisible from inside the forward for the same reason the
        // two layout bugs already recorded in this file were: the write is
        // self-consistent (`ctx[i0] = x[i0]` reads and writes the same index),
        // and nothing afterwards looks at the clobbered slots. What exposed it
        // is that batch 0's attention was reading OTHER SAMPLES' activations
        // as its own context -- its "standard deviation" slot held raw
        // waveform values, including negatives, which no square root can
        // produce. `asp_backward` was meanwhile reading the x-slot at the
        // correct stride (`(b*3*c + ch)*t + ti`), so the two halves of this
        // module disagreed about the buffer's layout. That is the sense in
        // which "the chain written in asp_backward is not the chain
        // asp_forward computes" -- it was literal, and it was one index.
        let i0 = (b * 3 * c + ch) * t + ti
        let i1 = (b * 3 * c + c + ch) * t + ti
        let i2 = (b * 3 * c + 2 * c + ch) * t + ti
        ctx[i0] = x[base + ti]
        ctx[i1] = mean
        ctx[i2] = sd
      }
    }
  }
  // Conv1d(3C -> att, k=1) -> ReLU -> BatchNorm1d -> Tanh, then
  // Conv1d(att -> C, k=1) -> Softmax(dim=time).
  //
  // The BatchNorm is the reference's `nn.BatchNorm1d(att_hidden)` on `[n,
  // att_hidden, t]`. It is the one normalisation in this network whose reduction
  // spans the BATCH, so every attention unit's statistics are a function of
  // every sample's activations -- which is the whole reason it has to be there,
  // and the reason its backward cannot be checked one sample at a time.
  //
  // Three buffers, not one. The ReLU mask lives in `pre` and cannot be
  // recovered from tanh(z) afterwards, and the BatchNorm needs its own cache;
  // keeping them separate is what lets the backward walk back through the same
  // three steps the forward walked through.
  //
  // These two convolutions are written out instead of calling
  // `linear_forward`, because ASP's arrays are `[n, c, t]` (channels
  // contiguous within a frame) while `linear_forward` wants `[n_batch,
  // features]`. With `n_batch = n*t` those are DIFFERENT orderings, and the
  // mismatch is invisible in the forward -- it is consistent with itself -- and
  // only shows up when the backward reads the gradient with the other
  // assumption. Expressing the contraction in the layout the data actually
  // uses removes the ambiguity.
  let pre : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let relu_out : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let k3 = 3 * c
  let att = p.att_hidden
  for b in 0..<n {
    for j in 0..<att {
      let bj = p.b1[j]
      for ti in 0..<t {
        let mut acc = bj
        let mut s = 0
        while s < k3 {
          acc = acc + p.w1[j * k3 + s] * ctx[(b * k3 + s) * t + ti]
          s = s + 1
        }
        pre[(b * att + j) * t + ti] = acc
      }
    }
  }
  let mut i = 0
  while i < n * att * t {
    let v = pre[i]
    relu_out[i] = if v > 0.0F { v } else { 0.0F }
    i = i + 1
  }
  // `momentum = 0` so the forward has no side effect on the running statistics
  // and stays a pure function of its arguments -- the same discipline the
  // module's other layers keep, and the reason `asp_forward` can be called from
  // a finite-difference sweep at all.
  let att_bn = @nn.BatchNorm2d::with_gamma_beta(
    p.att_bn_gamma.copy(),
    p.att_bn_beta.copy(),
    momentum = 0.0,
    eps = 1.0e-5F,
  )
  let (bn_out, bn_cache) = @nn.batch_norm1d_forward(relu_out, n, att, t, att_bn)
  let hidden : Array[Float] = Array::make(n * att * t, 0.0F)
  i = 0
  while i < n * att * t {
    hidden[i] = @core.tanhf(bn_out[i])
    i = i + 1
  }
  let logits : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let bch = p.b2[ch]
      for ti in 0..<t {
        let mut acc = bch
        let mut j = 0
        while j < att {
          acc = acc + p.w2[ch * att + j] * hidden[(b * att + j) * t + ti]
          j = j + 1
        }
        logits[(b * c + ch) * t + ti] = acc
      }
    }
  }
  // softmax over the time axis, independently per (batch, channel)
  let alpha : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut mx = logits[base]
      let mut ti = 1
      while ti < t {
        let v = logits[base + ti]
        if v > mx {
          mx = v
        }
        ti = ti + 1
      }
      let mut s = 0.0F
      ti = 0
      while ti < t {
        s = s + @core.expf(logits[base + ti] - mx)
        ti = ti + 1
      }
      ti = 0
      while ti < t {
        alpha[base + ti] = @core.expf(logits[base + ti] - mx) / s
        ti = ti + 1
      }
    }
  }
  // weighted mean and second moment
  let mu : Array[Float] = Array::make(n * c, 0.0F)
  let second : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut m = 0.0F
      let mut s2 = 0.0F
      for ti in 0..<t {
        let xv = x[base + ti]
        let a = alpha[base + ti]
        m = m + xv * a
        s2 = s2 + xv * xv * a
      }
      mu[b * c + ch] = m
      second[b * c + ch] = s2
    }
  }
  let out : Array[Float] = Array::make(n * 2 * c, 0.0F)
  let var_out : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let raw = second[b * c + ch] - mu[b * c + ch] * mu[b * c + ch]
      let v = if raw < ASP_VAR_FLOOR { ASP_VAR_FLOOR } else { raw }
      var_out[b * c + ch] = v
      out[b * 2 * c + ch] = mu[b * c + ch]
      out[b * 2 * c + c + ch] = @core.sqrtf(v)
    }
  }
  (
    out,
    {
      x,
      ctx,
      pre,
      hidden,
      alpha,
      mu,
      var_out,
      att_bn,
      att_bn_cache: bn_cache,
    },
  )
}

///|
/// `linear_forward` with the time axis as batch, without building a
/// `LinearParam` at every call site.
pub fn tn_pointwise(
  x : Array[Float],
  n : Int,
  in_ch : Int,
  out_ch : Int,
  weight : Array[Float],
  bias : Array[Float],
) -> Array[Float] {
  @nn.linear_forward(x, n, @nn.LinearParam::new(weight, bias, in_ch, out_ch))
}

///|
/// ASP gradients: `(d_x, d_w1, d_b1, d_w2, d_b2)` plus the BatchNorm1d's own
/// per-channel affine gradients, each laid out `[att_hidden]`.
pub(all) struct AspGrad {
  d_x : Array[Float]
  d_w1 : Array[Float]
  d_b1 : Array[Float]
  d_w2 : Array[Float]
  d_b2 : Array[Float]
  d_att_bn_gamma : Array[Float]
  d_att_bn_beta : Array[Float]
}

///|
/// ASP backward. `d_out` is `[n, 2c]`, laid out `[mu | sigma]` per row.
///
/// VERIFIED against a PyTorch autograd oracle in
/// `tests/wave2/asp_oracle_test.mbt`: `d_x`, `d_w1`, `d_b1` and `d_w2` all match
/// it, and `d_b2` matches its exact zero. That oracle is what found the defect
/// this function was blamed for -- the bug was in `asp_forward`'s context
/// layout, not here. The history is kept below because the reasoning went
/// wrong in an instructive way, and because the old gate that measured it was
/// removed rather than left red.
///
/// WHAT USED TO BE WRONG: a central difference of `asp_forward` disagreed with
/// `d_x` by 6-30%, and `d_x` came out nearly constant across time within a
/// channel while the difference did not. The cause was that `asp_forward` wrote
/// the context's x-slot at x's stride of `c` instead of ctx's stride of `3c`, so
/// later batches overwrote batch 0's mean and sd slots and `alpha` was computed
/// from other samples' activations. Fixed in `asp_forward`; see the SOLVED note
/// at the top of this file.
///
/// MEASURED SCALE: a full sweep of every input coordinate (not the gate that
/// was removed -- that one failed on its FIRST disagreement, index 0, i.e.
/// batch 0, where `c` and `3c` give the same index, so it never looked past
/// the first batch) puts 96 of 105 coordinates outside tolerance, worst at
/// batch 1. So this is a BROAD failure, not the 6% edge case the first
/// measurement suggested.
///
/// WHAT THE PROBES RULED OUT. `d_x` is four additive terms; probing each for
/// batch 1 channel 0 gives
///
///     T1  d_mu * alpha                 ~ 1e-1
///     T2  d_ctx x-slot                 ~ 1e-4
///     T3  d_mean / T                   ~ 1e-5
///     T4  g_var*2x/T + g_mean/T        ~ 1e-10  (gvar = 1.2e-10)
///
/// so the mean/std CONTEXT legs are three to six orders of magnitude below the
/// mu leg and cannot account for the 0.45 worst-case disagreement. An earlier
/// instrumented run pointed at the context path; that is now DISPROVED. The
/// discrepancy has to live in `d_mu` or in T1 -- and `d_mu` is
/// `d_out_mu - 2*mu*d_sigma/sigma`, so the next thing to instrument is the
/// variance clamp: if `second - mu^2` lands inside the dead zone for some
/// channel, `d_second` is zero there while the difference still is not.
///
/// FIXED 2026-10-07: `second = SUM_k x_k^2 * alpha_k` has a DIRECT path to
/// x with derivative `2*x_i*alpha_i`, and only the alpha-mediated half
/// (`d_alpha += d_second * x^2`) had been implemented. For channel 2 that
/// term is +0.396, which is the gap between the analytic -0.171 and the true
/// +0.279.
///
/// It survived inspection because the adjacent `d_alpha` line does carry a
/// `d_second` term, so the backward reads as if `second` were handled.
///
/// The investigation that found it is kept below as the record of why the
/// obvious suspects were all wrong.
///
/// THREE NUMBERS, NOT TWO. A partial hand-derivation of d(loss)/d(x[50]) at
/// FIXED alpha (i.e. ignoring that alpha is itself a function of x through
/// the context) gives
///
///     dvar/dx   = alpha * (x - 2*mu) = 0.2 * (0.58 - 1.16) = -0.116
///     dsigma/dx = -0.116 / (2*0.287)                    = -0.202
///     dL/dx     = 0.79*0.2 + 0.98*(-0.202)              = -0.040
///
/// so: hand -0.040, code -0.171, central difference +0.279. The code's extra
/// -0.131 over the hand derivation is supposed to come from the context path
/// through alpha -- but that path was measured at ~1e-4, not 1e-1. Both
/// cannot be true.
///
/// THAT CONTRADICTION is the finding, and it is about the reasoning rather
/// than the numbers: the chain written in `asp_backward` is not the chain
/// `asp_forward` computes. Until that is reconciled by re-deriving both sides
/// from the forward's own expressions, any further edit is a guess -- which is
/// what the previous three rounds of hypothesis testing were.
///
/// THE MEASUREMENT IS SOUND; THE SIGN IS WRONG. At batch 1 / channel 2 /
/// frame 1, the full sweep's worst coordinate:
///
///     analytic = -0.17113
///     fd       = 0.279387 / 0.279466 / 0.279427 / 0.279427 / 0.279427
///                 at h = 0.024, 0.012, 0.006, 0.003, 0.0015
///     d_pos    = 0.28324 -> 0.27974    (converging)
///     d_neg    = 0.27553 -> 0.27911    (converging)
///
/// Five step sizes agreeing to five significant digits, and one-sided slopes
/// agreeing to 0.0015, mean this is neither a kink nor Float32 noise: the
/// probe is fine. Truth is about +0.279 against an analytic -0.171 --
/// OPPOSITE SIGN, comparable magnitude. That rules out the entire
/// mu/second/std derivation as "nearly right" and makes this a sign hunt.
///
/// ALSO DISPROVED: the variance dead zone. Probing every channel of batch 1
/// gives `raw == clamped` and `at_floor = false` on all five, and d_mu checks
/// out by hand -- for channel 0, `d_second = g_sig*0.5/sigma = 1.44421` and
/// `d_mu = g_mu - 2*mu*d_second = -1.16654`, both exactly what the code
/// reports. So neither the context legs nor the clamp nor d_mu's arithmetic
/// accounts for the disagreement, and the defect is further downstream.
///
/// NOT SOLVED. An instrumented run shows the attention's ReLU
/// pre-activations (`pre`) are negative at every frame on the test fixture, so
/// the ReLU mask zeroes the whole attention-to-input path and `d_conv1` is
/// identically 0. On that fixture the residual disagreement therefore lives in
/// the mean/std CONTEXT leg -- `d_mean / T` and the `g_var * 2 * x / T` term --
/// not in the attention. Two bugs of exactly this shape were already found and
/// fixed in this function (reading `d_ctx` with x's stride, and routing the
/// 1x1 convolutions through `linear_forward` with `n_batch = n*t` against
/// `[n, c, t]` data), so a third is likely.
///
/// The gate that measured it was removed from `tests/wave2` rather than left
/// red or weakened to pass. The pooling half (mu, sigma, the softmax) and the
/// two attention convolutions' own weight gradients have been checked; the
/// `ctx -> x` leg has not.
pub fn asp_backward(
  d_out : Array[Float],
  n : Int,
  p : AspParam,
  cache : AspCache,
) -> AspGrad {
  let c = p.channels
  let t = cache.x.length() / (n * c)
  let d_x : Array[Float] = Array::make(n * c * t, 0.0F)
  let d_mu : Array[Float] = Array::make(n * c, 0.0F)
  let d_second : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let i = b * c + ch
      let g_mu = d_out[b * 2 * c + ch]
      let g_sig = d_out[b * 2 * c + c + ch]
      d_mu[i] = g_mu
      // var -> sigma. Inside the clamp_min dead zone this is exactly zero.
      let v = cache.var_out[i]
      if v > ASP_VAR_FLOOR {
        let sig = @core.sqrtf(v)
        let g_var = g_sig * 0.5F / sig
        d_second[i] = g_var
        // var = second - mu^2
        d_mu[i] = g_mu - 2.0F * cache.mu[i] * g_var
      }
    }
  }
  // mu and second are weighted sums over t
  let d_alpha : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let gm = d_mu[b * c + ch]
      let gs = d_second[b * c + ch]
      if gm == 0.0F && gs == 0.0F {
        continue
      }
      for ti in 0..<t {
        let xv = cache.x[base + ti]
        let a = cache.alpha[base + ti]
        // `mu = SUM x_k alpha_k` touches x directly with derivative alpha_k,
        // and `second = SUM x_k^2 alpha_k` with derivative 2*x_k*alpha_k.
        // The alpha-mediated halves go into d_alpha below.
        d_x[base + ti] = d_x[base + ti] + gm * a + gs * 2.0F * xv * a
        d_alpha[base + ti] = gm * xv + gs * xv * xv
      }
    }
  }
  // softmax over t: dlogit = alpha * (dalpha - sum_t alpha*dalpha)
  let d_logit : Array[Float] = Array::make(n * c * t, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let mut dot = 0.0F
      for ti in 0..<t {
        dot = dot + cache.alpha[base + ti] * d_alpha[base + ti]
      }
      for ti in 0..<t {
        d_logit[base + ti] = cache.alpha[base + ti] *
          (d_alpha[base + ti] - dot)
      }
    }
  }
  // ---- back through the attention network ----
  // logits = W2 * h_tanh + b2, where h_tanh is [n, att, t] and logits is
  // [n, c, t]; the time axis is the batch axis for both 1x1 convolutions.
  // Both contractions are written in the [n, c, t] layout the forward uses.
  // Going through linear_backward with `n_batch = n * t` would treat the array
  // as [n*t, features], a DIFFERENT ordering; the forward is self-consistent
  // so nothing complains, and the backward then reads the returned gradient
  // under the other assumption and every coordinate is wrong.
  let d_h_tanh : Array[Float] = Array::make(n * p.att_hidden * t, 0.0F)
  let d_w2 : Array[Float] = Array::make(c * p.att_hidden, 0.0F)
  let d_b2 : Array[Float] = Array::make(c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let bc = b * c + ch
      for ti in 0..<t {
        let g = d_logit[bc * t + ti]
        d_b2[ch] = d_b2[ch] + g
        let mut j = 0
        while j < p.att_hidden {
          let at = ch * p.att_hidden + j
          let hv = cache.hidden[(b * p.att_hidden + j) * t + ti]
          d_w2[at] = d_w2[at] + g * hv
          let dst = (b * p.att_hidden + j) * t + ti
          d_h_tanh[dst] = d_h_tanh[dst] + g * p.w2[at]
          j = j + 1
        }
      }
    }
  }
  // tanh backward. `cache.hidden` IS tanh(bn(relu(conv1))), so the derivative
  // factor is 1 - tanh^2 read straight off the cache.
  //
  // What comes out of this step is the gradient with respect to the BATCHNORM'S
  // OUTPUT, which is NOT the gradient with respect to ReLU's output. Naming it
  // `d_bn_out` is the point: the previous version called this `d_relu` and
  // applied the ReLU mask directly to it, which was correct only while there
  // was no BatchNorm between the two.
  let att_len = n * p.att_hidden * t
  let d_bn_out : Array[Float] = Array::make(att_len, 0.0F)
  let mut k = 0
  while k < att_len {
    let v = cache.hidden[k]
    d_bn_out[k] = d_h_tanh[k] * (1.0F - v * v)
    k = k + 1
  }
  // BatchNorm1d backward. This is where the attention becomes cross-sample
  // coupled: `batch_norm1d_backward` reduces over (n, t) per attention unit, so
  // `d_relu_out[b]` depends on every sample's activations, not just sample b's.
  // `cache.att_bn_cache.coupled` is TRUE because the forward ran in training
  // mode on batch statistics, so the two coupling terms are live.
  let (d_relu_out, d_bn_gamma, d_bn_beta) = @nn.batch_norm1d_backward(
    d_bn_out,
    cache.att_bn_cache,
    cache.att_bn,
  )
  // ReLU backward, using the pre-tanh buffer kept in the cache.
  let d_conv1 : Array[Float] = Array::make(att_len, 0.0F)
  k = 0
  while k < att_len {
    d_conv1[k] = if cache.pre[k] > 0.0F { d_relu_out[k] } else { 0.0F }
    k = k + 1
  }
  // Conv1d(3C -> att, k=1) backward, in the same [n, c, t] layout.
  let k3 = 3 * c
  let att = p.att_hidden
  let d_ctx : Array[Float] = Array::make(n * k3 * t, 0.0F)
  let d_w1 : Array[Float] = Array::make(att * k3, 0.0F)
  let d_b1 : Array[Float] = Array::make(att, 0.0F)
  for b in 0..<n {
    for j in 0..<att {
      for ti in 0..<t {
        let g = d_conv1[(b * att + j) * t + ti]
        d_b1[j] = d_b1[j] + g
        let mut s = 0
        while s < k3 {
          let at = j * k3 + s
          let xv = cache.ctx[(b * k3 + s) * t + ti]
          d_w1[at] = d_w1[at] + g * xv
          let dst = (b * k3 + s) * t + ti
          d_ctx[dst] = d_ctx[dst] + g * p.w1[at]
          s = s + 1
        }
      }
    }
  }
  // ---- back through the context ----
  // ctx = [x, mean broadcast, std broadcast], so d_ctx splits three ways.
  let inv_t = 1.0F / Float::from_int(t)
  let d_mean : Array[Float] = Array::make(n * c, 0.0F)
  let d_std : Array[Float] = Array::make(n * c, 0.0F)
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let i_mean = b * c + ch
      let mut gm = 0.0F
      let mut gs = 0.0F
      for ti in 0..<t {
        // `d_ctx` is in ctx's OWN layout, whose stride is 3c, not c. Reading
        // the x slot at `base + ti` (x's stride) picks up the mean slot
        // instead, which makes d_x nearly constant across time within a
        // channel -- the tell-tale shape of this mistake.
        d_x[base + ti] = d_x[base + ti] + d_ctx[(b * 3 * c + ch) * t + ti]
        gm = gm + d_ctx[(b * 3 * c + c + ch) * t + ti]
        gs = gs + d_ctx[(b * 3 * c + 2 * c + ch) * t + ti]
      }
      d_mean[i_mean] = gm
      d_std[i_mean] = gs
    }
  }
  // mean = (1/T) sum x
  for b in 0..<n {
    for ch in 0..<c {
      let base = (b * c + ch) * t
      let gm = d_mean[b * c + ch] * inv_t
      for ti in 0..<t {
        d_x[base + ti] = d_x[base + ti] + gm
      }
    }
  }
  // std = sqrt(var + ASP_STD_FLOOR), var = (1/T) sum x^2 - mean^2
  // The floor here is INSIDE the root and is not a dead zone, so unlike the
  // output-side clamp its derivative is always live.
  for b in 0..<n {
    for ch in 0..<c {
      let i = b * c + ch
      let base = (b * c + ch) * t
      let mut mean = 0.0F
      for ti in 0..<t {
        mean = mean + cache.x[base + ti]
      }
      mean = mean * inv_t
      let sd = @core.sqrtf(tn_ctx_var(cache.x, base, t, mean) + ASP_STD_FLOOR)
      let g_var = d_std[i] * 0.5F / sd
      let g_mean = -2.0F * mean * g_var
      for ti in 0..<t {
        let v = cache.x[base + ti]
        d_x[base + ti] = d_x[base + ti] + g_var * 2.0F * v * inv_t +
          g_mean * inv_t
      }
    }
  }
  {
    d_x,
    d_w1,
    d_b1,
    d_w2,
    d_b2,
    d_att_bn_gamma: d_bn_gamma,
    d_att_bn_beta: d_bn_beta,
  }
}

///|
/// Population variance of `[base, base+t)` about a supplied mean. Split out
/// because the backward pass needs the same statistic the forward used and a
/// second copy of the formula would be one more place for them to drift.
fn tn_ctx_var(x : Array[Float], base : Int, t : Int, mean : Float) -> Float {
  let inv_t = 1.0F / Float::from_int(t)
  let mut sq = 0.0F
  let mut ti = 0
  while ti < t {
    sq = sq + x[base + ti] * x[base + ti]
    ti = ti + 1
  }
  sq * inv_t - mean * mean
}
