# C* v0.9 — C ABI / Native Interop

C* v0.9 keeps the v0.8 AST + Comptime pipeline and adds a minimal, explicit C ABI boundary.

## New

- `extern fn` declarations for native C functions.
- C-compatible prototypes are emitted before C* function definitions.
- Parameter and return types use the existing C* type system.
- External functions can return `void` and accept zero parameters.
- External calls participate in existing argument-count and type checking.
- External symbol names are emitted unchanged.

Example:

```cppo
include <stdint.h>

extern fn host_add(a: int, b: int) -> int;
extern fn host_ping() -> void;

fn main() -> int {
    let x: int = host_add(20, 22);
    host_ping();
    return x;
}
```

The generated C can be linked directly with an ordinary C object/source file:

```sh
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
./cstar tests/ffi_test.cppo
clang -std=c11 -Wall -Wextra -Wpedantic output.c tests/ffi_host.c -o ffi-test
./ffi-test
```

## Pipeline

`.cppo → AST → Typecheck → Comptime → C backend → Clang -O3 → native code`

v0.9 does not add a VM, GC, or hidden runtime. The ABI boundary is explicit and remains compatible with the generated C layer.
