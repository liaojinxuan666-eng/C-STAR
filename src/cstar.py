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
    c_code = "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\n"
    i = 0
    while i < len(tokens):
        kind, val = tokens[i]

        # 1. 函数声明
        if val == 'fn':
            i += 1
            func_name = tokens[i][1]
            i += 2
            c_code += f"int {func_name}() {{\n"

        # 2. 变量声明
        elif val == 'let':
            i += 1
            var_name = tokens[i][1]
            i += 2
            expr_tokens = []
            while i < len(tokens) and tokens[i][1] != ';':
                expr_tokens.append(tokens[i][1])
                i += 1
            c_code += f"    int {var_name} = {' '.join(expr_tokens)};\n"

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
                c_str = str_val.replace('{a}', '%d')
                i += 1
                if '{a}' in str_val:
                    c_code += f'    printf("{c_str}\\n", a);\n'
                else:
                    c_code += f'    printf("{c_str}\\n");\n'

        elif val == '}':
            c_code += "    }\n\n"
            
        # 终极保底：不管发生什么，i 必定向前走一步！绝对不死循环！
        i += 1
            
    return c_code

if __name__ == '__main__':
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    with open('output.c', 'w') as f:
        f.write(codegen(lexer(source)))
    print("[C*] 编译成功，生成 output.c")