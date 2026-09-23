import {
  "riantr/moonbit_image@0.3.4",
}

name = "riantr/snn_mbt"

version = "0.25.3"

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
  "surrogate-gradient",
  "fast-sigmoid",
  "bp-friendly-spike",
  "gelu",
  "transformer-activation",
  "multi-head-attention",
  "self-attention",
  "transformer-block",
  "positional-encoding",
  "sinusoidal",
  "transformer-block",
  "pre-norm",
  "causal-mask",
  "attention-mask",
  "dropout",
  "inverted-dropout",
  "spiking-attention",
  "spikeformer",
  "spiking-transformer",
  "snnt-transformer",
  "sparse-gelu",
  "exact-gelu",
  "adafactor",
  "layer-scale",
  "t5-relative-position",
]

description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl plus CNN primitives + 5D spatiotemporal tensors + full optimiser suite + normalisation layers + learning-rate schedulers + generic Chain + pre-made bottles + ResNet foundations + surrogate gradients + transformer activations. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), multicompartment dendritic neurons (BallAndStick, Tripod, Multipod); Conv2d / MaxPool2d / ReLU / Flatten / Linear forward + backward primitives; Tensor struct with broadcasting + softmax + log-softmax + cross-entropy; image adapter (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF) via riantr/moonbit_image; 5D [T,B,C,H,W] STImage for spiking-CNN time-series input; SGD + SGD-with-momentum + Adam (with bias correction) + AdamW (decoupled weight decay) + RMSprop + AdaGrad optimisers; BatchNorm2d (with running stats + training/inference modes) + LayerNorm (per-sample, no running stats) with forward + backward; StepLR + ExponentialLR + CosineAnnealingLR + ReduceLROnPlateau schedulers; generic Chain / Sequential over Conv2d / ReLU / MaxPool2d / Flatten / Linear / BatchNorm2d / LayerNorm via tagged-enum dispatch with shape tracking and per-layer parameter gradients; pre-made MLP / SimpleCNN / LeNet5 bottles with He-init + xoshiro RNG; elementwise Add + AvgPool2d / GlobalAvgPool2d + ResidualBlock (identity + projection variants) ResNet foundations; fast-sigmoid surrogate gradient (Zenke & Ganguli 2018) for BPTT through IF / LIF Heaviside spikes; GELU (Gaussian Error Linear Unit) tanh-approximation activation for transformer FFN; Multi-Head Self-Attention (Vaswani 2017) with Q/K/V/O projections, scaled dot-product, head split/merge, optional additive mask, and full backward through softmax + linear projections; sinusoidal (fixed) and learnable (Xavier-init) positional encoding with backward for learnable variant; Pre-Norm Transformer block (LN → MHA → + → LN → FFN(GELU) → +) with full BPTT-style backward; attention mask utilities (causal mask + head-broadcast + allowed-keys → additive mask); inverted dropout regularisation (Bernoulli mask with 1/(1-p) scale, training/inference mode toggle); Spiking Self-Attention (SpikeFormer-style) that replaces softmax along key axis with fast-sigmoid surrogate from v0.22.0, with full BPTT-compatible backward using the surrogate gradient; Pre-Norm Spiking Transformer block (LN → SpikingAttention → + → LN → FFN(GELU) → +) demonstrating the full SNN-Transformer training pipeline; Mini-SpikeFormer training demo (synthetic 28×28 dataset + patch embedding + learnable class token + SpikingTransformerBlock + classifier + cross-entropy + per-parameter gradient clipping + SGD); exact (sparse) GELU activation `x · Φ(x)` via libm `erff` for the cumulative normal distribution; Adafactor optimizer (Shazeer 2018) with factorised second-moment estimation for memory-efficient 2D + 1D updates; LayerScale (Touvron 2021) per-channel learnable scale γ (init=1e-4) on residual branches for stable training of deep transformers; T5-style relative position bias (Raffel 2020) added directly to attention scores per (head, offset) with linear-bucket clamping. Float32 end-to-end with libm FFI (expf, tanhf, logf, sqrtf, cosf, sinf, erff). 1167 tests passing, 76 examples ported from Julia."

preferred_target = "native"