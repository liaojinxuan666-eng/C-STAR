#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

typedef enum { TOK_EOF, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_SYMBOL } TokenType;
typedef struct { TokenType type; char text[256]; } Token;

Token tokens[40000];
int token_count = 0;

// ================= 词法分析 (Lexer) =================
void lex(const char* src) {
    int i = 0;
    while (src[i] != '\0') {
        if (src[i] == '/' && src[i+1] == '/') { while (src[i] != '\n' && src[i] != '\0') i++; continue; }
        if (src[i] == '/' && src[i+1] == '*') { i += 2; while (src[i] != '*' || src[i+1] != '/') { if (src[i] == '\0') break; i++; } i += 2; continue; }
        if (isspace(src[i])) { i++; continue; }
        
        if (src[i] == '"') { 
            int len = 0; 
            tokens[token_count].type = TOK_STRING; 
            tokens[token_count].text[len++] = src[i++]; 
            while (src[i] != '"' && src[i] != '\0') tokens[token_count].text[len++] = src[i++]; 
            if (src[i] == '"') tokens[token_count].text[len++] = src[i++]; 
            tokens[token_count].text[len] = '\0'; 
            token_count++; continue; 
        }
        
        if (isalpha(src[i]) || src[i] == '_') { 
            int len = 0; 
            tokens[token_count].type = TOK_IDENT; 
            while (isalnum(src[i]) || src[i] == '_') tokens[token_count].text[len++] = src[i++]; 
            tokens[token_count].text[len] = '\0'; 
            token_count++; continue; 
        }
        
        if (isdigit(src[i])) { 
            int len = 0; 
            tokens[token_count].type = TOK_NUMBER; 
            if (src[i] == '0' && (src[i+1] == 'x' || src[i+1] == 'X')) { 
                tokens[token_count].text[len++] = src[i++]; 
                tokens[token_count].text[len++] = src[i++]; 
                while (isxdigit(src[i])) tokens[token_count].text[len++] = src[i++]; 
            } else { 
                while (isdigit(src[i])) tokens[token_count].text[len++] = src[i++]; 
            } 
            tokens[token_count].text[len] = '\0'; 
            token_count++; continue; 
        }
        
        // 单独处理 & 符号，防止被吞
        if (src[i] == '&') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "&"); token_count++; i++; continue; }
        
        if (src[i] == '=' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "=="); token_count++; i += 2; continue; }
        if (src[i] == '!' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "!="); token_count++; i += 2; continue; }
        if (src[i] == '<' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "<="); token_count++; i += 2; continue; }
        if (src[i] == '>' && src[i+1] == '=') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, ">="); token_count++; i += 2; continue; }
        if (src[i] == '-' && src[i+1] == '>') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "->"); token_count++; i += 2; continue; }
        if (src[i] == '<' && src[i+1] == '<') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, "<<"); token_count++; i += 2; continue; }
        if (src[i] == '>' && src[i+1] == '>') { tokens[token_count].type = TOK_SYMBOL; strcpy(tokens[token_count].text, ">>"); token_count++; i += 2; continue; }
        
        tokens[token_count].type = TOK_SYMBOL; 
        tokens[token_count].text[0] = src[i++]; 
        tokens[token_count].text[1] = '\0'; 
        token_count++;
    }
    tokens[token_count].type = TOK_EOF;
    strcpy(tokens[token_count].text, "EOF");
}

