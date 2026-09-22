name = "riantr/snn_mbt"

version = "0.11.1"

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
  "conv2d",
  "cnn",
  "maxpool",
]

description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl plus CNN primitives. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), multicompartment dendritic neurons (BallAndStick, Tripod, Multipod); and Conv2d / MaxPool2d forward-pass primitives for spiking-CNN composition. Float32 end-to-end with libm FFI (expf, tanhf, logf). 912 tests passing, 76 examples ported from Julia."

preferred_target = "native"