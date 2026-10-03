# C* v0.6 — Expression AST

本版本继续保持 C* v0.5 已通过的 Comptime 路径不变，重点把运行时代码的表达式从“原始 token 直接输出 C”升级为真正的表达式 AST。

## 新增

- 基本字面量：整数、字符串、标识符
- 一元运算：`& * + - ! ~`
- 二元运算与优先级：`= += -= *= /= %= || && | ^ & == != < > <= >= << >> + - * / %`
- 函数调用：`foo(a, b)`
- 成员访问：`cpu.x0`、`cpu->x0`
- `let`、`return`、`if`、`while`、普通表达式语句全部走 AST
- 保留 `let cpu = CPU()` 的特殊构造行为
- 保留 v0.5 Comptime 专化生成路径

## 目标

继续保持 C* 的三个原则：

> 极致的性能，优秀的兼容，极致的简易

编译链仍然是：

`.cppo → C* → generated C → clang -O3 → native`

## 回归测试

原 v0.5 四个测试应继续通过：

- `comptime_if.cppo`
- `hello.cppo`
- `ptr_test.cppo`
- `sim_test.cppo`

新增：

- `expr_ast.cppo`
- `expr_logic.cppo`
