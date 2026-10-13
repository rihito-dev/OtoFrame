# Numerical contract

The core accepts mono normalized Float32 PCM. Frame size `N` must be a power of two from 32 to 32768; sample rate must be finite and in 8000–384000 Hz. Each call supplies `L` samples, where `1 <= L <= N`. Short frames are padded with zeros for the transform; their RMS and peak use only the `L` valid input samples. Full frames are recommended for streaming comparisons.

## Spectrum

Periodic Hamming window: `w[n] = 0.54 - 0.46 cos(2πn/N)`. Let `S = sum(w)`, and `X[k]` be the mathematical DFT of the windowed, zero-padded frame. One-sided amplitudes are:

```text
A[0]   = |X[0]| / S
A[N/2] = |X[N/2]| / S
A[k]   = 2 |X[k]| / S       for 0 < k < N/2
f[k]   = k * sample_rate / N
```

This corrects the coherent window gain for a bin-centered tone. Off-bin tones spread energy across bins; the tallest bin is not necessarily their full amplitude. A short final frame also has a different effective window gain because `S` remains the full-window sum. There are `N/2 + 1` bins, including DC and Nyquist.

Apple's real forward FFT returns `2*X`; it packs Nyquist into `imag[0]`. Both properties must be accounted for. See [Apple's packing and scaling reference](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html). The portable backend is an iterative radix-2 complex FFT with precomputed twiddles and bit-reversal indices.

## Scalar descriptors

```text
rms = sqrt(sum(x[n]^2) / L)
peak = max(abs(x[n]))
centroid_hz = sum(f[k] * A[k]) / sum(A[k])
rolloff_hz = first f[k] where cumulative A reaches 0.85 * sum(A)
p[k] = A[k] / sum(A)
entropy = -sum(p[k] * log2(p[k])) / log2(N/2 + 1)
flux = sqrt(sum((A[k] - previous_A[k])^2) / (N/2 + 1))
```

Rolloff uses magnitude, not power. Entropy also uses magnitude. These choices descend from the extracted implementation and are part of this API's contract. Spectral centroid follows the magnitude-weighted definition described in [Essentia's Centroid reference](https://essentia.upf.edu/reference/std_Centroid.html).

The first valid call, and the first call after `reset()`, set `has_previous_frame=false` and `spectral_flux=0`. Zero represents an unavailable comparison in that case. Subsequent flux depends on the caller's hop and is not an onset probability. After a silent frame, a tonal frame can have positive flux; repeated identical frames have exactly zero flux on the same instance.

## Bands and chroma

For power accounting, use `P[k] = A[k]^2` at DC/Nyquist and `P[k] = A[k]^2 / 2` in the interior. This accounts for the amplitude doubling of the one-sided spectrum. Each band's power sum is divided by the sum of `P` across all bins.

Bands are `[20,250)`, `[250,2000)`, and `[2000,20000]` Hz. They do not overlap. Values can sum to less than one because DC, very low frequencies, and frequencies above 20 kHz remain in the denominator. Their values describe the **windowed frame**. They are not filtered time-domain RMS, acoustic SPL, or a calibration in pascals.

Chroma maps bins from 27.5 through 4186 Hz to `round(69 + 12*log2(f/440)) mod 12`, sums squared amplitudes, then divides the 12-vector by its largest value. C is index zero. It uses nearest pitch classes without tuning estimation, harmonic weighting, or constant-Q analysis. The FFT resolution is `sample_rate/N`; low notes can map to a neighboring class. Do not infer musical key from this vector alone.

## Silence and invalid input

If total magnitude is at most `1e-12`, centroid, rolloff, and entropy return zero. Band normalization uses a power floor of `1e-24`; chroma uses a maximum-bin power floor of `1e-24`. A silent frame following sound can still have positive flux because sound ended. Extremely tiny signals below these floors are intentionally treated as absent.

Null pointers, zero/oversized lengths, nonfinite samples, or samples outside `[-1,1]` return `false`, zero the output struct, and preserve the previous valid stream state. Invalid configuration throws at construction before allocating FFT storage. Each instance owns mutable FFT and history buffers and must be called from a single thread. Spectrum references are invalidated in content by the next valid process/reset call.
