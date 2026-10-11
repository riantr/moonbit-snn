# moonbit-snn

Bit-exact MoonBit port of [SpikingNeuralNetworks.jl](https://github.com/AlessioQuer/SpikingNeuralNetworks.jl).

The MoonBit implementation lives in [`mbt/`](./mbt/). See
[`mbt/README.md`](./mbt/README.md) for full status, design notes, and
test history.

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

## Project layout

- `mbt/` — the MoonBit port: all source, plus `mbt/examples/` (77
  runnable examples).
- `mbt/tests/wave1` … `wave9` — the test suite, split by domain so a failure
  points at one. None of it can live in the package root; see Quick start.
- `tools/split_tests/` — the rewriter that qualifies a root test for a
  sub-package, and its README, which records why the split exists and what
  the allow-list must not be derived from.
- `verify/` — the two executable harnesses and the script that runs
  them inside the library package.
- `refs/` — local working copies of the upstream Julia reference
  repos used as the bit-exact specification (`SpikingNeuralNetworks.jl`,
  `SNNModels.jl`, `SNNUtils.jl`). **Not tracked in git.**
- `_build/`, `_disabled/` — build artifacts and discarded work.
  **Not tracked in git.**