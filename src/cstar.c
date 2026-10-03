#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_TOKENS 40000
#define MAX_SYMBOLS 1000

typedef enum { TOK_EOF, TOK_IDENT, TOK_NUMBER, TOK_STRING, TOK_SYMBOL } TokenType;
typedef struct { TokenType type; char text[256]; } Token;

typedef struct { char name[64]; int is_string; } Symbol;

static Token tokens[MAX_TOKENS];
static int token_count = 0;
static Symbol symbols[MAX_SYMBOLS];
static int symbol_count = 0;

static void die(const char *msg) {
    fprintf(stderr, "C* error: %s\n", msg);
    exit(1);
}

static void add_token(TokenType type, const char *text, size_t len) {
    if (token_count >= MAX_TOKENS - 1) die("too many tokens");
    if (len >= sizeof(tokens[token_count].text)) die("token too long");
    tokens[token_count].type = type;
    memcpy(tokens[token_count].text, text, len);
    tokens[token_count].text[len] = '\0';
    token_count++;
}

static bool tok_is(int i, const char *s) {
    return i >= 0 && i < token_count && strcmp(tokens[i].text, s) == 0;
}

static bool is_interpolation_open(int i, int limit) {
    return i + 2 < limit && tok_is(i, "{") &&
           (tokens[i + 1].type == TOK_IDENT || tokens[i + 1].type == TOK_NUMBER) &&
           tok_is(i + 2, "}");
}

