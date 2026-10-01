import sys
import re

def lexer(source):
    tokens = []
    token_spec = [
        ('STRING', r'".*?"'), ('NUMBER', r'\d+'), ('IDENT', r'[a-zA-Z_]\w*'),
        ('OP', r'==|!=|<=|>=|\+=|-=|\*=|/=|->|[+\-*/=<>!{};:,&\[\].]'),
        ('BRACE', r'[()]'), ('SKIP', r'[ \t\n]+'),
    ]
    tok_regex = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_spec)
    for mo in re.finditer(tok_regex, source):
        if mo.lastgroup != 'SKIP':
            tokens.append((mo.lastgroup, mo.group()))
    return tokens

def collect_until(tokens, i, stop_tokens):
    res = []
    while i < len(tokens) and tokens[i][1] not in stop_tokens:
        res.append(tokens[i][1])
        i += 1
    return ' '.join(res), i

def codegen(tokens):
    c_code = "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\ntypedef uint64_t u64;\ntypedef uint8_t u8;\n\n"
    known_structs = set()
    i = 0
    while i < len(tokens):
        kind, val = tokens[i]

        # 1. 结构体声明 (改进版，严格识别 类型 变量名)
        if val == 'struct':
            i += 1
            struct_name = tokens[i][1]
            known_structs.add(struct_name)
            i += 2 # 跳过 { 
            c_code += f"typedef struct {struct_name} {{\n"
            while i < len(tokens) and tokens[i][1] != '}':
                if tokens[i][0] == 'IDENT' and i + 1 < len(tokens) and tokens[i+1][0] == 'IDENT':
                    c_code += f"    {tokens[i][1]} {tokens[i+1][1]};\n"
                    i += 2
                else:
                    i += 1
            c_code += f"}} {struct_name};\n\n"
            i += 1 # 跳过 }

        # 2. 函数声明
        elif val == 'fn':
            i += 1
            func_name = tokens[i][1]
            i += 2
            c_code += f"int {func_name}() {{\n"

        # 3. 变量声明 (识别结构体初始化)
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2 # 跳过 =
            if tokens[i][0] == 'IDENT' and tokens[i][1] in known_structs:
                c_code += f"    {tokens[i][1]} {var_name};\n"
                while i < len(tokens) and tokens[i][1] != ';': i += 1
            else:
                expr, i = collect_until(tokens, i, [';'])
                c_code += f"    __auto_type {var_name} = {expr};\n"

        # 4. while 循环
        elif val == 'while':
            i += 1
            cond, i = collect_until(tokens, i, ['{'])
            c_code += f"    while ({cond}) {{\n"
            i += 1 # 跳过 {

        # 5. if 分支
        elif val == 'if':
            i += 1
            cond, i = collect_until(tokens, i, ['{'])
            c_code += f"    if ({cond}) {{\n"
            i += 1 # 跳过 {
        elif val == 'else':
            c_code += "    } else {\n"
            i += 1

        # 6. 赋值语句 (cpu.pc = 100;)
        elif (kind == 'IDENT' or val == '(') and i + 1 < len(tokens):
            lookahead = i
            is_assign = False
            while lookahead < len(tokens) and tokens[lookahead][1] not in [';', '{', '}']:
                if tokens[lookahead][1] == '=':
                    is_assign = True
                    break
                lookahead += 1
            if is_assign:
                lhs, i = collect_until(tokens, i, ['='])
                i += 1 # 跳过 =
                rhs, i = collect_until(tokens, i, [';'])
                c_code += f"    {lhs} = {rhs};\n"
            else:
                i += 1

        # 7. return
        elif val == 'return':
            i += 1
            expr, i = collect_until(tokens, i, [';'])
            c_code += f"    return {expr};\n"

        # 8. print
        elif val == 'print':
            i += 1
            if tokens[i][1] == '(': i += 1
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
            
        # 终极保底：不管发生什么，i 必定向前走一步！
        else:
            i += 1
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")