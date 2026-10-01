#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

typedef enum { TOK_EOF, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_SYMBOL } TokenType;
typedef struct { TokenType type; char text[256]; } Token;

Token tokens[20000];
int token_count = 0;

void lex(const char* src) {
    int i = 0;
    while (src[i] != '\0') {
        if (isspace(src[i])) { i++; continue; }
        if (src[i] == '"') {
            int len = 0;
            tokens[token_count].type = TOK_STRING;
            tokens[token_count].text[len++] = src[i++];
            while (src[i] != '"' && src[i] != '\0') tokens[token_count].text[len++] = src[i++];
            if (src[i] == '"') tokens[token_count].text[len++] = src[i++];
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        if (isalpha(src[i]) || src[i] == '_') {
            int len = 0;
            tokens[token_count].type = TOK_IDENT;
            while (isalnum(src[i]) || src[i] == '_') tokens[token_count].text[len++] = src[i++];
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        if (isdigit(src[i])) {
            int len = 0;
            tokens[token_count].type = TOK_NUMBER;
            while (isdigit(src[i])) tokens[token_count].text[len++] = src[i++];
            tokens[token_count].text[len] = '\0';
            token_count++;
            continue;
        }
        // 关键修复：优先匹配双字符操作符
        if (src[i] == '=' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "=="); token_count++; i += 2; continue; }
        if (src[i] == '!' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "!="); token_count++; i += 2; continue; }
        if (src[i] == '<' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "<="); token_count++; i += 2; continue; }
        if (src[i] == '>' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, ">="); token_count++; i += 2; continue; }
        if (src[i] == '-' && src[i+1] == '>') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "->"); token_count++; i += 2; continue; }
        
        // 单字符符号
        tokens[token_count].type = TOK_SYMBOL;
        tokens[token_count].text[0] = src[i++];
        tokens[token_count].text[1] = '\0';
        token_count++;
    }
    tokens[token_count].type = TOK_EOF;
}

void parse_function_params(FILE* out, int start_idx, int* end_idx) {
    int i = start_idx;
    fprintf(out, "(");
    bool first = true;
    while (i < token_count && tokens[i].text[0] != ')') {
        if (tokens[i].type == TOK_IDENT) {
            if (!first) fprintf(out, ", ");
            first = false;
            char* name = tokens[i].text;
            i++;
            if (tokens[i].text[0] == ':') i++;
            char type_str[64] = "";
            while (i < token_count && tokens[i].text[0] != ',' && tokens[i].text[0] != ')') {
                strcat(type_str, tokens[i].text);
                i++;
            }
            if (type_str[0] == '*') {
                fprintf(out, "%s* %s", type_str + 1, name);
            } else if (strstr(type_str, "*") != NULL) {
                char base_type[64];
                strcpy(base_type, type_str);
                base_type[strlen(base_type)-1] = '\0';
                fprintf(out, "%s* %s", base_type, name);
            } else {
                fprintf(out, "%s %s", type_str, name);
            }
        }
        if (tokens[i].text[0] == ',') i++;
    }
    fprintf(out, ")");
    *end_idx = i;
}