// ================= 辅助代码生成 =================
void parse_function_params(FILE* out, int start_idx, int* end_idx) {
    int i = start_idx;
    fprintf(out, "(");
    bool first = true;
    while (i < token_count && tokens[i].text[0] != ')') {
        if (!first) fprintf(out, ", ");
        first = false;

        char param_tokens[10][64];
        int param_count = 0;
        while (i < token_count && tokens[i].text[0] != ',' && tokens[i].text[0] != ')') {
            strncpy(param_tokens[param_count], tokens[i].text, 63);
            param_tokens[param_count][63] = '\0';
            param_count++;
            i++;
        }
        
        if (param_count >= 3 && strcmp(param_tokens[1], ":") == 0) {
            // C* 风格 "name: type" -> C 风格 "type name"
            fprintf(out, "%s %s", param_tokens[2], param_tokens[0]);
            if (param_count > 3) {
                for (int k = 3; k < param_count; k++) fprintf(out, "%s", param_tokens[k]);
            }
        } else {
            // C 风格 "type name" 或 "type *name" 直接透传
            for (int k = 0; k < param_count; k++) {
                fprintf(out, "%s", param_tokens[k]);
                if (k < param_count - 1) fprintf(out, " ");
            }
        }

        if (tokens[i].text[0] == ',') i++;
    }
    fprintf(out, ")");
    *end_idx = i;
}

