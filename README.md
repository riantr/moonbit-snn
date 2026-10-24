# moonbit-snn

Bit-exact MoonBit port of [SpikingNeuralNetworks.jl](https://github.com/AlessioQuer/SpikingNeuralNetworks.jl),
extended with the training machinery the Julia ecosystem never had.

[![CI](https://github.com/riantr/moonbit-snn/actions/workflows/ci.yml/badge.svg)](https://github.com/riantr/moonbit-snn/actions/workflows/ci.yml)
[![Release](https://github.com/riantr/moonbit-snn/actions/workflows/publish.yml/badge.svg)](https://github.com/riantr/moonbit-snn/actions/workflows/publish.yml)

The MoonBit implementation lives in [`mbt/`](./mbt/), and is published to
mooncakes.io as [`riantr/snn_mbt`](https://mooncakes.io/docs/riantr/snn_mbt).
The package's own page — status, scope table, provenance, and an explicit list
of what is *not* implemented — is [`mbt/README.md`](./mbt/README.md).

Since v0.166.0 the library is split across 24 packages under `mbt/`, and the
root package holds no source of its own: `mbt/moon.pkg` is an import list.

## What this is, and what it is not

This is the **simulation and training layer** of the Julia SNN ecosystem:
neurons, synapses, plasticity, stimuli, and the model/simulation harness —
plus, written here rather than ported, the deep-learning machinery the Julia
packages did not have (convolution, attention, reinforcement learning,
generative models, causal inference, meta-learning).

**The long-term goal is not met.** The goal is that every example in
`SpikingNeuralNetworks.jl/examples/` runs under MoonBit and produces the same
numerical trajectories, last-bit Float32, as the Julia run. Twelve examples
carry a recorded comparison against the Julia side and five of those are
positive; the comparison machinery for the rest exists but has never been
closed against a reference trace. The package README says which is which, per
example, in the table *Network experiments summary*.

Three layers of evidence back the work, and they are not equally strong:
self-consistency everywhere (1925 assertions across nine waves), bit-exact
comparison against the Julia upstream (partially available — the machinery is
here, the reference traces are not), and comparison against an independent
reference implementation (applied to TARNet's ASP head and its BatchNorm1d).
The rest of this file explains how each of them is checked, and why a fourth
layer — "the test suite is green" — is not one of them.

## Install

```sh
moon add riantr/snn_mbt@0.166.2
```

That writes `import { "riantr/snn_mbt@0.166.2" }` into your `moon.mod`. The
root package exports no symbols — it is a manifest, not a body — so import the
packages you actually want:

```moonbit
import {
  "riantr/snn_mbt/core"
  "riantr/snn_mbt/neuron"
}
```

The module is **native-only**. `core/` binds libm through `extern "C"`, and
`mbt/moon.mod` declares `preferred_target = "native"` to match; there is no
wasm build of this package, and the CI compiles it with `--target native` for
that reason.

## Quick start

```sh
git clone https://github.com/riantr/moonbit-snn.git
cd moonbit-snn/mbt
moon update
moon check --target native
moon run examples/chain --target native
```

Expected output:

```text
chain.mbt: simulation done
  v[0] = -68.17937
  v[1] = -69.97869
  v[2] = -69.98688
  recorded steps: 801
Monitor[v] n=801 min=-68.17937 max=-51.54429 mean=-65.70784
```

Tests live in nine sub-packages under `mbt/tests/`, not in the package root.
`moon test` on the root is not a valid entry point: Moon inlines every source
path into the Windows command line, the package exceeds the 32K cap
(CreateProcessW), and the root's test target cannot link. Run a wave instead:

```sh
cd mbt
moon test ./tests/wave1 --target native   # ... through ./tests/wave9
```

That is 1925 tests. A test placed in the root would never run, so CI fails the
build if one appears there.

## Quality gates

```sh
python verify/qa_loop.py            # every gate, one exit code
python verify/qa_loop.py --list     # the registry
python verify/qa_loop.py --quick    # skip the slow gates
python verify/qa_loop.py --ci       # the CI-portable subset
```

20 gates: formatting, static analysis, dependency shape, a dependency-pin check
against the registry, duplicate-code detection, a secret scan, the TARNet
autograd oracle's self-test, the reference transcription audit, a release
workflow audit, a golden-file reproducibility check, the unit suite, the
documented counts, the example suite, property checks, the benchmark harness, a
performance baseline, coverage, a repeat-run stability check, mutation testing,
and a shadowed-definition scan. A gate that does not run reports `unavailable`
with the reason rather than `pass` — "did not check" is a finding, not a
success.

The `deps` gate measures the declared package graph rather than trusting it:
24 packages, a longest chain of 7, and `infra` at the top importing 11 others.
Those numbers are recorded in `verify/deps_baseline.json`, and the gate fails
when the chain deepens past them. It exists because the compiler has no
opinion about depth — only about cycles, and a cycle is a build error anyway.

The mutation gate is the one worth knowing about. It injects mutants into the
CPU reference loops in `mbt/gpu/cuda/linear_accel_stub.c` and checks that
`mbt/fuzz_props/` notices. It currently kills 3 of 3. When it did not, the
property suite was not covering a primitive at all, which is the kind of gap
a passing test count hides.

The `fmt` gate deserves a note, because it used to be a lie. It ran
`moon fmt --check` from the repository root, where there is no `moon.mod`
and no `moon.work`, so every invocation died with "not in a Moon project",
the fallback could not run either, and the gate reported *formatting clean*
every time. It now measures the thing instead: **621** source files are not in
the formatter's canonical form — the tree predates the spelling the toolchain
now prefers, `Int` for `Int32` and `x?` for `x~` — and the gate records that
distance in `verify/fmt_baseline.json` and fails when it moves in either
direction. A file becoming non-canonical is drift; a file becoming canonical
means the record is stale.

Note the number, because `moon fmt --check` cannot tell you it: that command
stops after ten differences and the ten rotate between runs on the same tree.
It found "ten files" in a previous note in this repository, and the real
number is 621.

The battery is the other half of that question. `python verify/gate_battery.py`
injects a deliberate fault into the thing a gate measures, runs the gate, and
requires it to go **red**; then it removes the fault and requires the gate to
go green again — so the red is attributable to the fault and not to the
machine, the clock, or a gate that was already broken. It covers `lint`,
`integration`, `coverage`, `fuzz`, `stress`, `mutation`, `oracle`,
`provenance`, `release`, `fmt`, `deps` and `docs` — 21 fault cases across 12
gates; the other 8 were proved by hand.
Every file it touches is restored byte-exactly, because `core.autocrlf=true`
with no `.gitattributes` means `git checkout --` would rewrite a file with
CRLF and change its bytes.

### What runs where

The release is tag-driven: `.github/workflows/publish.yml` publishes when a
`v*` tag appears. It runs the same seven process gates on the tree it is
about to publish, then `moon check` and the nine test waves, and it is the
only thing that publishes.

`.github/workflows/ci.yml` runs on every push and runs the 7 gates that need
no toolchain — `deps`, `pin`, `dup`, `shadow`, `security`, `release`, `docs`
— plus `moon check`. These are the checks the release workflow structurally
cannot make about itself: it is the thing being checked, from the inside,
where it cannot see its own path. Three releases here died at three
different steps of that workflow, and every one of them was the
workflow's fault rather than the code's.

The `provenance` gate is deliberately not among them. It audits the oracle
against a checkout of the reference under `_build/`, which is gitignored, so
on a runner there is nothing to audit against; it reports `unavailable`
rather than `pass`. That is the rule working: the first version of the CI
list did include it, and the first CI run failed on exactly this.

The gates that need PyTorch or a release build stay local, on purpose: the
performance budget is derived from a median measured on one machine, and a
runner is a colder and a slower machine than that. A gate that fails for
environmental reasons teaches everyone to ignore red. In CI, a gate that
could not check anything fails the job — "did not check" is not a pass.

Two gradient harnesses remain, for checks that need to execute rather than
type-check:

```powershell
& ".\verify\verify_gnn_grads.ps1"      # gradient gate
& ".\verify\run_gnn_train_demo.ps1"     # end-to-end training demo
```

Both temporarily flip `"is-main": true` into `mbt/moon.pkg` and restore
it afterwards, because a freshly created sub-package cannot resolve any
symbol from the parent module. See
[`mbt/README.md`](./mbt/README.md#verification-harness) for the criteria.

### TARNet against PyTorch autograd

Every other gradient gate in this repository compares a layer against a finite
difference of **its own** forward. That cannot see a forward which is wrong in a
self-*consistent* way, and whole-model wiring is exactly where that lives:
concatenate the three scale stacks in the wrong order and the forward and the
backward are both wrong in matching ways, every finite difference agrees with
both, the loss still descends, and the model optimises a network that is not the
paper's.

`mbt/tarnet/` is checked against PyTorch autograd end to end instead. Before
using it, read [`mbt/tarnet/PAPER_VS_CODE.md`](./mbt/tarnet/PAPER_VS_CODE.md) —
the paper and the authors' released code do not describe the same network, and
the difference is structural rather than a hyperparameter.

```sh
python verify/gen_model_oracle.py --selftest     # the oracle validates ITSELF
python verify/audit_transcription.py --selftest  # the audit can FAIL
python verify/gate_canary.py                     # proves each gate can fail
```

The selftest needs no MoonBit, no goldens and no export: it differentiates the
oracle's own forward in float64, along random directions, and compares against
its analytic gradients. Directional differences rather than per-coordinate ones,
because the network is kinked (ReLU, PReLU, `clamp`) and a coordinate probe of
step 1e-5 against pre-activations of order 9e-3 steps across zero constantly —
measured per-coordinate relative error was 1e-1 to 3e+0, and got *worse* as the
step shrank.

This matters more than its test count suggests. The oracle had six defects of its
own behind it, including one whose headline number was `absmax(x) / absmax(x)` —
identically 1.0, green regardless of what the backward did — and one that made
the logit gradient six frames wide so it silently *broadcast* against the
logits. Four fix-diagnose-revert cycles had blamed the port for faults that were
in the reference. A reference that cannot validate itself is a reference that
gets trusted, and being trusted is what made it wrong four times.

### Provenance is checked, not asserted

The oracle's independence rests entirely on one sentence in its header: it is
transcribed from the upstream `tarnet/models/tarnet.py` rather than from this
port. `verify/audit_transcription.py` makes that sentence checkable — it reads
the reference and asserts 31 structural facts the oracles depend on: the
concat order of the three scale stacks, SE after the ReLU rather than before,
the `1e-4` ASP floors, `max(32, fuse_out // 4)` for the SE bottleneck, the
depthwise padding formula, the dilation lists, and the
`bn_seg → fc → bn_out` tail.

It reports **31/31** against the current reference, and it lists the two
deliberate deviations as assertions rather than excuses: the port shares one
PReLU slope where the reference declares two, and it inits the classifier with
`randn(0.05)` where the reference uses `xavier_uniform_`. Its `--selftest`
mutates the reference ten ways and requires each mutation to turn its own check
red and nothing else.

`_build/` is gitignored, so the reference is a local checkout. Where it is
absent this gate reports `unavailable`, not `pass` — and that is a real
limitation worth stating plainly: outside this checkout, the transcription is
unverified.

### The acoustic front-end is checked too

The autograd gate starts at a log-Mel spectrogram, which leaves everything
*before* it — pre-emphasis, the STFT, the mel filterbank, the log and the
per-band mean removal — verified only by invariants. Those cannot see a
composition that is uniformly wrong, and `mbt/tests/wave2/logmel_oracle_test.mbt`
exists because it did not catch a real defect: every mel triangle was half height,
because both edges were divided by the full band width instead of their own
half-widths.

The reference is `verify/gen_logmel_golden.py`, whose STFT is `torch.stft` — a
library call — against this package's hand-rolled radix-2 FFT. The four stages
agree to **7.9e-08** (filtered signal), **1.8e-06** (power spectrum), **1.0e-05**
(mel filterbank) and **2.5e-03** (log-Mel output). The last figure is the one
that matters, because the log-Mel output is what the model consumes; the
earlier stages are intermediates, and the tolerances in
`logmel_oracle_test.mbt` are set per stage for that reason.

Two things from that investigation are worth more than the fix:

- **The obvious guard does not exist.** "Every band peaks at 1.0" is *false* — the
  triangle is continuous in Hz and sampled at 257 bin frequencies 31.25 Hz apart,
  so it peaks at 1.0 only when a bin lands on the band's centre. Band 3's centre
  misses its nearest bin by 12.6 Hz and peaks at 0.4934; torchaudio has the same
  property, so such a gate would be red on a *correct* implementation.
- **The output is the wrong thing to gate.** The error is a per-band constant,
  and `log` followed by per-band mean subtraction removes a per-band constant
  exactly — so fixing it moved the log-Mel output from 4.9e-03 to 2.5e-03 while
  the filterbank moved from 5.1e-01 to 1.0e-05. A gate on the output would have
  been a gate that could not see the bug it was written for, so the output is
  reported and the filterbank is the gate.

`python verify/gate_canary.py` exists because of that history: it reintroduces
each defect on a scratch copy of the tree and requires the corresponding gate to
report `FAILED > 0`. A gate that has only ever been green measures nothing.

## Optional GPU path

`mbt/gpu/` is a single-precision BLAS surface for the dense layers in `nn/`.
It is **deliberately unconnected**: no library package imports it — only the
two harness packages that test it do — and `nn/` itself declares no
`extern "C"`. The module as a whole is native-only anyway — `core/` binds libm
the same way — so "unconnected" is a statement about the GPU seam, not about
wasm portability.

The reason is measured. At the LeNet5 fc1 shape (batch 64, 4000 → 500, 256
MFLOP per call, release build):

| | |
|---|---|
| `nn/linear_forward` today | 73.4 ms |
| `Array` → `FixedArray`, weight (8.0 MB) | 4.5 ms |
| PCIe 4.0 ×16 round trip, 1.0 MB | 0.04 ms |

The first figure is the recorded baseline in `verify/perf_baseline.json`,
measured on an Intel Core Ultra 9 285H. The `perf` gate measures the same
workload on every run and compares the median against a budget of 150 ms; on
the machine that took the baseline it reads a little under it, and it reports
the value it measured rather than the one in this table.

`FixedArray::from_array` is a deep copy, so a dispatch that converted per call
would pay ten times the transfer for the input and a hundred and ten times for
the weight before a single byte reached the device. Connecting this properly
means `LinearParam` holding its weights as a `FixedArray` — an invasive change
across every layer, optimiser and test that touches one — against a benefit
this repository cannot demonstrate without hardware it does not have. The
numbers are recorded in [`mbt/gpu/moon.pkg`](./mbt/gpu/moon.pkg).

## Project layout

- `mbt/` — the MoonBit port: **301 library source files** (tracked `*.mbt`
  outside `mbt/tests/` and `mbt/examples/`), about 65,300 lines, across
  **24 packages**, plus `mbt/examples/` (**77 runnable examples**).
- `mbt/core`, `mbt/neuron`, `mbt/synapse`, `mbt/stimulus` — the simulation
  core. `synapse` is the largest of the four at about 8,000 lines.
- `mbt/nn`, `mbt/optim`, `mbt/activ`, `mbt/attention`, `mbt/infra` — the
  deep-learning layer and its training machinery.
- `mbt/rl` — reinforcement learning, about 15,100 lines, the largest single
  package.
- `mbt/gnn`, `mbt/generative`, `mbt/causal`, `mbt/meta`, `mbt/capsule`,
  `mbt/autodiff`, `mbt/tarnet`, `mbt/models`, `mbt/analysis`,
  `mbt/timeseries` — the newer domains, each its own package.
- `mbt/gpu/` — the optional BLAS seam, described above.
- `mbt/tests/wave1` … `wave9` — 1925 tests, split by domain so a failure
  points at one. That is 258 test files, and none of it can live in the
  package root; see Quick start.
- `mbt/fuzz_props/`, `mbt/gpu_selftest/` — property and fallback checks,
  driven by the mutation and fuzz gates.
- `mbt/bench_lin/` — the release-mode timing of the hot kernel, which the
  `perf` gate measures against a budget derived from the measurement.
- `tools/split_tests/` — the rewriter that qualifies a root test for a
  sub-package, and its README, which records why the split exists and what
  the allow-list must not be derived from.
- `verify/` — the QA loop, the two gradient harnesses, and the script that
  runs them inside the library package.
- `refs/` — local working copies of the upstream Julia reference
  repos used as the bit-exact specification (`SpikingNeuralNetworks.jl`,
  `SNNModels.jl`, `SNNUtils.jl`). **Not tracked in git.**
- `_build/`, `_disabled/` — build artifacts and discarded work.
  **Not tracked in git.**

## A note on numbers

Performance figures in this repository come from release builds. The debug
profile is about **12×** slower — roughly 860 ms against 70 ms for the same
call — so any timing taken from `moon test` or `moon run` without `--release`
is wrong by that factor. `verify/run_release_bench.ps1` exists for this, and its
header records why a baseline subtraction is required rather than merely tidy:
on Windows the `time` builtin reports ~0.02 s of user CPU for a run whose wall
clock is 2 s, so only elapsed-minus-baseline is usable.

The same caution applies to the counts in this file. They are measured, not
estimated, and they are pinned: the tree counts in
`verify/tree_baseline.json`, the suite in `verify/test_baseline.json`, the
gate counts in `verify/gate_baseline.json`. The `docs` gate counts the tree
from what git actually tracks, compares it with the baseline, and then checks
that this file and `mbt/README.md` still quote the numbers — including the
package table row by row, the list of runnable examples, and each row's own
test count. An earlier version of this file said "681 source files, ~125k
lines across 23 packages" while the package README said "732 source files" —
both wrong, both counting the 400 vendored `.mbt` files that `moon update`
leaves in `mbt/.mooncakes/`. The dependency cache is not this package's
source, and `git ls-files` is how the difference is now made.

## A note on `git status` after a build

A build regenerates the 80 tracked `pkg.generated.mbti` interface snapshots
and rewrites them with new timestamps, so `git status` reports all 80 as
modified right after a build — while `git diff` shows **no** content change
whatsoever, and `git add -A` stages nothing.

It is a stale index cache, not a change. The index still holds the size and
timestamp of the checkout, and the rewritten files no longer match that
size, so git re-checks them and finds them identical; the stat entry it
cached is never refreshed, and the files stay listed as modified until
something refreshes the index. `git add -A` does exactly that, and the tree
is clean again.

Nothing can be committed out of that state — the content is identical, so
the staged diff is empty and `git commit -a` reports that there is nothing
to commit — but it looks alarming enough to be worth saying here, because
the reflex is to "fix" it with a commit, and the only way to get one is to
change the line endings by hand.

## Provenance and license

Three Julia packages are the source of the ported files, and all three are MIT,
as is this package:

| Upstream | Vendored at | Ported into |
|---|---|---|
| SpikingNeuralNetworks.jl v1.0.0 | `SpikingNeuralNetworks.jl/` | `neuron/`, `synapse/`, `analysis/`, `stimulus/`, `infra/` |
| SNNModels.jl | `refs/SNNModels.jl/` | `core/`, `nn/structs_extra.mbt` |
| SNNUtils.jl v0.2.0 | `refs/SNNUtils.jl/` | `infra/sim.mbt`, `infra/compose.mbt`, `infra/population.mbt`, `infra/bimodal_kernel.mbt` |

Everything else is original work, written against a published paper with the
citation in the file. `mbt/README.md` carries the full table — 74 of the 301
library files are translations of a named upstream, and the paper behind
each original area.
