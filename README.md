# C* v0.15 — Low-level Operators + Atomics

C* continues to follow the original principles:

- 极致的性能
- 优秀的兼容
- 极致的简易

## v0.15 additions

### Pointer arithmetic

```cppo
let p: *u64 = &array[0];
p += 1;
p -= 1;
let q: *u64 = p + 1;
let d: isize = q - p;
```

Pointer arithmetic is type-checked instead of being treated as generic integer arithmetic.

### Increment / decrement

```cppo
++x;
x++;
--x;
x--;
```

Both integer and pointer values are supported and must be assignable.

### Compound assignments

```cppo
x += y;
x -= y;
x &= mask;
x |= mask;
x ^= mask;
x <<= 2;
x >>= 1;
```

Pointer `+=` / `-=` accepts integral offsets.

### Atomic primitives

C* exposes a small low-level atomic surface that lowers directly to Clang/GCC `__atomic_*` builtins:

```cppo
atomic_store(&x, 20u64, relaxed);
let a: u64 = atomic_load(&x, acquire);
let b: u64 = atomic_fetch_add(&x, 5u64, acq_rel);
let c: u64 = atomic_fetch_sub(&x, 2u64, seq_cst);
let d: u64 = atomic_exchange(&x, 7u64);
atomic_fence(seq_cst);
```

Supported memory orders:

```text
relaxed
consume
acquire
release
acq_rel
seq_cst
```

The compiler uses `__ATOMIC_*` constants in the generated C. No C* runtime is introduced.

## Compatibility

The previous C* functionality remains intact:

- Expression AST
- Type system
- Comptime AST evaluation and specialization
- arrays / enums / pointers / structs
- explicit casts / sizeof / alignof
- type aliases
- `extern fn` C ABI
- `.hppo` headers
- `.cso` native object modules
- iOS-safe compiler path using `posix_spawnp()` instead of `system()`

## Design direction

v0.15 focuses on the low-level primitives needed by system software, emulator cores and future SIMD/runtime work, while keeping the compiler AOT and C/Clang based.

The next major low-level step is SIMD/intrinsic support, followed by a more explicit concurrency model.

## Verification

The full regression suite is run with:

```sh
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
./tests/run_v015.sh
```

Current result:

```text
C* v0.15 regression suite: PASS
```
