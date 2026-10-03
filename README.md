C* (C-Star)极致的性能，优秀的兼容，极致的简易。 
C* 是一门专为系统级编程与游戏机模拟器设计的开源编程语言。它的编译器甚至可以在你的 iPhone（越狱环境下）运行，在移动端完成从源码到原生 ARM64 机器码的全链路编译。
 
🚀 项目愿景
 
传统移动端模拟器开发受限于 iOS 沙盒的 W^X（可写与可执行内存分离）机制，无法在运行时动态生成并执行机器码（JIT），导致模拟器性能受限。
 
C* 语言通过 “编译期穷举（Comptime）” 与 “模板 AOT” 技术，巧妙绕开了这一限制。我们直接生成经过极度优化的 C 代码，再借助 clang -O3 编译出纯粹的 ARM64 原生机器码，在零运行时开销、零 JIT 权限的前提下，实现足以比肩 JIT 的恐怖性能。
 
✨ 核心特性
• 极致性能：没有虚拟机，没有 GC（垃圾回收），没有异常处理，没有任何隐藏的运行时开销。所有代码最终都以原生机器码执行，拥有与手写 C 语言同级别的执行效率，配合编译期穷举甚至能实现超越普通 C 语言项目的表现。
• 优秀兼容：完全兼容 C ABI。你的 C* 程序可以无缝导入 iOS 底层的 mach.h、sys/mman.h 等系统头文件，也能轻易打包为 .a 静态库，供 Swift 或 Objective-C 调用。
• 极致简易：拥有类似 Python/Go 的简洁语法。全局自动类型推导（let），字符串插值（print），无头文件文件地狱，无复杂的宏系统。
• 后缀编译期穷举 (Comptime)：这是 C* 的核心灵魂。允许开发者在编译期直接生成大量特化的函数（体系例如一键生成数千个模拟器指令处理函数），极大提高指令： 
分发效率。
 
📁 文件格式
 
C* 拥有专属的三位一体   .cppo：C 源代码文件（C-Star Program Object）。
• .hppo：C* 头文件，用于暴露接口给外部 C/Objective-C/Swift 环境。
• .cso：C* 编译产物（C-Star Object），可直接链接进 App。 
⚙️ 编译架构
 
C* 编译器的设计路线极其克制，我们生成 C 代码并借用 Clang 后端的力量，这是工程上性价比最高、且能极致压榨设备性能的方案：
.cppo 源码 -> cstar (C* 编译器) -> output.c -> clang -O3 -> 原生 ARM64 可执行文件
 
📱 快速开始（在越狱 iPhone 上）
 
你不需要一台 Mac，只需要越狱的 iPhone、NewTerm 终端、Clang 和 Python。
 
1. 编译 C* 编译器本身
clang src/cstar.c -o cstar
 
2. 编写你的第一个 C* 程序（examples/hello.cppo）
struct CPU {
    u64 pc;
    u64 x0;
}

comptime for op in 0 to 4 {
    int op_{op}(CPU* cpu) { cpu->x0 = {op} + 10; return 0; }
}

fn main() -> int {
    let cpu = CPU();
    cpu.pc = 0;
    cpu.x0 = 0;

    while cpu.pc < 12 {
        let inst = 1;
        execute(inst, &cpu);
        cpu.pc = cpu.pc + 4;
    }
    
    print(”Final PC = {cpu.pc}“);
    print(”Final X0 = {cpu.x0}“);
    return 0;
}
 
3. 编译并运行 C* 程序
./cstar examples/hello.cppo
clang output.c -o hello
./hello
 
📌 当前进度与里程碑
• v0.1 破冰：成功实现词法分析、语法分析（识别函数、变量、字符串）并生成 C 代码。
• v0.2 控制流与指针：加入 if-else、while，支持指针（->、&）和结构体 struct 内存布局。
• v0.3 内存操作：加入数组（[]）、位运算（<<、>>、&、|）以及指针强制类型转换。
• v0.4 编译期穷举 (Current)：引入 comptime 语法，实现编译期循环生成指令处理函数。
• v1.0 自举 (Future)：用 C* 语言重写 C* 编译器，彻底剥离对 C 语言的依赖。
• v2.0 IDE (Future)：开发专属的移动端 IDE，为 C* 语言赋予正式的版本标签。 
🤝 贡献与理念
 
C* 诞生于一个疯狂的想法：在手机上，用 C 语言写一个编译器，再去编译一个专门写模拟器的语言，最终去模拟别的机器。
 
我们在无数个死循环、内存段错误和解析错位中挣扎求生。如果你对底层系统编程、编译器设计、iOS 越狱开发或游戏机模拟器有着同样的狂热，欢迎加入我们，一起把 C* 推向极致！ 
 
“不要让硬件的沙盒限制你的想象力，用代码去重构规则。”