#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
for t in comptime_if hello ptr_test sim_test comptime_ast type_system array_enum usize_test; do
    ./cstar "tests/$t.cppo"
    clang -std=c11 -Wall -Wextra -Wpedantic output.c -o "/tmp/cstar-$t"
    "/tmp/cstar-$t"
done
./cstar tests/ffi_test.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/ffi_host.c -o /tmp/cstar-ffi
/tmp/cstar-ffi
./cstar tests/type_alias.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/type_alias_host.c -o /tmp/cstar-alias
/tmp/cstar-alias
