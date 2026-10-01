import sys
import re

def lexer(source):
    tokens = []
    token_spec = [
        ('STRING', r'".*?"'), ('NUMBER', r'\d+'), ('IDENT', r'[a-zA-Z_]\w*'),
        ('OP', r'==|!=|<=|>=|\+=|-=|\*=|/=|->|[+\-*/=<>!{};:,&\[\]]'),
        ('BRACE', r'[()]'), ('SKIP', r'[ \t\n]+'),
    ]
    tok_regex = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_spec)
    for mo in re.finditer(tok_regex, source):
        if mo.lastgroup != 'SKIP':
            tokens.append((mo.lastgroup, mo.group()))
    return tokens

# 提取表达式的辅助函数（处理括号嵌套，遇到 ; 或 ) 停止）
def parse_expr(tokens, i):
    expr = []
    depth = 0
    while i < len(tokens):
        k, v = tokens[i]
        if v in ['(', '[']: depth += 1
        elif v in [')', ']']:
            if depth == 0 and v == ')': break
            depth -= 1
        elif v == ';' and depth == 0: break
        expr.append(v)
        i += 1
    return ' '.join(expr), i

def codegen(tokens):
    c_code = "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\ntypedef uint64_t u64;\ntypedef uint8_t u8;\n\n"
    i = 0
    while i < len(tokens):
        kind, val = tokens[i]

        # 1. 赋值语句（必须以 IDENT 开头，且后面跟着 =）
        if kind == 'IDENT' and i + 1 < len(tokens) and tokens[i+1][1] == '=':
            lhs = val
            i += 2
            rhs, i = parse_expr(tokens, i)
            c_code += f"    {lhs} = {rhs};\n"

        # 2. 指针解引用赋值 *(ptr) = val;
        elif val == '(' and i + 1 < len(tokens) and tokens[i+1][1] == '*':
            i += 1
            ptr_expr, i = parse_expr(tokens, i) # 提取 *ptr
            i += 1 # 跳过 )
            if i < len(tokens) and tokens[i][1] == '=':
                i += 1
                rhs, i = parse_expr(tokens, i)
                c_code += f"    {ptr_expr} = {rhs};\n"

        # 3. 函数
        elif val == 'fn':
            i += 1
            func_name = tokens[i][1]
            i += 2
            params = []
            while i < len(tokens) and tokens[i][1] != ')':
                if tokens[i][0] == 'IDENT':
                    p_name = tokens[i][1]
                    i += 1
                    if tokens[i][1] == ':':
                        i += 1
                        p_type = tokens[i][1]
                        # 支持指针类型声明
                        if i + 1 < len(tokens) and tokens[i+1][1] == '*':
                            p_type += '*'
                            i += 1
                        params.append(f"{p_type} {p_name}")
                i += 1
            i += 2
            c_code += f"int {func_name}({', '.join(params)}) {{\n"

        # 4. 变量声明（引入 __auto_type 完美支持指针推导）
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2
            rhs, i = parse_expr(tokens, i)
            c_code += f"    __auto_type {var_name} = {rhs};\n"

        # 5. 结构体（完美支持 *u64 指针成员）
        elif val == 'struct':
            i += 1
            struct_name = tokens[i][1]
            i += 2
            c_code += f"typedef struct {struct_name} {{\n"
            while i < len(tokens) and tokens[i][1] != '}':
                # 解析结构体成员，例如 *u64 pc;
                f_tokens = []
                while i < len(tokens) and tokens[i][1] not in [';', '}']:
                    f_tokens.append(tokens[i][1])
                    i += 1
                if len(f_tokens) >= 2:
                    # 把 *u64 pc 转换为 u64 *pc
                    if f_tokens[0] == '*':
                        c_code += f"    {f_tokens[1]} *{f_tokens[2]};\n"
                    else:
                        c_code += f"    {f_tokens[0]} {f_tokens[1]};\n"
                i += 1
            c_code += f"}} {struct_name};\n\n"

        elif val == 'if':
            i += 1
            cond, i = parse_expr(tokens, i)
            c_code += f"    if ({cond}) {{\n"
            i += 1 # 跳过 {

        elif val == 'while':
            i += 1
            cond, i = parse_expr(tokens, i)
            c_code += f"    while ({cond}) {{\n"
            i += 1 # 跳过 {

        elif val == 'else':
            c_code += "    } else {\n"
            i += 1

        elif val == 'return':
            i += 1
            rhs, i = parse_expr(tokens, i)
            c_code += f"    return {rhs};\n"

        elif val == 'print':
            i += 2 # 跳过 ( 和 "
            # 提取字符串和变量
            if tokens[i][0] == 'STRING':
                str_val = tokens[i][1][1:-1]
                vars_in_str = re.findall(r'\{(\w+)\}', str_val)
                c_str = re.sub(r'\{\w+\}', '%d', str_val)
                i += 1
                if vars_in_str:
                    c_code += f'    printf("{c_str}\\n", {", ".join(vars_in_str)});\n'
                else:
                    c_code += f'    printf("{c_str}\\n");\n'

        elif val == '}':
            c_code += "    }\n\n"
        i += 1
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")