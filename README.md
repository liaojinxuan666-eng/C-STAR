# C* v0.14 — Cast + sizeof/alignof + Integer Literal Semantics

C* continues to follow the original principles:

- 极致的性能
- 优秀的兼容
- 极致的简易

## v0.14 additions

### Explicit casts

```cppo
let x: u64 = cast(u64, value);
let p: *u8 = cast(*u8, 0);
```

`cast(Type, expression)` supports explicit integer/integer and pointer/pointer
conversions, plus integer/pointer conversions. Struct and array casts are not
accepted because the C backend cannot represent them as scalar casts safely.

### Size and alignment

```cppo
let size: usize = sizeof(MyStruct);
let bytes: usize = sizeof(u8[64]);
let align: usize = alignof(MyStruct);
```

`sizeof(Type)`, `sizeof(expression)` and `alignof(Type)` lower directly to C
`sizeof` / `_Alignof`. Their C* result type is `usize`.

### Integer literals

Explicit literal suffixes are supported:

```text
u8 u16 u32 u64
 i8  i16 i32 i64
usize isize
```

Example:

```cppo
let a: u8 = 7u8;
let b: u64 = 42u64;
let c: i64 = 7i64;
```

The frontend retains the literal's declared integer width/sign so later type
checking can use the intended ABI type instead of treating every literal as
plain `int`.

## Compatibility

Existing C* v0.13 functionality remains intact:

- Expression AST
- Type system
- Comptime AST evaluation and specialization
- arrays / enums / pointers / structs
- `extern fn` C ABI
- `.hppo` headers
- `.cso` native object modules
- iOS-safe compiler path using `posix_spawnp()` instead of `system()`

## Verification

The v0.14 regression suite covers all previous tests plus memory/type tests.
The compiler itself is checked with:

```sh
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
```

Generated C is also checked with the same warning level.