void parse_and_gen(FILE* out) {
    fprintf(out, "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\ntypedef uint64_t u64;\ntypedef uint32_t u32;\ntypedef uint8_t u8;\n\n");
    
    char struct_names[50][64];
    int s_count = 0;
    
    for (int i = 0; i < token_count; i++) {
        char* t = tokens[i].text;
        
        // 1. struct
        if (strcmp(t, "struct") == 0 && tokens[i+1].type == TOK_IDENT) {
            char* sname = tokens[i+1].text;
            strcpy(struct_names[s_count++], sname);
            fprintf(out, "typedef struct %s {\n", sname);
            i += 3;
            while (tokens[i].text[0] != '}') {
                if (tokens[i].type == TOK_IDENT && tokens[i+1].type == TOK_IDENT) {
                    fprintf(out, "    %s %s;\n", tokens[i].text, tokens[i+1].text);
                    i += 2;
                } else i += 1;
            }
            fprintf(out, "} %s;\n\n", sname);
            continue;
        }
        
        // 2. fn
        if (strcmp(t, "fn") == 0 && tokens[i+1].type == TOK_IDENT) {
            char* func_name = tokens[i+1].text;
            i += 2;
            fprintf(out, "int %s", func_name);
            parse_function_params(out, i + 1, &i);
            while (tokens[i].text[0] != '{' && i < token_count) i++;
            fprintf(out, " {\n");
            continue;
        }
        
        // 3. let
        if (strcmp(t, "let") == 0 && tokens[i+1].type == TOK_IDENT) {
            char* var_name = tokens[i+1].text;
            bool is_struct_init = false;
            if (i+3 < token_count && tokens[i+2].text[0] == '=' && tokens[i+3].type == TOK_IDENT) {
                for (int k = 0; k < s_count; k++) {
                    if (strcmp(tokens[i+3].text, struct_names[k]) == 0) { is_struct_init = true; break; }
                }
            }
            if (is_struct_init) {
                fprintf(out, "    %s %s;\n", tokens[i+3].text, var_name);
                i += 4;
                while (tokens[i].text[0] != ';' && i < token_count) i++;
            } else {
                fprintf(out, "    __auto_type %s = ", var_name);
                i += 3;
                while (tokens[i].text[0] != ';' && i < token_count) {
                    fprintf(out, "%s ", tokens[i].text);
                    i++;
                }
                fprintf(out, ";\n");
            }
            continue;
        }
        
        // 4. print
        if (strcmp(t, "print") == 0) {
            i += 1;
            if (tokens[i].text[0] == '(') i += 1;
            if (tokens[i].type == TOK_STRING) {
                char* raw = tokens[i].text;
                char content[256];
                strncpy(content, raw + 1, strlen(raw) - 2);
                content[strlen(raw) - 2] = '\0';
                char format[256] = "";
                char args[256] = "";
                int pos = 0, fmt_pos = 0, arg_pos = 0;
                bool in_var = false;
                char var_name[64] = "";
                int var_pos = 0;
                while (content[pos] != '\0') {
                    if (content[pos] == '{') { in_var = true; pos++; continue; }
                    if (content[pos] == '}') {
                        in_var = false;
                        if (arg_pos > 0) strcat(args, ", ");
                        strcat(args, var_name);
                        arg_pos = 1;
                        var_name[0] = '\0';
                        format[fmt_pos++] = '%'; format[fmt_pos++] = 'l'; format[fmt_pos++] = 'l'; format[fmt_pos++] = 'u';
                        pos++; continue;
                    }
                    if (in_var) {
                        var_name[var_pos++] = content[pos++]; var_name[var_pos] = '\0';
                    } else {
                        format[fmt_pos++] = content[pos++];
                    }
                }
                format[fmt_pos] = '\0';
                if (arg_pos > 0) fprintf(out, "    printf(\"%s\\n\", %s);\n", format, args);
                else fprintf(out, "    printf(\"%s\\n\");\n", format);
                i += 1;
            }
            continue;
        }
        
        // 5. while
        if (strcmp(t, "while") == 0) {
            char cond[256] = "";
            i += 1;
            while (tokens[i].text[0] != '{' && i < token_count) { strcat(cond, tokens[i].text); strcat(cond, " "); i++; }
            fprintf(out, "    while (%s) {\n", cond);
            continue;
        }
        
        // 6. if
        if (strcmp(t, "if") == 0) {
            char cond[256] = "";
            i += 1;
            while (tokens[i].text[0] != '{' && i < token_count) { strcat(cond, tokens[i].text); strcat(cond, " "); i++; }
            fprintf(out, "    if (%s) {\n", cond);
            continue;
        }
        
        // 7. return
        if (strcmp(t, "return") == 0) {
            fprintf(out, "    return ");
            i += 1;
            while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
            fprintf(out, ";\n");
            continue;
        }
        
        // 8. 赋值
        if ((tokens[i].type == TOK_IDENT || t[0] == '(') && i + 1 < token_count) {
            int lookahead = i;
            bool is_assign = false;
            while (lookahead < token_count && tokens[lookahead].text[0] != ';' && tokens[lookahead].text[0] != '{' && tokens[lookahead].text[0] != '}') {
                if (tokens[lookahead].text[0] == '=') { is_assign = true; break; }
                lookahead++;
            }
            if (is_assign) {
                while (tokens[i].text[0] != '=') { fprintf(out, "%s", tokens[i].text); i++; }
                fprintf(out, " = ");
                i += 1;
                while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
                fprintf(out, ";\n");
                continue;
            }
        }
        
        // 9. 独立函数调用
        if (tokens[i].type == TOK_IDENT && i + 1 < token_count && tokens[i+1].text[0] == '(') {
            fprintf(out, "    ");
            while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
            fprintf(out, ";\n");
            continue;
        }
        
        // 10. 右花括号
        if (t && t[0] == '}') {
            if (i + 1 < token_count && strcmp(tokens[i+1].text, "else") == 0) {
                fprintf(out, "    } else {\n");
                i++;
            } else {
                fprintf(out, "}\n\n");
            }
            continue;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <file.cppo>\n", argv[0]); return 1; }
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