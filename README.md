# C* v0.8 — AST Comptime

核心目标：让 Comptime 条件与运行时代码共用 Expression AST，并用 AST evaluator 在编译期求值。

## 本版变化

- Comptime `if` 条件改为复用 Expression AST。
- 新增 AST 整数求值：`+ - * / % << >> & | ^`。
- 支持编译期比较与逻辑：`== != < > <= >= && ||`。
- 支持编译期一元运算：`+ - ! ~`。
- 编译期除零/未知标识符会直接报错。
- 类型检查可以识别由 Comptime 特化生成的函数，并检查参数数量/类型。
- 推导类型的 `let` 不再依赖 GCC `__auto_type`，生成标准 C 类型。
- 生成 C 时避免 `main()` 的 strict-prototypes 警告。
- 专化函数使用可移植的 `CSTAR_UNUSED` 宏，未调用的特化函数不会制造 clang unused-function 警告。

## 测试

已通过 clang `-std=c11 -Wall -Wextra -pedantic` 编译 C* 编译器本身。

回归测试：

- `comptime_if.cppo` → `op_0: 0`, `op_1: 11`, `op_2: 20`, `op_3: 13`
- `hello.cppo` → `2 + 3 = 5`
- `ptr_test.cppo` → `op_0: 10`, `op_1: 11`, `op_3: 13`
- `sim_test.cppo` → `other step`, `step one`, `other step`
- `type_system.cppo` → `types: ok 10`
- `comptime_ast.cppo` → `comptime ast: 2296`

输出 C 使用 clang `-std=gnu11 -Wall -Wextra -Wpedantic -O3` 编译，以上测试无 warning、无 error。

## 使用

把 `src/cstar.c` 替换到你的 C* 工程，然后：

```sh
cd /var/mobile/Documents/cstar-test
clang src/cstar.c -o cstar
./cstar tests/comptime_ast.cppo
clang -O3 output.c -o comptime-test
./comptime-test
```

期望：

```text
[C* Compiler v0.8] compiled successfully, output.c generated
comptime ast: 2296
```
