#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-c++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -I include)
if [[ "${1:-}" == "--portable" || "$(uname -s)" != "Darwin" ]]; then
    flags+=(-DOTOFRAME_PORTABLE=1)
else
    flags+=(-framework Accelerate)
fi
mkdir -p build
"$CXX" "${flags[@]}" tests/features_test.cpp -o build/features-tests
"$CXX" "${flags[@]}" examples/analyze.cpp -o build/otoframe
build/features-tests
python3 tests/cli_test.py build/otoframe
build/otoframe --demo build/demo.json
python3 examples/build_demo.py build/demo.json
