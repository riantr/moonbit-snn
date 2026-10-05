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

`moon test` does **not** currently work in this package and the command is
not a valid entry point: there are 511 `.mbt` files, Moon inlines every
source path into the Windows command line, and that caps at 32K
(CreateProcessW). Use `moon check` for the type check, and the two
harnesses below for anything that needs to actually execute:

```powershell
& ".\verify\verify_gnn_grads.ps1"      # gradient gate
& ".\verify\run_gnn_train_demo.ps1"     # end-to-end training demo
```

Both temporarily flip `"is-main": true` into `mbt/moon.pkg` and restore
it afterwards, because a freshly created sub-package cannot resolve any
symbol from the parent module. See
[`mbt/README.md`](./mbt/README.md#verification-harness) for the criteria.

## Project layout

- `mbt/` — the MoonBit port (all source, tests, and examples).
- `verify/` — the two executable harnesses and the script that runs
  them inside the library package.
- `refs/` — local working copies of the upstream Julia reference
  repos used as the bit-exact specification (`SpikingNeuralNetworks.jl`,
  `SNNModels.jl`, `SNNUtils.jl`). **Not tracked in git.**
- `_build/`, `_disabled/` — build artifacts and discarded work.
  **Not tracked in git.**