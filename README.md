# moonbit-snn

Bit-exact MoonBit port of [SpikingNeuralNetworks.jl](https://github.com/AlessioQuer/SpikingNeuralNetworks.jl).

The MoonBit implementation lives in [`mbt/`](./mbt/). See
[`mbt/README.md`](./mbt/README.md) for full status, design notes, and
test history.

## Quick start

```sh
cd mbt
moon test --target native
```

## Project layout

- `mbt/` — the MoonBit port (all source, tests, and examples).
- `refs/` — local working copies of the upstream Julia reference
  repos used as the bit-exact specification (`SpikingNeuralNetworks.jl`,
  `SNNModels.jl`, `SNNUtils.jl`). **Not tracked in git.**
- `_build/`, `_disabled/` — build artifacts and discarded work.
  **Not tracked in git.**