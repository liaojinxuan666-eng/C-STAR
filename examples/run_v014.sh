#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
for t in comptime_if hello ptr_test sim_test comptime_ast type_system array_enum usize_test memory_types; do
    ./cstar "tests/$t.cppo"
    clang -std=c11 -Wall -Wextra -Wpedantic output.c -o "/tmp/cstar14-$t"
    "/tmp/cstar14-$t"
done
./cstar tests/ffi_test.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/ffi_host.c -o /tmp/cstar14-ffi
/tmp/cstar14-ffi
./cstar tests/type_alias.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/type_alias_host.c -o /tmp/cstar14-alias
/tmp/cstar14-alias
./cstar --emit-module tests/math.cppo tests/math.cso
./cstar tests/module_v12.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/math.cso -o /tmp/cstar14-module
/tmp/cstar14-module
if ./cstar tests/cast_invalid.cppo >/tmp/cstar14-cast-invalid.log 2>&1; then
    echo "ERROR: cast_invalid.cppo unexpectedly compiled" >&2
    cat /tmp/cstar14-cast-invalid.log >&2
    exit 1
fi
grep -q "invalid explicit cast" /tmp/cstar14-cast-invalid.log
if ./cstar tests/alignof_invalid.cppo >/tmp/cstar14-alignof-invalid.log 2>&1; then
    echo "ERROR: alignof_invalid.cppo unexpectedly compiled" >&2
    cat /tmp/cstar14-alignof-invalid.log >&2
    exit 1
fi
grep -q "unknown type in sizeof/alignof" /tmp/cstar14-alignof-invalid.log
echo "C* v0.14 regression suite: PASS"
