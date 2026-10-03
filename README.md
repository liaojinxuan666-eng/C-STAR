# C* v0.5 — Compiler Foundation

C* continues to follow its core principles:

- 极致的性能
- 优秀的兼容
- 极致的简易

v0.5 moves the compiler internals toward a real frontend/backend boundary without changing the existing C* surface syntax.

## Internal pipeline

```text
.cppo
  ↓
Lexer
  ↓
Parser
  ↓
Program / Statement AST
  ↓
Comptime specialization
  ↓
C backend
  ↓
Clang -O3
  ↓
Native code
```

Runtime function bodies now use a small AST for `let`, `return`, expression statements, `print`, `if`, `else`, `while`, and nested blocks.

The proven token-based Comptime specialization path is retained in v0.5 so the compiler can be refactored without changing the language's core behavior.
