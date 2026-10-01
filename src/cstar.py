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
    start_i = i
    while i < len(tokens):
        k, v = tokens[i]
        if v in ['(', '[']: depth += 1
        elif v in [')', ']']:
            if depth == 0 and v == ')': break
            depth -= 1
        elif v == ';' and depth == 0: break
        expr.append(v)
        i += 1
    # 强制推进，防止死循环
    if i == start_i and i < len(tokens):
        i += 1
    return ' '.join(expr), i

def codegen(tokens):
    c_code = "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\ntypedef uint64_t u64;\ntypedef uint8_t u8;\n\n"
    i = 0
    while i < len(tokens):
        kind, val = tokens[i]

        # 1. 函数声明
        if val == 'fn':
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

        # 2. 变量声明
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2
            rhs, i = parse_expr(tokens, i)
            c_code += f"    __auto_type {var_name} = {rhs};\n"

        # 3. 控制流
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

        # 4. 通用赋值
        elif kind in ['IDENT', 'OP'] or val == '(':
            lookahead = i
            is_assign = False
            while lookahead < len(tokens) and tokens[lookahead][1] not in [';', '{', '}']:
                if tokens[lookahead][1] == '=':
                    is_assign = True
                    break
                lookahead += 1
            
            if is_assign:
                lhs_tokens = []
                while i < len(tokens) and tokens[i][1] != '=':
                    lhs_tokens.append(tokens[i][1])
                    i += 1
                i += 1
                rhs, i = parse_expr(tokens, i)
                c_code += f"    {' '.join(lhs_tokens)} = {rhs};\n"
            else:
                i += 1
        else:
            # 最后的保底，绝对不让主循环卡死
            i += 1
            
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")