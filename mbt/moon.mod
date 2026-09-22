name = "riantr/snn_mbt"

version = "0.10.131"

readme = "README.md"

repository = "https://gitee.com/ren-yongxiang/moonbit-snn"

license = "MIT"

keywords = [
  "spiking-neural-network",
  "neuroscience",
  "bit-exact",
  "izhikevich",
  "if-neuron",
  "adex",
  "hodgkin-huxley",
  "morris-lecar",
  "stdp",
  "stp",
  "poisson",
  "dendritic",
  "snns",
  "julia-port",
  "simulation",
]

description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), and multicompartment dendritic neurons (BallAndStick, Tripod, Multipod). Float32 end-to-end with libm FFI (expf, tanhf, logf). 881 tests passing, 76 examples ported from Julia."

preferred_target = "native"