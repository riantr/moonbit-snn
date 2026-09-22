import {
  "riantr/moonbit_image@0.3.4",
}

name = "riantr/snn_mbt"

version = "0.14.1"

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
  "image",
  "tensor",
  "video",
]

description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl plus CNN primitives + 5D spatiotemporal tensors. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), multicompartment dendritic neurons (BallAndStick, Tripod, Multipod); Conv2d / MaxPool2d / ReLU / Flatten / Linear forward + backward primitives; Tensor struct with broadcasting + softmax + log-softmax + cross-entropy; image adapter (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF) via riantr/moonbit_image; 5D [T,B,C,H,W] STImage for spiking-CNN time-series input. Float32 end-to-end with libm FFI (expf, tanhf, logf). 971 tests passing, 76 examples ported from Julia."

preferred_target = "native"