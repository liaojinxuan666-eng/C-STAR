#!/bin/sh
set -e
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"
clang src/cstar.c -o cstar
for f in tests/*.cppo; do
    echo "== $f =="
    ./cstar "$f"
    clang output.c -o cstar_test
    ./cstar_test
    echo
 done
rm -f output.c cstar cstar_test
