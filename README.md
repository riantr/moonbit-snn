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

Thirteen gates: formatting, static analysis, dependency shape, duplicate-code
detection, a secret scan, the unit suite, the example suite, property checks,
a performance baseline, coverage, a repeat-run stability check, and mutation
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
