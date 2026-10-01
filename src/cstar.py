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

        # 指针解引用赋值 *(ptr) = val;
        if val == '(' and i + 1 < len(tokens) and tokens[i+1][1] == '*':
            i += 1
            ptr_expr, i = parse_expr(tokens, i)
            i += 1 # 跳过 )
            if i < len(tokens) and tokens[i][1] == '=':
                i += 1
                rhs, i = parse_expr(tokens, i)
                c_code += f"    {ptr_expr} = {rhs};\n"

        # 普通赋值
        elif kind == 'IDENT' and i + 1 < len(tokens) and tokens[i+1][1] == '=':
            lhs = val
            i += 2
            rhs, i = parse_expr(tokens, i)
            c_code += f"    {lhs} = {rhs};\n"

        # 函数
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
                        if i + 1 < len(tokens) and tokens[i+1][1] == '*':
                            p_type += '*'
                            i += 1
                        params.append(f"{p_type} {p_name}")
                i += 1
            i += 2
            c_code += f"int {func_name}({', '.join(params)}) {{\n"

        # 变量声明
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2
            rhs, i = parse_expr(tokens, i)
            c_code += f"    __auto_type {var_name} = {rhs};\n"

        elif val == 'if':
            i += 1
            cond, i = parse_expr(tokens, i)
            c_code += f"    if ({cond}) {{\n"
            i += 1

        elif val == 'while':
            i += 1
            cond, i = parse_expr(tokens, i)
            c_code += f"    while ({cond}) {{\n"
            i += 1

        elif val == 'else':
            c_code += "    } else {\n"
            i += 1

        elif val == 'return':
            i += 1
            rhs, i = parse_expr(tokens, i)
            c_code += f"    return {rhs};\n"

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
        i += 1
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")