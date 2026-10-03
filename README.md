# C* v0.7 — Type System Foundation

v0.7 在 v0.6 Expression AST 基础上加入最小类型系统与静态检查，同时保持原有 C backend 和 Comptime 专化路径。

## 支持

- 基础类型：`int`, `i8/i16/i32/i64`, `u8/u16/u32/u64`, `bool`, `void`
- 结构体类型：`struct Name { ... }`
- 指针类型：`T*`，包括 `Pair*`
- `let name: Type = expr` 显式类型
- `let name = expr` 类型推导
- `let value = Type()` 结构体零初始化构造
- 函数参数类型检查
- 函数返回值类型检查
- 算术、比较、逻辑、位运算的基本类型检查
- `&` / `*`、`.` / `->` 的基本类型检查
- 基本赋值兼容性检查

## 保持不变

v0.6 的 Expression AST、函数调用、成员访问、Comptime for / comptime if 均保留。

编译链仍然是：

`.cppo → C* → generated C → clang -O3 → native`

## 新测试

`tests/type_system.cppo` 应输出：

```text
types: ok 10
```

## 编译器

```sh
clang src/cstar.c -o cstar
./cstar tests/type_system.cppo
clang -O3 output.c -o type-test
./type-test
```
