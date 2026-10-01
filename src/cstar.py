# src/cstar.py - C* 编译器第一版原型
import sys
import re

# 1. 词法分析器
def lexer(source):
    tokens = []
    token_spec = [
        ('NUMBER',   r'\d+'),
        ('IDENT',    r'[a-zA-Z_]\w*'),
        ('OP',       r'[+\-*/=<>!]'),
        ('BRACE',    r'[{}()]'),
        ('SKIP',     r'[ \t\n]+'),
    ]
    tok_regex = '|'.join(f'(?P<{pair[0]}>{pair[1]})' for pair in token_spec)
    for mo in re.finditer(tok_regex, source):
        kind = mo.lastgroup
        value = mo.group()
        if kind != 'SKIP':
            tokens.append((kind, value))
    return tokens

# 2. 语法分析 & 代码生成（C 后端）
def codegen(tokens):
    c_code = "#include <stdio.h>\n\n"
    i = 0
    while i < len(tokens):
        kind, val = tokens[i]
        if val == 'fn':
            i += 1
            func_name = tokens[i][1]
            i += 1 # 跳过 (
            params = []
            while tokens[i][1] != ')':
                if tokens[i][0] == 'IDENT':
                    params.append(tokens[i][1] + " " + tokens[i+1][1])
                    i += 1
                i += 1
            i += 1 # 跳过 )
            i += 1 # 跳过 ->
            i += 1 # 跳过 int
            c_code += f"int {func_name}({', '.join(params)}) {{\n"
        elif val == 'return':
            i += 1
            c_code += f"    return {tokens[i][1]};\n"
        elif val == '}':
            c_code += "}\n\n"
        i += 1
    return c_code

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 cstar.py <file.cppo>")
        sys.exit(1)
    with open(sys.argv[1], 'r') as f:
        source = f.read()
    tokens = lexer(source)
    c_output = codegen(tokens)
    with open('output.c', 'w') as f:
        f.write(c_output)
    print(f"[C*] 编译成功，生成 output.c")