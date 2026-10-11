#!/bin/sh
# C* v0.15 regression runner, using the repository's actual examples/ layout.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
SRC=examples
BIN="$ROOT/.cstar-test-bin"
mkdir -p "$BIN"

clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar

run_plain() {
    name=$1
    echo "== $name =="
    ./cstar "$SRC/$name.cppo"
    clang -std=c11 -Wall -Wextra -Wpedantic output.c -o "$BIN/$name"
    "$BIN/$name"
}

for name in \
    comptime_if hello ptr_test sim_test comptime_ast type_system \
    array_enum usize_test memory_types lowlevel atomic; do
    run_plain "$name"
done

echo '== ffi_test =='
./cstar "$SRC/ffi_test.cppo"
clang -std=c11 -Wall -Wextra -Wpedantic output.c "$SRC/ffi_host.c" -o "$BIN/ffi_test"
"$BIN/ffi_test"

echo '== type_alias =='
./cstar "$SRC/type_alias.cppo"
clang -std=c11 -Wall -Wextra -Wpedantic output.c "$SRC/type_alias_host.c" -o "$BIN/type_alias"
"$BIN/type_alias"

echo '== module_v12 =='
./cstar --emit-module "$SRC/math.cppo" "$SRC/math.cso"
./cstar "$SRC/module_v12.cppo"
clang -std=c11 -Wall -Wextra -Wpedantic output.c "$SRC/math.cso" -o "$BIN/module_v12"
"$BIN/module_v12"

expect_reject() {
    name=$1
    expected=$2
    log="$BIN/$name.log"
    echo "== expected rejection: $name =="
    if ./cstar "$SRC/$name.cppo" >"$log" 2>&1; then
        echo "ERROR: $name was unexpectedly accepted"
        exit 1
    fi
    if ! grep -q "$expected" "$log"; then
        echo "ERROR: $name failed for an unexpected reason"
        cat "$log"
        exit 1
    fi
}

expect_reject cast_invalid 'invalid explicit cast'
expect_reject alignof_invalid 'unknown type in sizeof/alignof'
expect_reject abi_invalid 'C ABI argument type mismatch'
expect_reject array_invalid 'indexing requires an array or pointer'
expect_reject lvalue_invalid 'left side of assignment must be assignable'

echo
printf '%s\n' 'C* v0.15 regression suite: PASS'
