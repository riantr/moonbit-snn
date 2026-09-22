import {
  "riantr/moonbit_image@0.3.4",
}

name = "riantr/snn_mbt"

version = "0.20.2"

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
  "sgd",
  "adam",
  "adamw",
  "rmsprop",
  "adagrad",
  "optimizer",
  "batch-norm",
  "layer-norm",
  "normalization",
  "lr-scheduler",
  "cosine-annealing",
  "reduce-on-plateau",
  "chain",
  "sequential",
  "mlp",
  "cnn",
  "lenet",
  "add",
  "avgpool",
  "global-avgpool",
  "residual",
  "resnet",
]

description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl plus CNN primitives + 5D spatiotemporal tensors + full optimiser suite + normalisation layers + learning-rate schedulers + generic Chain + pre-made bottles. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), multicompartment dendritic neurons (BallAndStick, Tripod, Multipod); Conv2d / MaxPool2d / ReLU / Flatten / Linear forward + backward primitives; Tensor struct with broadcasting + softmax + log-softmax + cross-entropy; image adapter (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF) via riantr/moonbit_image; 5D [T,B,C,H,W] STImage for spiking-CNN time-series input; SGD + SGD-with-momentum + Adam (with bias correction) + AdamW (decoupled weight decay) + RMSprop + AdaGrad optimisers; BatchNorm2d (with running stats + training/inference modes) + LayerNorm (per-sample, no running stats) with forward + backward; StepLR + ExponentialLR + CosineAnnealingLR + ReduceLROnPlateau schedulers; generic Chain / Sequential over Conv2d / ReLU / MaxPool2d / Flatten / Linear / BatchNorm2d / LayerNorm via tagged-enum dispatch with shape tracking and per-layer parameter gradients; pre-made MLP / SimpleCNN / LeNet5 bottles with He-init + xoshiro RNG. Float32 end-to-end with libm FFI (expf, tanhf, logf, sqrtf, cosf, sinf). 1053 tests passing, 76 examples ported from Julia."

preferred_target = "native"