# C* v0.12 — Header / Object Modules

C* continues to target: 极致的性能，优秀的兼容，极致的简易。

## New

- `.hppo` — C* module interface/header. The compiler can import it and type-check its declarations.
- `.cso` — native compiled object produced by `cstar --emit-module` via `clang -O3 -c`.
- `import "foo.hppo";` — imports the C* interface.
- `import "foo.cso";` — records the native object for final linking.
- `link "foo.cso";` — explicit object-link directive.
- `--emit-module` — emits `<module>.hppo` and `<module>.cso` next to the source by default.
- Repeated imports are deduplicated; cyclic `.cppo`/`.hppo` imports remain rejected.

## Build a module

```sh
clang -std=c11 -Wall -Wextra -Wpedantic cstar.c -o cstar
./cstar --emit-module tests/math.cppo
```

This creates:

```text
tests/math.hppo
tests/math.cso
```

## Consume a module

```sh
./cstar tests/module_v12.cppo
clang -std=c11 -Wall -Wextra -Wpedantic -O3 output.c tests/math.cso -o module-test
./module-test
```

Expected output:

```text
module v12: ok
```

`.cso` is a real native relocatable object for the current host/target clang toolchain, while `.hppo` carries the C* interface used during source compilation.