// ================= 语法分析与 C 代码生成 =================
void parse_and_gen(FILE* out) {
    fprintf(out, "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\n");
    fprintf(out, "typedef uint64_t u64;\ntypedef uint32_t u32;\ntypedef uint16_t u16;\ntypedef uint8_t u8;\n\n");
    
    fprintf(out, "#define _CSTAR_PRINT(x) _Generic((x), \\\n");
    fprintf(out, "    char*: \"%%s\", const char*: \"%%s\", \\\n");
    fprintf(out, "    int: \"%%d\", unsigned int: \"%%u\", \\\n");
    fprintf(out, "    long: \"%%ld\", unsigned long: \"%%lu\", \\\n");
    fprintf(out, "    long long: \"%%lld\", unsigned long long: \"%%llu\", \\\n");
    fprintf(out, "    float: \"%%f\", double: \"%%f\", \\\n");
    fprintf(out, "    default: \"%%llu\")(x)\n\n");

    for (int i = 0; i < token_count; i++) {
        char* t = tokens[i].text;

        if (strcmp(t, "include") == 0) {
            i++; 
            if (tokens[i].text[0] == '<') {
                fprintf(out, "#include <");
                i++;
                while (tokens[i].text[0] != '>') { fprintf(out, "%s", tokens[i].text); i++; }
                fprintf(out, ">\n");
            } else if (tokens[i].text[0] == '"') {
                fprintf(out, "#include %s\n", tokens[i].text);
            }
            continue;
        }

        if (strcmp(t, "struct") == 0 && tokens[i+1].type == TOK_IDENT) {
            char struct_name[64];
            strcpy(struct_name, tokens[i+1].text);
            fprintf(out, "typedef struct %s {\n", struct_name);
            i += 3;
            while (i < token_count && tokens[i].text[0] != '}') {
                if (tokens[i].text[0] == ';') {
                    fprintf(out, ";\n");
                } else {
                    fprintf(out, "%s ", tokens[i].text);
                }
                i++;
            }
            fprintf(out, "} %s;\n\n", struct_name);
            continue;
        }

        if (strcmp(t, "comptime") == 0) {
            i += 1;
            if (strcmp(tokens[i].text, "for") == 0) {
                i += 1;
                if (tokens[i].text[0] == '(') i += 1;
                char loop_var[64]; strcpy(loop_var, tokens[i].text); i += 1;
                if (strcmp(tokens[i].text, "in") == 0) i += 1;
                int start_val = atoi(tokens[i].text); i += 1;
                if (strcmp(tokens[i].text, "to") == 0) i += 1;
                int end_val = atoi(tokens[i].text); i += 1;
                if (tokens[i].text[0] == ')') i += 1;
                if (tokens[i].text[0] == '{') i += 1;

                int template_start = i; int template_end = i;
                int brace_depth = 1;
                while (template_end < token_count && brace_depth > 0) {
                    if (tokens[template_end].text[0] == '{') brace_depth++;
                    if (tokens[template_end].text[0] == '}') brace_depth--;
                    if (brace_depth == 0) break;
                    template_end++;
                }
                
                for (int op = start_val; op < end_val; op++) {
                    char num_str[16]; sprintf(num_str, "%d", op);
                    fprintf(out, "static inline "); 
                    
                    for (int k = template_start; k < template_end; k++) {
                        if (tokens[k].type == TOK_IDENT && k + 3 < template_end &&
                            tokens[k+1].text[0] == '{' && tokens[k+2].type == TOK_IDENT &&
                            strcmp(tokens[k+2].text, loop_var) == 0 && tokens[k+3].text[0] == '}') {
                            
                            fprintf(out, "%s%s", tokens[k].text, num_str);
                            k += 3;
                        } else if (tokens[k].text[0] == '{' && tokens[k+1].type == TOK_IDENT && 
                                   strcmp(tokens[k+1].text, loop_var) == 0 && tokens[k+2].text[0] == '}') {
                            fprintf(out, "%s", num_str);
                            k += 2;
                        } else {
                            if (tokens[k].type == TOK_SYMBOL) fprintf(out, "%s", tokens[k].text);
                            else fprintf(out, "%s ", tokens[k].text);
                        }
                    }
                    fprintf(out, "\n");
                }
                
                // 核心修改：移除硬编码的 execute 生成，完全由用户在 .cppo 中自行决定调用方式
                i = template_end;
                continue;
            }
        }

        if (strcmp(t, "fn") == 0 && tokens[i+1].type == TOK_IDENT) {
            char* func_name = tokens[i+1].text; 
            i += 2;
            
            int ret_type_start = i;
            while (i < token_count && tokens[i].text[0] != '(') i++; 
            int ret_type_end = i; 
            
            if (ret_type_start < ret_type_end) {
                if (strcmp(tokens[ret_type_start].text, "->") == 0) {
                    for (int k = ret_type_start + 1; k < ret_type_end; k++) {
                        fprintf(out, "%s ", tokens[k].text);
                    }
                } else {
                    fprintf(out, "int "); 
                }
            } else {
                fprintf(out, "int "); 
            }
            
            fprintf(out, "%s", func_name); 
            parse_function_params(out, i + 1, &i);
            while (tokens[i].text[0] != '{' && i < token_count) i++;
            fprintf(out, " {\n"); 
            continue;
        }

        if (strcmp(t, "let") == 0 && tokens[i+1].type == TOK_IDENT) {
            char* var_name = tokens[i+1].text;
            if (i+4 < token_count && tokens[i+2].text[0] == '=' && tokens[i+3].type == TOK_IDENT && tokens[i+4].text[0] == '[') {
                fprintf(out, "    %s %s[%s];\n", tokens[i+3].text, var_name, tokens[i+5].text); i += 7; continue;
            }
            if (i+3 < token_count && tokens[i+2].text[0] == '=' && tokens[i+3].type == TOK_IDENT && strcmp(tokens[i+3].text, "CPU") == 0) {
                fprintf(out, "    CPU %s = {0};\n", var_name); i += 4;
                while (tokens[i].text[0] != ';' && i < token_count) i++;
                continue;
            }
            fprintf(out, "    __auto_type %s = ", var_name); i += 3;
            while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
            fprintf(out, ";\n");
            continue;
        }

        if (strcmp(t, "print") == 0) {
            i += 1; if (tokens[i].text[0] == '(') i += 1;
            if (tokens[i].type == TOK_STRING) {
                char* raw = tokens[i].text; char content[256];
                strncpy(content, raw + 1, strlen(raw) - 2); content[strlen(raw) - 2] = '\0';
                char format[256] = ""; char args[256] = ""; int pos = 0, fmt_pos = 0, arg_pos = 0; bool in_var = false; char var_name[64] = ""; int var_pos = 0;
                while (content[pos] != '\0') {
                    if (content[pos] == '{') { in_var = true; pos++; continue; }
                    if (content[pos] == '}') {
                        in_var = false; 
                        if (arg_pos > 0) strcat(args, ", "); 
                        strcat(args, var_name); 
                        arg_pos = 1; 
                        var_name[0] = '\0';
                        strcat(format, "%s"); pos++; continue;
                    }
                    if (in_var) { var_name[var_pos++] = content[pos++]; var_name[var_pos] = '\0'; } 
                    else { 
                        if (content[pos] == '%') { format[fmt_pos++] = '%'; format[fmt_pos++] = '%'; } 
                        else format[fmt_pos++] = content[pos]; 
                        pos++; 
                    }
                }
                format[fmt_pos] = '\0';
                if (arg_pos > 0) {
                    fprintf(out, "    printf(\"%s\\n\", ", format);
                    // 用 strtok 切割出 cpu.x0 这种复合成员表达式
                    char* token = strtok(args, ", ");
                    bool first_arg = true;
                    while (token != NULL) {
                        if (!first_arg) fprintf(out, ", ");
                        fprintf(out, "_CSTAR_PRINT(%s)", token);
                        first_arg = false;
                        token = strtok(NULL, ", ");
                    }
                    fprintf(out, ");\n");
                } else {
                    fprintf(out, "    printf(\"%s\\n\");\n", format);
                }
                i += 1;
            }
            continue;
        }

        if (strcmp(t, "while") == 0) {
            char cond[256] = ""; i += 1;
            while (tokens[i].text[0] != '{' && i < token_count) { strcat(cond, tokens[i].text); strcat(cond, " "); i++; }
            fprintf(out, "    while (%s) {\n", cond); continue;
        }

        if (strcmp(t, "if") == 0) {
            char cond[256] = ""; i += 1;
            while (tokens[i].text[0] != '{' && i < token_count) { strcat(cond, tokens[i].text); strcat(cond, " "); i++; }
            fprintf(out, "    if (%s) {\n", cond); continue;
        }

        if (strcmp(t, "return") == 0) {
            fprintf(out, "    return "); i += 1;
            while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
            fprintf(out, ";\n"); continue;
        }

        if ((tokens[i].type == TOK_IDENT || t[0] == '(' || t[0] == '*') && i + 1 < token_count) {
            int lookahead = i; bool is_assign = false;
            while (lookahead < token_count && tokens[lookahead].text[0] != ';' && tokens[lookahead].text[0] != '{' && tokens[lookahead].text[0] != '}') {
                if (tokens[lookahead].text[0] == '=') { is_assign = true; break; } lookahead++;
            }
            if (is_assign) {
                while (tokens[i].text[0] != '=') { fprintf(out, "%s", tokens[i].text); i++; }
                fprintf(out, " = "); i += 1;
                while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
                fprintf(out, ";\n"); continue;
            }
        }

        if (tokens[i].type == TOK_IDENT && i + 1 < token_count && tokens[i+1].text[0] == '(') {
            fprintf(out, "    ");
            while (tokens[i].text[0] != ';' && i < token_count) { fprintf(out, "%s ", tokens[i].text); i++; }
            fprintf(out, ";\n"); continue;
        }

        if (t && t[0] == '}') {
            if (i + 1 < token_count && strcmp(tokens[i+1].text, "else") == 0) { fprintf(out, "    } else {\n"); i++; }
            else { fprintf(out, "}\n\n"); }
            continue;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <file.cppo>\n", argv[0]); return 1; }
    FILE* fp = fopen(argv[1], "r");
    if (!fp) { printf("Cannot open file\n"); return 1; }
    fseek(fp, 0, SEEK_END); long fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
    char* src = malloc(fsize + 1); fread(src, 1, fsize, fp); src[fsize] = '\0'; fclose(fp);
    lex(src); free(src);
    FILE* out = fopen("output.c", "w");
    parse_and_gen(out); fclose(out);
    printf("[C* Compiler in C] compiled successfully, output.c generated\n");
    return 0;
}