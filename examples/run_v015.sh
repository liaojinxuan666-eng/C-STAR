#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
for t in comptime_if hello ptr_test sim_test comptime_ast type_system array_enum usize_test memory_types lowlevel atomic; do
    ./cstar "tests/$t.cppo"
    clang -std=c11 -Wall -Wextra -Wpedantic output.c -o "/tmp/cstar15-$t"
    "/tmp/cstar15-$t"
done
./cstar tests/ffi_test.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/ffi_host.c -o /tmp/cstar15-ffi
/tmp/cstar15-ffi
./cstar tests/type_alias.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/type_alias_host.c -o /tmp/cstar15-alias
/tmp/cstar15-alias
./cstar --emit-module tests/math.cppo tests/math.cso
./cstar tests/module_v12.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/math.cso -o /tmp/cstar15-module
/tmp/cstar15-module
if ./cstar tests/cast_invalid.cppo >/tmp/cstar15-cast-invalid.log 2>&1; then exit 1; fi
grep -q "invalid explicit cast" /tmp/cstar15-cast-invalid.log
if ./cstar tests/alignof_invalid.cppo >/tmp/cstar15-alignof-invalid.log 2>&1; then exit 1; fi
grep -q "unknown type in sizeof/alignof" /tmp/cstar15-alignof-invalid.log
echo "C* v0.15 regression suite: PASS"
