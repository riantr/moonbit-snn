# moonbit-snn

Bit-exact MoonBit port of [SpikingNeuralNetworks.jl](https://github.com/AlessioQuer/SpikingNeuralNetworks.jl).

The MoonBit implementation lives in [`mbt/`](./mbt/). See
[`mbt/README.md`](./mbt/README.md) for full status, design notes, and
test history.

Since v0.166.0 the library is split across 23 sub-packages and the root
package holds no source of its own.

## Quick start

```sh
cd mbt
moon check --target native
```

Tests live in nine sub-packages under `mbt/tests/`, not in the package root.
`moon test` on the root is not a valid entry point: Moon inlines every source
path into the Windows command line, the package exceeds the 32K cap
(CreateProcessW), and the root's test target cannot link. Run a wave instead:

```sh
cd mbt
moon test ./tests/wave1 --target native   # ... through ./tests/wave9
```

That is 1884 tests. A test placed in the root would never run, so CI fails the
build if one appears there.

## Quality gates

```sh
python verify/qa_loop.py            # every gate, one exit code
python verify/qa_loop.py --list     # the registry
python verify/qa_loop.py --quick    # skip the slow gates
```

Fifteen gates: formatting, static analysis, dependency shape, duplicate-code
detection, a secret scan, the TARNet autograd oracle's self-test, the reference
transcription audit, the unit suite, the example suite, property checks, a
performance baseline, coverage, a repeat-run stability check, and mutation
testing. A gate that does not run reports `unavailable` with the reason rather
than `pass` — "did not check" is a finding, not a success.

The mutation gate is the one worth knowing about. It injects mutants into the
CPU reference loops in `mbt/gpu/cuda/linear_accel_stub.c` and checks that
`mbt/fuzz_props/` notices. It currently kills 3 of 3. When it did not, the
property suite was not covering a primitive at all, which is the kind of gap
a passing test count hides.

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

`python verify/gate_canary.py` exists because of that history: it reintroduces
each defect on a scratch copy of the tree and requires the corresponding gate to
report `FAILED > 0`. A gate that has only ever been green measures nothing.

## Optional GPU path

`mbt/gpu/` is a single-precision BLAS surface for the dense layers in `nn/`.
It is **deliberately unconnected**: no package imports it, so `nn/` stays free
of `extern "c"` and remains importable from wasm.

The reason is measured. At the LeNet5 fc1 shape (batch 64, 4000 → 500, 256
MFLOP per call, release build):

| | |
|---|---|
| `nn/linear_forward` today | 98.4 ms |
| `Array` → `FixedArray`, weight (8.0 MB) | 4.5 ms |
| PCIe 4.0 ×16 round trip, 1.0 MB | 0.04 ms |

`FixedArray::from_array` is a deep copy, so a dispatch that converted per call
would pay ten times the transfer for the input and a hundred and ten times for
the weight before a single byte reached the device. Connecting this properly
means `LinearParam` holding its weights as a `FixedArray` — an invasive change
across every layer, optimiser and test that touches one — against a benefit
this repository cannot demonstrate without hardware it does not have. The
numbers are recorded in [`mbt/gpu/moon.pkg`](./mbt/gpu/moon.pkg).

## Project layout

- `mbt/` — the MoonBit port: 681 source files, ~125k lines across 23 packages,
  plus `mbt/examples/` (77 runnable examples).
- `mbt/core`, `mbt/neuron`, `mbt/synapse`, `mbt/stimulus` — the simulation
  core. `synapse` is the largest at ~8k lines.
- `mbt/nn`, `mbt/optim`, `mbt/activ`, `mbt/attention`, `mbt/infra` — the
  deep-learning layer and its training machinery.
- `mbt/rl` — reinforcement learning, ~15k lines, the largest single package.
- `mbt/gnn`, `mbt/generative`, `mbt/causal`, `mbt/meta`, `mbt/capsule`,
  `mbt/autodiff`, `mbt/tarnet`, `mbt/models`, `mbt/analysis`,
  `mbt/timeseries` — the newer domains, each its own package.
- `mbt/gpu/` — the optional BLAS seam, described above.
- `mbt/tests/wave1` … `wave9` — 1884 tests, split by domain so a failure
  points at one. None of it can live in the package root; see Quick start.
- `mbt/fuzz_props/`, `mbt/gpu_selftest/` — property and fallback checks,
  driven by the mutation and fuzz gates.
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
profile is 8.5× slower — 837 ms against 98.4 ms for the same call — so any
timing taken from `moon test` or `moon run` without `--release` is wrong by
that factor. `verify/run_release_bench.ps1` exists for this, and its header
records why a baseline subtraction is required rather than merely tidy: on
Windows the `time` builtin reports ~0.02 s of user CPU for a run whose wall
clock is 2 s, so only elapsed-minus-baseline is usable.
