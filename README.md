# C* v0.11 — Module + Native Header Interop

C* continues to follow the same goals: extreme performance, excellent compatibility, and extreme simplicity.

## Added

- `module name;` top-level module declaration
- `import "other.cppo";` for source-level C* modules
- `import <header.h>;` for native C headers
- quoted non-`.cppo` imports become C header includes
- nested C* imports
- duplicate C* imports are ignored
- cyclic C* imports are rejected
- imported files are resolved relative to the importing `.cppo`
- existing `include <...>` syntax remains supported
- ordinary zero-argument function calls in `let` initializers are no longer mistaken for struct constructors

## Example

```cppo
module emulator.cpu;
import "alu.cppo";
import <stdint.h>;

fn main() -> int {
    let x: int = add(20, 22);
    return x;
}
```

The compiler flattens C* module imports into one compilation unit, then sends the normal C backend output to Clang.

## Tests

New tests:

- `tests/module_main.cppo`
- `tests/module_nested.cppo`
- `tests/import_header.cppo`
- `tests/math.cppo`
- `tests/import_host.c`

All v0.10 and earlier regression tests were also rerun successfully with `-Wall -Wextra -Wpedantic` on the compiler and generated C.
