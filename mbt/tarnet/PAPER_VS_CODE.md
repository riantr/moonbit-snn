# TARNet: where the paper and the released code disagree

**Read this before trusting `mbt/tarnet/` to be "the paper".** It implements the
authors' released PyTorch code. That code and the paper do not describe the same
network in every respect, and in one place they disagree about the *topology* of
the encoder rather than a hyperparameter.

Sources:

- Paper — arXiv:2605.07735v1, *TARNet: A Temporal-Aware Multi-Scale Architecture
  for Closed-Set Speaker Identification* (Terraf & Iraqi, ICME 2026).
  Section and equation numbers below refer to that version.
- Code — `_build/tarnet_ref/tarnet/models/tarnet.py` from the authors' repository,
  <https://github.com/YassinTERRAF/TARNet>.

`verify/audit_transcription.py` checks that the oracles still match the **code**.
Nothing automates this table, because the paper is not in the tree — so this file
is the record, and it is a record of *deliberate* choices, not of oversights.

## 1. The encoder is CASCADED in the paper and PARALLEL in the code

This is the one that matters.

The paper's equations chain the stages:

> **Eq. (1)** `X_S = (B_{d_s,2} ∘ B_{d_s,1})^R (X_0)`
> **Eq. (2)** `X_M = (B_{d_m,2} ∘ B_{d_m,1})^R (`**`X_S`**`)`
> **Eq. (3)** `X_L = (B_{d_l,2} ∘ B_{d_l,1})^R (`**`X_M`**`)`

and the prose is unambiguous about it — "**three cascaded** temporal stages",
"Building on `X_S`, the second stage...", "operating on `X_M` with the largest
dilation set". Each stage consumes the previous stage's output.

The code runs all three off the same bottleneck and concatenates the results:

```python
x_s = self._run_stack(x0, self.stack_s)   # (B, C, T)
x_m = self._run_stack(x0, self.stack_m)   # (B, C, T)
x_l = self._run_stack(x0, self.stack_l)   # (B, C, T)
z = torch.cat([x_s, x_m, x_l], dim=1)
```

The class is named for this: `TCN_ParallelMultiTap_MFA_ASP_Backbone`. "Parallel"
and "multi-tap" are the topology the code implements.

**This port follows the code.** All three stacks take the bottleneck, and
`tn_concat3` concatenates the three results. See `tarnet_backward.mbt`,
`tarnet_forward_full`.

Note the paper is *internally* consistent here — concatenating the three stage
outputs is a "multi-tap" read of a cascade, which is a real architecture. It is
simply not the one in the repository, and the two are not the same function: in a
cascade the mid- and long-term representations are conditioned on everything
downstream of the short stage, and in a parallel tap they are not.

## 2. The long stage has one dilation in the code and two in the paper

| | paper (§III-B) | code |
|---|---|---|
| short | `{1, 2}` | `[1, 2]` |
| mid | `{4, 8}` | `[4, 8]` |
| long | `{16, 32}` | `[16]` |

`build_stack([16], repeats_l)` is a single-element list. The paper's `D_L` is a
two-element set and Eq. (3) is written as `B_{d_l,2} ∘ B_{d_l,1}`, i.e. two
blocks composed before the repetition.

This port follows the code: `tn_build_stack(dil_l = [16], ...)`.

## 3. Repetitions R = 3 in the paper, 4 in the code

§III-B: "each stage repeated R = 3 times". The reference defaults are
`repeats_s = repeats_m = repeats_l = 4`.

This port uses 4 (`Tarnet::new`'s `repeats_s~ / repeats_m~ / repeats_l~ = 4`).
Combined with §2, the block count differs: the paper's configuration is
`3 stages x 2 dilations x R=3 = 18` blocks; this port has `8 + 8 + 4 = 20`.

## 4. The bottleneck has a global layer norm the paper does not mention

§II-A describes the bottleneck in one sentence:

> "a bottleneck projection is applied using a 1×1 convolution, which linearly
> projects X into a compact channel representation"

No normalisation. The code is:

```python
self.bottleneck = nn.Sequential(
    layer_norm,                                   # <- gLN, per §III-B norm_type="gLN"
    nn.Conv1d(in_chan, bn_chan, kernel_size=1, bias=True),
)
```

This port applies `@nn.gln_forward` before the convolution, following the code.
Fig. 2 labels "G-norm" as global layer normalization but places it inside the TCN
block, so the bottleneck's normalisation appears to be undocumented in the paper
rather than absent from the design.

## 5. The ASP head has layers the paper does not describe

§II-C describes the attention as "a lightweight **two-layer 1×1 convolutional
network**... applies a nonlinear transformation followed by a softmax operation
along the temporal dimension".

`ASPHead.attention` is:

```
Conv1d(3C, att_hidden, 1) -> ReLU -> BatchNorm1d(att_hidden) -> Tanh
                           -> Conv1d(att_hidden, C, 1) -> Softmax(dim=2)
```

The BatchNorm1d and the Tanh are not in the paper's description. Note the Tanh
and BatchNorm are also *load-bearing* rather than incidental: the Tanh bounds the
attention logits, and the BatchNorm is the one normalisation in this network whose
reduction spans the batch, which is why its backward cannot be checked one sample
at a time. Removing them to match the paper's prose would be a real change, not a
transcription fix.

This port follows the code. See `tarnet_asp.mbt`.

## 6. The front-end has steps the paper does not list

§II-A: "log-Mel spectrogram features are extracted following standard
time–frequency processing, including short-time Fourier transform, Mel filterbank
projection, and logarithmic compression". §III-B adds only "80-dimensional
log-Mel".

The code additionally applies **pre-emphasis**, a `clamp_min(1e-6)` before the
logarithm, and **cepstral mean normalisation** (`x - x.mean(dim=-1)`) afterwards.
All three are standard in VoxCeleb-style pipelines, and all three are implemented
here (`tarnet_logmel.mbt`), but none is stated in the paper.

## What follows from this

The honest summary is that `mbt/tarnet/` reproduces the **released
implementation**, which is the artefact that produced the paper's numbers. Where
the paper's equations and the code disagree, matching the code is the choice that
keeps this port comparable to the authors' reported results — but it means the
port is not a literal reading of Eq. (1)–(3).

Changing §1 to match the paper is a substantial piece of work, not a fix: it
alters the encoder's dataflow, the residual paths, the backward, and the parameter
count, and it invalidates every golden in `mbt/tests/wave2/`. Anyone doing it
should start from the autograd gate rather than from the code, because that gate
is the only thing here that would tell them whether the new topology is right.
