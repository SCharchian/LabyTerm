#!/usr/bin/env bash
# Build with AddressSanitizer + UBSan in build-san/ and run ctest:  tools/sanitize.sh [ctest args]
# Separate build dir, so the normal build/ is untouched. Reports land in build-san/ctest.log.
set -euo pipefail
cd "$(dirname "$0")/.."
for d in "$HOME"/.local/share/mise/installs/cmake/latest/*/bin "$HOME"/.local/share/mise/installs/ninja/latest; do
  [[ -d "$d" ]] && PATH="$d:$PATH"
done
export PATH
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
[[ -f build-san/CMakeCache.txt ]] || cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="$SAN" -DCMAKE_C_FLAGS="$SAN" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1
ctest --test-dir build-san --output-on-failure "$@" 2>&1 | tee build-san/ctest.log
