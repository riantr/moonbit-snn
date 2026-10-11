docs(tarnet): pin the ASP BatchNorm failure to a single link

Comment-only, but it is the most expensive thing this session learned, so it
belongs in the code rather than in a commit message nobody will read again.

Splitting ASP's d_x into its two sources -- the DIRECT mu/second term, and
everything through ctx -- is possible from the cache alone, so no library
instrumentation was needed. At the worst coordinate (i=59, batch 1, channel 3,
time 3) with the BatchNorm applied:

    fd     = 0.06866455078125     converged, stable across a 32x step cut
    total  = 0.08458627015352249  what asp_backward returns
    direct = 0.08287138491868973  the direct term alone
    ctx    = 0.0017148852348327637 the ctx path alone

d_x there is ~98% direct, and the direct path matches fd to within float32 --
so `d_mu`, `d_var`, `alpha` and the variance floor are all exonerated, which
they were not before this. The entire 0.0159 discrepancy is in the ctx path,
whose true cross-sample contribution is about -0.0159 against +0.0017 computed:
wrong by roughly ten times its own magnitude.

Within that path the BatchNorm is the only operation that mixes samples, so
the next thing to instrument is what `batch_norm2d_backward` returns as
d_input when the incoming gradient already carries cross-sample content --
even though that function is separately proven correct on a fixture of exactly
this shape (c=6, h=1, w=7) with both constructors. Those two facts cannot
both be true, and which one gives is the whole question.