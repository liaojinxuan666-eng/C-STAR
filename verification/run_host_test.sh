#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
clang -std=c11 -O3 -Wall -Wextra -Wpedantic \
  -I"$ROOT/include" \
  "$ROOT/verification/host_test.c" "$ROOT/runtime/cstar_simd128.c" \
  -o "$ROOT/verification/host_test"
"$ROOT/verification/host_test"
rm -f "$ROOT/verification/host_test"
