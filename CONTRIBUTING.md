# Contributing

Keep the API small and the numerical contract explicit. Before changing a descriptor, document its units, window, normalization, silence behavior, and history requirements. Add a test with an independent expected result, such as an analytical signal or direct DFT; comparing two calls to the same implementation is insufficient.

On macOS or Linux, run `./build.sh` and `./build.sh --portable`. CMake/CTest provides the Windows build route. The GitHub workflow checks macOS, Linux, and Windows after publication; it has not run for this local preview.

Use only synthetic fixtures or recordings with explicit redistribution permission. Do not include personal recordings, music assets, private app code, user data, or secrets. Report numerical discrepancies with compiler version, FFT backend, sample rate, frame/hop sizes, and a minimal synthetic input.