/* Find a normal C* block. {name} interpolation is deliberately ignored. */
static int find_matching_brace(int open, int limit) {
    if (!tok_is(open, "{")) return limit;
    int depth = 0;
    for (int i = open; i < limit; ++i) {
        if (is_interpolation_open(i, limit)) {
            i += 2;
            continue;
        }
        if (tok_is(i, "{")) {
            depth++;
        } else if (tok_is(i, "}")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return limit;
}

/* Return the matching ')' for an opening '('. */
static int find_matching_paren(int open, int limit) {
    if (!tok_is(open, "(")) return limit;
    int depth = 0;
    for (int i = open; i < limit; ++i) {
        if (tok_is(i, "(")) depth++;
        else if (tok_is(i, ")")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return limit;
}

/* Evaluate the small constant-expression subset used by comptime conditions. */
static int eval_expr(int start, int end, const char *loop_var, int loop_val) {
    if (start >= end) return 0;

    if (tok_is(start, "(") && find_matching_paren(start, end) == end - 1)
        return eval_expr(start + 1, end - 1, loop_var, loop_val);

    if (end == start + 1) {
        if (tokens[start].type == TOK_NUMBER) return (int)strtol(tokens[start].text, NULL, 0);
        if (tokens[start].type == TOK_IDENT && strcmp(tokens[start].text, loop_var) == 0) return loop_val;
        return 0;
    }

    /* Lowest-precedence comparisons first. */
    const char *cmp_ops[] = {"==", "!=", "<", ">", "<=", ">="};
    for (int opn = 0; opn < 6; ++opn) {
        for (int i = end - 1; i >= start; --i) {
            if (strcmp(tokens[i].text, cmp_ops[opn]) == 0) {
                int a = eval_expr(start, i, loop_var, loop_val);
                int b = eval_expr(i + 1, end, loop_var, loop_val);
                if (opn == 0) return a == b;
                if (opn == 1) return a != b;
                if (opn == 2) return a < b;
                if (opn == 3) return a > b;
                if (opn == 4) return a <= b;
                return a >= b;
            }
        }
    }

    const char *arith_ops[] = {"+", "-", "%", "*", "/"};
    for (int opn = 0; opn < 5; ++opn) {
        for (int i = end - 1; i >= start; --i) {
            if (strcmp(tokens[i].text, arith_ops[opn]) == 0) {
                int a = eval_expr(start, i, loop_var, loop_val);
                int b = eval_expr(i + 1, end, loop_var, loop_val);
                if (opn == 0) return a + b;
                if (opn == 1) return a - b;
                if (opn == 2) return b ? a % b : 0;
                if (opn == 3) return a * b;
                return b ? a / b : 0;
            }
        }
    }

    return 0;
}

static void lex(const char *src) {
    token_count = 0;
    for (size_t i = 0; src[i] != '\0'; ) {
        unsigned char c = (unsigned char)src[i];

        if (src[i] == '/' && src[i + 1] == '/') {
            i += 2;
            while (src[i] && src[i] != '\n') ++i;
            continue;
        }
        if (src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            if (src[i]) i += 2;
            continue;
        }
        if (isspace(c)) { ++i; continue; }

        if (src[i] == '"') {
            size_t start = i++;
            while (src[i] && src[i] != '"') {
                if (src[i] == '\\' && src[i + 1]) i += 2;
                else ++i;
            }
            if (src[i] == '"') ++i;
            add_token(TOK_STRING, src + start, i - start);
            continue;
        }

        if (isalpha(c) || src[i] == '_') {
            size_t start = i++;
            while (isalnum((unsigned char)src[i]) || src[i] == '_') ++i;
            add_token(TOK_IDENT, src + start, i - start);
            continue;
        }

        if (isdigit(c)) {
            size_t start = i++;
            if (src[start] == '0' && (src[i] == 'x' || src[i] == 'X')) {
                ++i;
                while (isxdigit((unsigned char)src[i])) ++i;
            } else {
                while (isdigit((unsigned char)src[i])) ++i;
            }
            add_token(TOK_NUMBER, src + start, i - start);
            continue;
        }

        const char *two[] = {"==", "!=", "<=", ">=", "->", "<<", ">>"};
        bool matched = false;
        for (size_t k = 0; k < sizeof(two) / sizeof(two[0]); ++k) {
            if (src[i] == two[k][0] && src[i + 1] == two[k][1]) {
                add_token(TOK_SYMBOL, two[k], 2);
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        add_token(TOK_SYMBOL, src + i, 1);
        ++i;
    }
    tokens[token_count].type = TOK_EOF;
    strcpy(tokens[token_count].text, "EOF");
}

static void emit_token(FILE *out, int i) {
    if (tokens[i].type == TOK_SYMBOL) fprintf(out, "%s", tokens[i].text);
    else fprintf(out, "%s ", tokens[i].text);
}

static void emit_range(FILE *out, int start, int end, const char *loop_var, int loop_val) {
    for (int i = start; i < end; ++i) {
        if (is_interpolation_open(i, end)) {
            fprintf(out, "%d", loop_val);
            i += 2;
            continue;
        }
        if (tokens[i].type == TOK_IDENT && i + 3 < end && tok_is(i + 1, "{") &&
            (tokens[i + 2].type == TOK_IDENT || tokens[i + 2].type == TOK_NUMBER) && tok_is(i + 3, "}")) {
            fprintf(out, "%s%d", tokens[i].text, loop_val);
            i += 3;
            continue;
        }
        emit_token(out, i);
    }
    (void)loop_var;
}

/* Parse an if/else pair inside a comptime body. Returns the final token index. */
static int emit_comptime_if(FILE *out, int if_idx, int body_end, const char *loop_var, int loop_val) {
    int cond_open = if_idx + 1;
    if (!tok_is(cond_open, "(")) return if_idx;
    int cond_close = find_matching_paren(cond_open, body_end);
    if (cond_close >= body_end) return if_idx;

    int then_open = cond_close + 1;
    while (then_open < body_end && !tok_is(then_open, "{")) ++then_open;
    if (then_open >= body_end) return if_idx;
    int then_close = find_matching_brace(then_open, body_end);
    if (then_close >= body_end) return if_idx;

    int next = then_close + 1;
    int else_open = -1, else_close = -1;
    if (next < body_end && tok_is(next, "else")) {
        int candidate = next + 1;
        if (tok_is(candidate, "{")) {
            else_open = candidate;
            else_close = find_matching_brace(else_open, body_end);
        }
    }

    int result = eval_expr(cond_open + 1, cond_close, loop_var, loop_val);
    int emit_start = result ? then_open + 1 : (else_open >= 0 ? else_open + 1 : -1);
    int emit_end = result ? then_close : else_close;
    if (emit_start >= 0 && emit_end > emit_start) {
        if (tok_is(emit_start, "int") && emit_start + 1 < emit_end && strncmp(tokens[emit_start + 1].text, "op_", 3) == 0)
            fprintf(out, "static inline ");
        emit_range(out, emit_start, emit_end, loop_var, loop_val);
        fprintf(out, "\n");
    }

    return else_close >= 0 ? else_close : then_close;
}

static void parse_function_params(FILE *out, int start, int *end_idx) {
    int i = start;
    fprintf(out, "(");
    bool first = true;
    while (i < token_count && !tok_is(i, ")")) {
        if (!first) fprintf(out, ", ");
        first = false;
        int begin = i;
        int paren_depth = 0;
        while (i < token_count) {
            if (tok_is(i, "(") ) ++paren_depth;
            else if (tok_is(i, ")") && paren_depth > 0) --paren_depth;
            if (paren_depth == 0 && (tok_is(i, ",") || tok_is(i, ")"))) break;
            ++i;
        }
        int count = i - begin;
        if (count >= 3 && tok_is(begin + 1, ":")) {
            fprintf(out, "%s %s", tokens[begin + 2].text, tokens[begin].text);
            for (int k = begin + 3; k < i; ++k) fprintf(out, "%s", tokens[k].text);
        } else {
            for (int k = begin; k < i; ++k) {
                if (k > begin) fprintf(out, " ");
                fprintf(out, "%s", tokens[k].text);
            }
        }
        if (tok_is(i, ",")) ++i;
    }
    fprintf(out, ")");
    *end_idx = i;
}

static void register_symbol(const char *name, int is_string) {
    for (int i = 0; i < symbol_count; ++i) {
        if (strcmp(symbols[i].name, name) == 0) {
            symbols[i].is_string = is_string;
            return;
        }
    }
    if (symbol_count >= MAX_SYMBOLS) die("too many symbols");
    strncpy(symbols[symbol_count].name, name, sizeof(symbols[symbol_count].name) - 1);
    symbols[symbol_count].name[sizeof(symbols[symbol_count].name) - 1] = '\0';
    symbols[symbol_count].is_string = is_string;
    ++symbol_count;
}

static int symbol_is_string(const char *name) {
    for (int i = 0; i < symbol_count; ++i)
        if (strcmp(symbols[i].name, name) == 0) return symbols[i].is_string;
    return 0;
}

static void parse_and_gen(FILE *out) {
    fprintf(out, "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\n");
    fprintf(out, "typedef uint64_t u64;\ntypedef uint32_t u32;\ntypedef uint16_t u16;\ntypedef uint8_t u8;\n\n");

    for (int i = 0; i < token_count; ++i) {

        if (tok_is(i, "include")) {
            ++i;
            if (tok_is(i, "<")) {
                fprintf(out, "#include <"); ++i;
                while (i < token_count && !tok_is(i, ">")) { fprintf(out, "%s", tokens[i].text); ++i; }
                fprintf(out, ">\n");
            } else if (tokens[i].type == TOK_STRING) {
                fprintf(out, "#include %s\n", tokens[i].text);
            }
            continue;
        }

        if (tok_is(i, "struct") && tokens[i + 1].type == TOK_IDENT) {
            char name[64]; strncpy(name, tokens[i + 1].text, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
            fprintf(out, "typedef struct %s {\n", name);
            i += 3;
            while (i < token_count && !tok_is(i, "}")) {
                if (tok_is(i, ";")) fprintf(out, ";\n");
                else emit_token(out, i);
                ++i;
            }
            fprintf(out, "} %s;\n\n", name);
            continue;
        }

        if (tok_is(i, "comptime")) {
            int j = i + 1;
            if (!tok_is(j, "for")) continue;
            ++j;
            if (tok_is(j, "(")) ++j;
            if (tokens[j].type != TOK_IDENT) die("expected comptime loop variable");
            char loop_var[64]; strncpy(loop_var, tokens[j].text, sizeof(loop_var) - 1); loop_var[sizeof(loop_var) - 1] = '\0';
            ++j;
            if (!tok_is(j, "in")) die("expected 'in' in comptime for");
            int start_val = (int)strtol(tokens[++j].text, NULL, 0);
            ++j;
            if (!tok_is(j, "to")) die("expected 'to' in comptime for");
            int end_val = (int)strtol(tokens[++j].text, NULL, 0);
            ++j;
            if (tok_is(j, ")")) ++j;
            if (!tok_is(j, "{")) die("expected '{' after comptime for");

            int body_start = j;
            int body_end = find_matching_brace(body_start, token_count);
            if (body_end >= token_count) die("unterminated comptime block");

            for (int op = start_val; op < end_val; ++op) {
                for (int k = body_start + 1; k < body_end; ++k) {
                    if (tok_is(k, "if")) {
                        int last = emit_comptime_if(out, k, body_end, loop_var, op);
                        k = last;
                    } else if (tok_is(k, "int") && k + 1 < body_end && strncmp(tokens[k + 1].text, "op_", 3) == 0) {
                        fprintf(out, "static inline ");
                        emit_range(out, k, body_end, loop_var, op);
                        fprintf(out, "\n");
                        break;
                    } else {
                        /* Skip nested braces as a unit; normal templates are emitted as a range below. */
                        emit_range(out, k, k + 1, loop_var, op);
                    }
                }
            }
            i = body_end;
            continue;
        }

        if (tok_is(i, "fn") && tokens[i + 1].type == TOK_IDENT) {
            char func_name[64]; strncpy(func_name, tokens[i + 1].text, sizeof(func_name) - 1); func_name[sizeof(func_name) - 1] = '\0';
            i += 2;
            int ret_start = i;
            while (i < token_count && !tok_is(i, "(")) ++i;
            if (ret_start < i && tok_is(ret_start, "->")) {
                for (int k = ret_start + 1; k < i; ++k) fprintf(out, "%s ", tokens[k].text);
            } else {
                fprintf(out, "int ");
            }
            fprintf(out, "%s", func_name);
            parse_function_params(out, i + 1, &i);
            while (i < token_count && !tok_is(i, "{")) ++i;
            if (i < token_count) fprintf(out, " {\n");
            continue;
        }

        if (tok_is(i, "let") && tokens[i + 1].type == TOK_IDENT) {
            const char *name = tokens[i + 1].text;
            int is_str = i + 3 < token_count && tok_is(i + 2, "=") && tokens[i + 3].type == TOK_STRING;
            register_symbol(name, is_str);

            if (i + 3 < token_count && tok_is(i + 2, "=") && tokens[i + 3].type == TOK_IDENT && strcmp(tokens[i + 3].text, "CPU") == 0) {
                fprintf(out, "    CPU %s = {0};\n", name);
                i += 3;
                while (i < token_count && !tok_is(i, ";")) ++i;
                continue;
            }
            fprintf(out, "    __auto_type %s = ", name);
            i += 3;
            while (i < token_count && !tok_is(i, ";")) { emit_token(out, i); ++i; }
            fprintf(out, ";\n");
            continue;
        }

        if (tok_is(i, "print")) {
            ++i;
            if (tok_is(i, "(")) ++i;
            if (tokens[i].type == TOK_STRING) {
                char content[512];
                size_t raw_len = strlen(tokens[i].text);
                size_t n = raw_len >= 2 ? raw_len - 2 : 0;
                if (n >= sizeof(content)) n = sizeof(content) - 1;
                memcpy(content, tokens[i].text + 1, n); content[n] = '\0';
                char fmt[1024] = "", args[1024] = "";
                size_t fp = 0, ap = 0;
                for (size_t p = 0; p < n; ) {
                    if (content[p] == '{') {
                        ++p; char var[64]; size_t vp = 0;
                        while (p < n && content[p] != '}' && vp + 1 < sizeof(var)) var[vp++] = content[p++];
                        var[vp] = '\0'; if (p < n && content[p] == '}') ++p;
                        if (symbol_is_string(var)) {
                            fp += (size_t)snprintf(fmt + fp, sizeof(fmt) - fp, "%%s");
                        } else {
                            fp += (size_t)snprintf(fmt + fp, sizeof(fmt) - fp, "%%lld");
                        }
                        ap += (size_t)snprintf(args + ap, sizeof(args) - ap, "%s, ", var);
                    } else {
                        if (content[p] == '%') { fmt[fp++] = '%'; fmt[fp++] = '%'; }
                        else fmt[fp++] = content[p];
                        ++p;
                    }
                }
                if (ap) args[ap - 2] = '\0';
                fmt[fp] = '\0';
                if (ap) fprintf(out, "    printf(\"%s\\n\", %s);\n", fmt, args);
                else fprintf(out, "    printf(\"%s\\n\");\n", fmt);
            }
            while (i < token_count && !tok_is(i, ";")) ++i;
            continue;
        }

        if (tok_is(i, "while") || tok_is(i, "if")) {
            bool is_while = tok_is(i, "while");
            ++i;
            char cond[512] = ""; size_t cp = 0;
            if (tok_is(i, "(")) {
                int close = find_matching_paren(i, token_count);
                for (int k = i + 1; k < close; ++k) { if (cp + strlen(tokens[k].text) + 2 < sizeof(cond)) { strcat(cond, tokens[k].text); strcat(cond, " "); cp += strlen(tokens[k].text) + 1; } }
                i = close;
            } else {
                while (i < token_count && !tok_is(i, "{")) { if (cp + strlen(tokens[i].text) + 2 < sizeof(cond)) { strcat(cond, tokens[i].text); strcat(cond, " "); cp += strlen(tokens[i].text) + 1; } ++i; }
            }
            fprintf(out, "    %s (%s) {\n", is_while ? "while" : "if", cond);
            continue;
        }

        if (tok_is(i, "return")) {
            fprintf(out, "    return "); ++i;
            while (i < token_count && !tok_is(i, ";")) { emit_token(out, i); ++i; }
            fprintf(out, ";\n");
            continue;
        }

        if (tok_is(i, "}") ) {
            if (i + 1 < token_count && tok_is(i + 1, "else")) { fprintf(out, "    } else {\n"); ++i; }
            else fprintf(out, "}\n\n");
            continue;
        }

        /* Generic statements are emitted until ';'. */
        if (tokens[i].type == TOK_IDENT || tok_is(i, "(") || tok_is(i, "*")) {
            int j = i;
            while (j < token_count && !tok_is(j, ";") && !tok_is(j, "{") && !tok_is(j, "}")) ++j;
            if (j < token_count && tok_is(j, ";")) {
                fprintf(out, "    ");
                for (int k = i; k < j; ++k) emit_token(out, k);
                fprintf(out, ";\n");
                i = j;
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.cppo>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("C*"); return 1; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return 1; }
    rewind(fp);
    char *src = (char *)malloc((size_t)size + 1);
    if (!src) { fclose(fp); return 1; }
    size_t got = fread(src, 1, (size_t)size, fp);
    fclose(fp);
    src[got] = '\0';

    lex(src);
    free(src);

    FILE *out = fopen("output.c", "w");
    if (!out) { perror("output.c"); return 1; }
    parse_and_gen(out);
    fclose(out);

    printf("[C* Compiler in C] compiled successfully, output.c generated\n");
    return 0;
}
