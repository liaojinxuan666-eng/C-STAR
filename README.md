# C* v0.10 — Arrays, Enums, Indexing

C* v0.10 extends the v0.9 AST + type system + C ABI pipeline with fixed-size arrays, enum types/constants, and indexed expressions.

## New

- Fixed-size array types: `int[4]`, `u64[16]`, etc.
- Array indexing: `a[i]`.
- Indexing of pointers: `ptr[i]`.
- Array members inside structs: `buffer.values[2]`.
- `enum` declarations and named enum types.
- Enum constants participate in expression/type checking.
- Assignment lvalue checking for identifiers, members, indexes, and dereferences.
- Zero-initialization for fixed arrays: `let a: int[4] = 0;` emits C `{0}` initialization.

## Example

```cppo
enum Mode {
    MODE_IDLE,
    MODE_RUN = 4,
    MODE_PAUSE
};

struct Buffer {
    int values[4];
};

fn main() -> int {
    let mode: Mode = MODE_RUN;
    let a: int[4] = 0;
    a[0] = 3;
    a[1] = 7;
    a[2] = a[0] + a[1];

    let p: int* = &a[2];
    *p += 5;

    let b: Buffer = Buffer();
    b.values[0] = a[2];
    return b.values[0];
}
```

## Pipeline

`.cppo → Lexer → AST → Typecheck → AST Comptime → C backend → Clang -O3 → native code`

Array literals and multi-dimensional declarator corner cases are intentionally left for a later version; v0.10 focuses on fixed-size storage, indexing, and enum semantics without changing the existing public syntax model.
