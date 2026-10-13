# Extraction decisions

The starting point was a 233-line feature extractor from a private Apple audio application. Only the generic PCM/spectral analysis responsibilities were selected. Application UI, music-library access, billing, collected user data, emotion scoring, and haptic pattern/classifier logic are outside this extraction.

## Two distinct validation stages

1. A local copy with only namespace/include-guard changes was compared with the original on **900 frames**, across frame sizes 256/512/2048 and sample rates 44100/48000/96000. All feature and spectrum values agreed within absolute tolerance `1e-6`. Maximum feature error was `4.76837e-7`; maximum spectral-bin error was `1.86265e-8`. FFT buffer alignment can produce last-bit differences.
2. The public-preview API then changed definitions and added input validation. It is **not numerically compatible with the original**. Its final outputs are checked against analytical signals, an independent direct DFT, and a second FFT backend. Stage-one equivalence does not establish stage-two equality.

Original source and private paths are retained only in the local audit area, outside this public-preview directory.

## Retained ideas and intentional changes

| Starting responsibility | Preview decision |
|---|---|
| Windowed FFT | Keep framing/Hamming analysis; correct coherent gain and separate DC/Nyquist |
| Brightness scaled/clamped to 0–1 | Expose magnitude-weighted centroid in Hz |
| 85% magnitude rolloff scaled/clamped | Retain magnitude definition, expose Hz |
| Normalized magnitude entropy | Retain definition; include separate Nyquist bin |
| Scaled/clamped full spectral difference | Expose unclamped RMS spectral difference; mark first frame unavailable |
| Three clamped band activity proxies | Replace with defined nonoverlapping band power fractions |
| Twelve pitch-class bins | Retain nearest-bin/max-normalized chroma concept; document its resolution limits |
| Three log-band averages named MFCC | Omit: no Mel filterbank and no cepstral transform |
| Autocorrelation ratio named HNR | Omit: no harmonic-to-noise ratio in dB is computed |
| Emotion/pitch-confidence/inharmonicity proxies | Omit from the reusable feature API |

Measured corrections on 2048 samples at 48 kHz:

- A bin-centered tone of amplitude `0.5` had original spectral-bin value `0.54`. The preview returns `0.5` after window/FFT scaling correction.
- An alternating `±0.25` Nyquist signal was packed into the original DC magnitude slot (`0.54`). The preview reports DC `0` and Nyquist amplitude `0.25`. The Hamming window also produces neighboring-bin energy, so its magnitude centroid is slightly below 24 kHz.

Copy and move operations are disabled to prevent FFT/history state from being accidentally shared. Inputs are validated before FFT or history state changes. A portable backend makes numerical comparison possible without adding third-party DSP code.

This extraction does not modify the original application or establish that the original application's downstream thresholds should be changed. Integrating corrected descriptors back into that application would be a separate behavior change requiring its own validation.
