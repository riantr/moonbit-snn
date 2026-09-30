import os, time, sys

path = r'D:\src\MiniMax\Projects\MoonBit\moonbit-snn\mbt\moon.mod'

new_version = 'version = "0.14.1"'
new_desc = (
    'description = "Bit-exact MoonBit port of SpikingNeuralNetworks.jl plus CNN primitives '
    '+ 5D spatiotemporal tensors. Covers IF, AdEx, Izhikevich (IZ), HH, Morris-Lecar, '
    'Poisson neurons; Markram STP, Gerstner / MexicanHat / AntiSymmetric STDP, vSTDP, '
    'Confavreux2025, Receptors (AMPA / NMDA / GABAa / GABAb), multicompartment dendritic '
    'neurons (BallAndStick, Tripod, Multipod); Conv2d / MaxPool2d / ReLU / Flatten / Linear '
    'forward + backward primitives; Tensor struct with broadcasting + softmax + log-softmax '
    '+ cross-entropy; image adapter (BMP/QOI/TGA/PNG/GIF/JPEG/ICO/TIFF) via '
    'riantr/moonbit_image; 5D [T,B,C,H,W] STImage for spiking-CNN time-series input. '
    'Float32 end-to-end with libm FFI (expf, tanhf, logf). 971 tests passing, 76 examples '
    'ported from Julia."'
)

for attempt in range(10):
    try:
        with open(path, 'r', encoding='utf-8') as f:
            txt = f.read()
        if new_version in txt:
            print('already updated')
            sys.exit(0)
        # Replace version line.
        txt = txt.replace('version = "0.13.2"', new_version)
        # Replace description (locate first line starting with 'description = "').
        lines = txt.split('\n')
        out_lines = []
        replaced = False
        for ln in lines:
            if not replaced and ln.startswith('description = "'):
                out_lines.append(new_desc)
                replaced = True
            else:
                out_lines.append(ln)
        txt = '\n'.join(out_lines)
        # Add 'video' keyword before the closing ']'.
        txt = txt.replace('  "tensor",\n]', '  "tensor",\n  "video",\n]')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(txt)
        print('updated, attempt', attempt + 1)
        sys.exit(0)
    except OSError as e:
        print('retry:', e, file=sys.stderr)
        time.sleep(0.3)
print('failed after retries')
sys.exit(1)