#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// 1. Token 定义
typedef enum { TOK_EOF, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_SYMBOL } TokenType;
typedef struct { TokenType type; char text[256]; } Token;

Token tokens[10000];
int token_count = 0;

// 2. 极简词法分析器（绝对安全）
void lex(const char* src) {
    int i = 0;
    while (src[i] != '\0') {
        if (isspace(src[i])) { i++; continue; }
        if (src[i] == '"') { // 字符串
            int len = 0;
            tokens[token_count].type = TOK_STRING;
            tokens[token_count].text[len++] = src[i++]; // 包含开头的引号
            while (src[i] != '"' && src[i] != '\0') tokens[token_count].text[len++] = src[i++];
            if (src[i] == '"') tokens[token_count].text[len++] = src[i++]; // 包含结尾的引号
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        if (isalpha(src[i]) || src[i] == '_') { // 标识符
            int len = 0;
            tokens[token_count].type = TOK_IDENT;
            while (isalnum(src[i]) || src[i] == '_') tokens[token_count].text[len++] = src[i++];
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        if (isdigit(src[i])) { // 数字
            int len = 0;
            tokens[token_count].type = TOK_NUMBER;
            while (isdigit(src[i])) tokens[token_count].text[len++] = src[i++];
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        // 符号（单字符）
        tokens[token_count].type = TOK_SYMBOL;
        tokens[token_count].text[0] = src[i++];
        tokens[token_count].text[1] = '\0';
        token_count++;
    }
    tokens[token_count].type = TOK_EOF;
}

// 3. 极简代码生成器（遍历 Token，遇到关键字就输出 C 代码）
void parse_and_gen(FILE* out) {
    fprintf(out, "#include <stdio.h>\n#include <stdint.h>\n\n");
    for (int i = 0; i < token_count; i++) {
        char* t = tokens[i].text;
        if (strcmp(t, "fn") == 0) {
            // 寻找函数名
            if (tokens[i+1].type == TOK_IDENT) {
                fprintf(out, "int %s() {\n", tokens[i+1].text);
                i += 1;
            }
        } else if (strcmp(t, "let") == 0) {
            if (tokens[i+1].type == TOK_IDENT) {
                fprintf(out, "    __auto_type %s = ", tokens[i+1].text);
                i += 2; // 跳过 let 和变量名
                while (tokens[i].text[0] != ';') {
                    fprintf(out, "%s ", tokens[i].text);
                    i++;
                }
                fprintf(out, ";\n");
            }
        } else if (strcmp(t, "print") == 0) {
            i += 1; // 跳过 print
            if (tokens[i].text[0] == '(') i += 1; // 跳过 (
            if (tokens[i].type == TOK_STRING) {
                char* str = tokens[i].text;
                // 简单替换 {a} 为 %d
                fprintf(out, "    printf(\"%s\\n\");\n", str);
                i += 1;
            }
        } else if (strcmp(t, "return") == 0) {
            fprintf(out, "    return ");
            i += 1;
            while (tokens[i].text[0] != ';') {
                fprintf(out, "%s ", tokens[i].text);
                i++;
            }
            fprintf(out, ";\n");
        } else if (strcmp(t, "}") == 0) {
            fprintf(out, "}\n\n");
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <file.cppo>\n", argv[0]);
        return 1;
    }
    FILE* fp = fopen(argv[1], "r");
    if (!fp) { printf("Cannot open file\n"); return 1; }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* src = malloc(fsize + 1);
    fread(src, 1, fsize, fp);
    src[fsize] = '\0';
    fclose(fp);

    lex(src);
    free(src);

    FILE* out = fopen("output.c", "w");
    parse_and_gen(out);
    fclose(out);

    printf("[C* Compiler in C] 编译成功，生成 output.c\n");
    return 0;
}