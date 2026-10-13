# Local validation — 2026-10-13

Environment: Apple Silicon macOS, Xcode 27.0, Apple Clang 21.0, C++17. Validation used deterministic synthetic PCM and generated WAV fixtures. It did not use personal recordings or commercial music.

| Check | Result |
|---|---|
| Apple Accelerate numerical tests | 444 assertions passed |
| Portable FFT numerical tests | 444 assertions passed |
| Portable tests with AddressSanitizer + UndefinedBehaviorSanitizer | 444 assertions passed, no reported sanitizer errors |
| PCM16 WAV / CLI tests | 13 cases passed with each backend; portable CLI also passed with both sanitizers |
| Strict compilation | Both backends built with `-Wall -Wextra -Werror -pedantic` |
| Original → name-only copy | 900 frames agreed within `1e-6`; see extraction record |
| Final Apple → portable report | 375 frames; differences below the tolerances below |
| Browser interaction | First/last frame, all descriptor tabs, report upload, rejected schema, demo reset confirmed |
| Layout | 1280 px and 390 px browser widths; no horizontal overflow |

Numerical tests include independent double-precision direct DFTs at N=32/64/256, zero-padded partial frames, known tone amplitude/RMS, DC/Nyquist separation, power bands, chroma pitch-class mapping, silence, temporal flux/reset, invalid construction/input, and C++ `operator new` instrumentation around process/reset. Allocation instrumentation does not cover `malloc` inside system frameworks.

WAV tests include mono, stereo, antiphase cancellation during downmix, partial frames, unknown odd-sized chunks, truncated chunks/headers, inconsistent RIFF length/format, unsupported encoding, empty data, usage errors, and missing input. The fixture generator is in `tests/cli_test.py`.

## Final backend comparison

The deterministic eight-second study was analyzed with N=2048, sample rate=48000, hop=1024. Maximum absolute differences over 375 frames, measured from JSON serialized with seven significant digits:

| Field | Maximum difference |
|---|---:|
| RMS / peak | 0 |
| Centroid | 0.0029 Hz |
| Rolloff | 0 Hz |
| Entropy | 3.0e-7 |
| Flux | 2.0e-9 |
| Band ratios | 1.0e-7 |
| Chroma bins | 5.0e-7 |
| Spectrum bins | 2.0e-7 |

Preselected comparison limits were 0.02 Hz for centroid, exact rolloff-bin equality, and 2e-5 for the other serialized descriptors. These are numerical agreement checks on this population, not a claim of accuracy on every recording.

## Reproduce

```sh
./build.sh
./build.sh --portable
# Optional sanitizer run on a compiler with sanitizer support:
c++ -std=c++17 -O1 -g -DOTOFRAME_PORTABLE=1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I include \
  tests/features_test.cpp -o build/features-sanitized
build/features-sanitized
```

The example's two generated `build/demo.json` files can be compared after saving one between backend builds. CMake/CTest is provided but was not executed locally because CMake is not installed. The remote CI workflow has not run; Windows, Linux, and physical iOS/macOS audio-device behavior remain unverified.

The viewer was tested through a loopback-only HTTP server. Its generated HTML embeds data, scripts, and styles and contains no runtime network requests. Direct `file://` navigation could not be tested because the in-app browser blocks that protocol. The delivered HTML can be opened manually in a normal browser; that exact opening path has not been verified in this session.

No speedup, processing deadline, recording-quality score, key-detection accuracy, adoption, or real-world audio performance is claimed.
