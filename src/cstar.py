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
                        params.append(f"{p_type} {p_name}")
                i += 1
            i += 2
            c_code += f"int {func_name}({', '.join(params)}) {{\n"

        # 2. 变量声明
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2
            expr_tokens = []
            while i < len(tokens) and tokens[i][1] != ';':
                expr_tokens.append(tokens[i][1])
                i += 1
            c_code += f"    __auto_type {var_name} = {' '.join(expr_tokens)};\n"

        # 3. 返回值
        elif val == 'return':
            i += 1
            expr_tokens = []
            while i < len(tokens) and tokens[i][1] != ';':
                expr_tokens.append(tokens[i][1])
                i += 1
            c_code += f"    return {' '.join(expr_tokens)};\n"

        # 4. 打印
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
            if i < len(tokens) and tokens[i][1] == ')': i += 1

        elif val == '}':
            c_code += "    }\n\n"
        else:
            # 终极保底：任何不认识的符号，直接跳过，绝对不卡死！
            i += 1
            
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")