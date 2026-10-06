# C* v0.13 — Type Aliases + Stronger Native C ABI

Core principles remain:

- 极致的性能
- 优秀的兼容
- 极致的简易

## v0.13

- `type Name = Type;` type aliases
- pointer aliases such as `type WordPtr = *Word`
- aliases participate in type checking
- C backend emits native `typedef`
- function parameters/returns are lowered from the parsed C* type instead of token order
- pointer parameters and pointer returns now lower to valid C declarators
- `usize` / `isize` map to native `size_t` / `ptrdiff_t`
- extern C functions use stricter ABI argument compatibility checks
- iOS compiler path remains free of `system()`; module object generation uses `posix_spawnp`

The public syntax stays intentionally small.
