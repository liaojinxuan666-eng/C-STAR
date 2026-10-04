#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_TOKENS 40000
#define MAX_TEXT 256
#define MAX_SYMBOLS 1000

/*
 * C* v0.11
 *
 * The frontend parses runtime expressions into an AST and provides a small,
 * deterministic module/header import layer before parsing.
 * Comptime conditions are parsed through the same expression AST used by
 * runtime code. The evaluator accepts deterministic integer expressions and
 * feeds their values into compile-time specialization.
 */

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_SYMBOL
} TokenType;

typedef struct {
    TokenType type;
    char text[MAX_TEXT];
    int line;
} Token;

typedef struct {
    const char *name;
    int is_string;
} Symbol;

typedef enum {
    EXPR_INT,
    EXPR_STRING,
    EXPR_IDENT,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_MEMBER,
    EXPR_INDEX
} ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    union {
        long long int_val;
        char *str_val;
        char *ident;
        struct {
            char op[4];
            Expr *operand;
        } unary;
        struct {
            char op[4];
            Expr *lhs;
            Expr *rhs;
        } binary;
        struct {
            Expr *callee;
            Expr **args;
            int arg_count;
        } call;
        struct {
            Expr *base;
            char *member;
            int arrow;
        } member;
        struct {
            Expr *base;
            Expr *index;
        } index;
    } as;
};

typedef struct CType CType;

typedef enum {
    ST_BLOCK,
    ST_LET,
    ST_RETURN,
    ST_EXPR,
    ST_PRINT,
    ST_IF,
    ST_WHILE
} StmtKind;

typedef struct Stmt Stmt;

struct Stmt {
    StmtKind kind;
    Stmt *next;
    union {
        struct {
            char *name;
            int is_cpu_ctor;
            char *ctor_type;
            Expr *expr;
            int type_start;
            int type_end;
            int has_explicit_type;
            CType *inferred_type;
        } let_stmt;
        struct {
            Expr *expr;
        } raw_stmt;
        struct {
            int string_token;
        } print_stmt;
        struct {
            Expr *cond;
            Stmt *then_body;
            Stmt *else_body;
        } if_stmt;
        struct {
            Expr *cond;
            Stmt *body;
        } while_stmt;
        struct {
            Stmt *body;
        } block_stmt;
    } as;
};

typedef struct {
    char *name;
    int fields_start;
    int fields_end;
} StructDecl;

typedef struct {
    char *name;
    int members_start;
    int members_end;
} EnumDecl;

typedef struct {
    char *name;
    int return_start;
    int return_end;
    int params_start;
    int params_end;
    int is_extern;
    Stmt *body;
} FunctionDecl;

typedef struct {
    char *loop_var;
    int start_value;
    int end_value;
    int body_start;
    int body_end;
} ComptimeDecl;

typedef struct {
    StructDecl *structs;
    size_t struct_count;
    EnumDecl *enums;
    size_t enum_count;
    FunctionDecl *functions;
    size_t function_count;
    ComptimeDecl *comptimes;
    size_t comptime_count;
} Program;

typedef enum {
    TYPE_UNKNOWN,
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_INT,
    TYPE_UINT,
    TYPE_PTR,
    TYPE_STRUCT,
    TYPE_ENUM,
    TYPE_ARRAY,
    TYPE_STRING
} TypeKind;

struct CType {
    TypeKind kind;
    int bits;
    int is_signed;
    char name[128];
    CType *base;
    size_t array_count;
};

typedef struct {
    char *name;
    CType type;
} LocalSymbol;


static Token tokens[MAX_TOKENS];
static int token_count = 0;
static Symbol symbols[MAX_SYMBOLS];
static int symbol_count = 0;

static void die_at(int token_index, const char *msg) {
    int line = (token_index >= 0 && token_index < token_count) ? tokens[token_index].line : 0;
    if (line > 0) fprintf(stderr, "C* error:%d: %s\n", line, msg);
    else fprintf(stderr, "C* error: %s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die_at(-1, "out of memory");
    return p;
}

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n ? n : 1);
    if (!p) die_at(-1, "out of memory");
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static void add_token(TokenType type, const char *text, size_t len, int line) {
    if (token_count >= MAX_TOKENS - 1) die_at(token_count, "too many tokens");
    if (len >= sizeof(tokens[token_count].text)) die_at(token_count, "token too long");
    tokens[token_count].type = type;
    memcpy(tokens[token_count].text, text, len);
    tokens[token_count].text[len] = '\0';
    tokens[token_count].line = line;
    ++token_count;
}

static bool tok_is(int i, const char *s) {
    return i >= 0 && i < token_count && strcmp(tokens[i].text, s) == 0;
}

static bool is_interpolation_open(int i, int limit) {
    return i + 2 < limit && tok_is(i, "{") &&
           (tokens[i + 1].type == TOK_IDENT || tokens[i + 1].type == TOK_NUMBER) &&
           tok_is(i + 2, "}");
}

static int find_matching_brace(int open, int limit) {
    if (!tok_is(open, "{")) return limit;
    int depth = 0;
    for (int i = open; i < limit; ++i) {
        if (is_interpolation_open(i, limit)) {
            i += 2;
            continue;
        }
        if (tok_is(i, "{")) ++depth;
        else if (tok_is(i, "}")) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return limit;
}

static int find_matching_paren(int open, int limit) {
    if (!tok_is(open, "(")) return limit;
    int depth = 0;
    for (int i = open; i < limit; ++i) {
        if (tok_is(i, "(")) ++depth;
        else if (tok_is(i, ")")) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return limit;
}

static long long eval_comptime_ast(const Expr *e, const char *loop_var, long long loop_val) {
    if (!e) die_at(-1, "invalid comptime expression");

    switch (e->kind) {
        case EXPR_INT:
            return e->as.int_val;

        case EXPR_IDENT:
            if (loop_var && strcmp(e->as.ident, loop_var) == 0) return loop_val;
            die_at(-1, "unknown identifier in comptime expression");
            return 0;

        case EXPR_UNARY: {
            long long v = eval_comptime_ast(e->as.unary.operand, loop_var, loop_val);
            const char *op = e->as.unary.op;
            if (strcmp(op, "+") == 0) return v;
            if (strcmp(op, "-") == 0) return -v;
            if (strcmp(op, "!") == 0) return !v;
            if (strcmp(op, "~") == 0) return ~v;
            die_at(-1, "unsupported unary operator in comptime expression");
            return 0;
        }

        case EXPR_BINARY: {
            const char *op = e->as.binary.op;
            long long a = eval_comptime_ast(e->as.binary.lhs, loop_var, loop_val);
            long long b = eval_comptime_ast(e->as.binary.rhs, loop_var, loop_val);

            if (strcmp(op, "=") == 0) return b;
            if (strcmp(op, "+=") == 0) return a + b;
            if (strcmp(op, "-=") == 0) return a - b;
            if (strcmp(op, "*=") == 0) return a * b;
            if (strcmp(op, "/=") == 0) {
                if (b == 0) die_at(-1, "division by zero in comptime expression");
                return a / b;
            }
            if (strcmp(op, "%=") == 0) {
                if (b == 0) die_at(-1, "modulo by zero in comptime expression");
                return a % b;
            }
            if (strcmp(op, "+") == 0) return a + b;
            if (strcmp(op, "-") == 0) return a - b;
            if (strcmp(op, "*") == 0) return a * b;
            if (strcmp(op, "/") == 0) {
                if (b == 0) die_at(-1, "division by zero in comptime expression");
                return a / b;
            }
            if (strcmp(op, "%") == 0) {
                if (b == 0) die_at(-1, "modulo by zero in comptime expression");
                return a % b;
            }
            if (strcmp(op, "<<") == 0) return a << b;
            if (strcmp(op, ">>") == 0) return a >> b;
            if (strcmp(op, "&") == 0) return a & b;
            if (strcmp(op, "|") == 0) return a | b;
            if (strcmp(op, "^") == 0) return a ^ b;
            if (strcmp(op, "==") == 0) return a == b;
            if (strcmp(op, "!=") == 0) return a != b;
            if (strcmp(op, "<") == 0) return a < b;
            if (strcmp(op, ">") == 0) return a > b;
            if (strcmp(op, "<=") == 0) return a <= b;
            if (strcmp(op, ">=") == 0) return a >= b;
            if (strcmp(op, "&&") == 0) return (a != 0) && (b != 0);
            if (strcmp(op, "||") == 0) return (a != 0) || (b != 0);

            die_at(-1, "unsupported binary operator in comptime expression");
            return 0;
        }

        case EXPR_STRING:
        case EXPR_CALL:
        case EXPR_MEMBER:
        case EXPR_INDEX:
            die_at(-1, "expression is not a comptime integer expression");
            return 0;
    }

    die_at(-1, "invalid comptime expression kind");
    return 0;
}


typedef struct {
    char **done;
    size_t done_count;
    char **active;
    size_t active_count;
} ImportState;

static bool str_ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static char *read_entire_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "C* error: cannot open imported file '%s'\n", path);
        exit(1);
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); die_at(-1, "cannot seek imported file"); }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); die_at(-1, "cannot size imported file"); }
    rewind(fp);
    char *buf = (char *)xmalloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[got] = '\0';
    return buf;
}

static char *dir_of_path(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return xstrdup(".");
    if (slash == path) return xstrdup("/");
    size_t n = (size_t)(slash - path);
    char *out = (char *)xmalloc(n + 1);
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_path(const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    bool sep = a > 0 && dir[a - 1] != '/';
    char *out = (char *)xmalloc(a + (sep ? 1 : 0) + b + 1);
    memcpy(out, dir, a);
    size_t p = a;
    if (sep) out[p++] = '/';
    memcpy(out + p, name, b);
    out[p + b] = '\0';
    return out;
}

static bool import_list_has(char **list, size_t count, const char *path) {
    for (size_t i = 0; i < count; ++i)
        if (strcmp(list[i], path) == 0) return true;
    return false;
}

static void import_list_add(char ***list, size_t *count, const char *path) {
    *list = (char **)xrealloc(*list, sizeof(char *) * (*count + 1));
    (*list)[*count] = xstrdup(path);
    ++*count;
}

static void import_state_pop_active(ImportState *st, const char *path) {
    for (size_t i = 0; i < st->active_count; ++i) {
        if (strcmp(st->active[i], path) == 0) {
            free(st->active[i]);
            for (size_t j = i + 1; j < st->active_count; ++j)
                st->active[j - 1] = st->active[j];
            --st->active_count;
            return;
        }
    }
}

static void import_state_free(ImportState *st) {
    for (size_t i = 0; i < st->done_count; ++i) free(st->done[i]);
    for (size_t i = 0; i < st->active_count; ++i) free(st->active[i]);
    free(st->done);
    free(st->active);
    memset(st, 0, sizeof(*st));
}

static char *expand_imports_recursive(const char *source, const char *current_file, ImportState *st, int depth) {
    if (depth > 64) die_at(-1, "import nesting too deep");

    size_t cap = strlen(source) + 1;
    char *out = (char *)xmalloc(cap);
    size_t used = 0;
    out[0] = '\0';

    const char *line = source;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        const char *q = line;
        while ((size_t)(q - line) < len && (*q == ' ' || *q == '\t' || *q == '\r')) ++q;

        bool handled = false;
        if ((size_t)(q - line) + 6 < len && strncmp(q, "import", 6) == 0 &&
            (q[6] == ' ' || q[6] == '\t')) {
            const char *arg = q + 6;
            while ((size_t)(arg - line) < len && (*arg == ' ' || *arg == '\t')) ++arg;
            char name[1024];
            size_t nn = 0;
            bool angle = false, quoted = false;
            if ((size_t)(arg - line) < len && *arg == '<') {
                angle = true;
                ++arg;
                while ((size_t)(arg - line) < len && *arg != '>' && nn + 1 < sizeof(name)) name[nn++] = *arg++;
                if ((size_t)(arg - line) >= len || *arg != '>') die_at(-1, "unterminated import header");
            } else if ((size_t)(arg - line) < len && *arg == '"') {
                quoted = true;
                ++arg;
                while ((size_t)(arg - line) < len && *arg != '"' && nn + 1 < sizeof(name)) name[nn++] = *arg++;
                if ((size_t)(arg - line) >= len || *arg != '"') die_at(-1, "unterminated import path");
            }
            if (angle || quoted) {
                name[nn] = '\0';
                const char *tail = arg + 1;
                while ((size_t)(tail - line) < len && (*tail == ' ' || *tail == '\t' || *tail == '\r')) ++tail;
                if ((size_t)(tail - line) < len && *tail == ';') {
                    handled = true;
                    if (angle || !str_ends_with(name, ".cppo")) {
                        size_t need = strlen("include ") + nn + 8;
                        char *piece = (char *)xmalloc(need);
                        if (angle) snprintf(piece, need, "include <%s>\n", name);
                        else snprintf(piece, need, "include \"%s\"\n", name);
                        size_t pl = strlen(piece);
                        if (used + pl + 1 > cap) {
                            while (used + pl + 1 > cap) cap *= 2;
                            out = (char *)xrealloc(out, cap);
                        }
                        memcpy(out + used, piece, pl); used += pl; out[used] = '\0';
                        free(piece);
                    } else {
                        char *dir = dir_of_path(current_file);
                        char *joined = join_path(dir, name);
                        free(dir);
                        if (import_list_has(st->active, st->active_count, joined)) {
                            free(joined);
                            die_at(-1, "cyclic C* import");
                        }
                        if (!import_list_has(st->done, st->done_count, joined)) {
                            char *child = read_entire_file(joined);
                            import_list_add(&st->active, &st->active_count, joined);
                            char *expanded = expand_imports_recursive(child, joined, st, depth + 1);
                            free(child);
                            import_state_pop_active(st, joined);
                            import_list_add(&st->done, &st->done_count, joined);
                            size_t el = strlen(expanded);
                            if (used + el + 1 > cap) {
                                while (used + el + 1 > cap) cap *= 2;
                                out = (char *)xrealloc(out, cap);
                            }
                            memcpy(out + used, expanded, el); used += el; out[used] = '\0';
                            free(expanded);
                        }
                        free(joined);
                    }
                }
            }
        }

        if (!handled) {
            if (used + len + 2 > cap) {
                while (used + len + 2 > cap) cap *= 2;
                out = (char *)xrealloc(out, cap);
            }
            memcpy(out + used, line, len); used += len;
            out[used++] = '\n'; out[used] = '\0';
        }

        line = nl ? nl + 1 : line + len;
        if (!nl) break;
    }
    return out;
}

static void lex(const char *src) {
    token_count = 0;
    int line = 1;

    for (size_t i = 0; src[i] != '\0'; ) {
        unsigned char c = (unsigned char)src[i];

        if (src[i] == '/' && src[i + 1] == '/') {
            i += 2;
            while (src[i] && src[i] != '\n') ++i;
            continue;
        }
        if (src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') ++line;
                ++i;
            }
            if (src[i]) i += 2;
            continue;
        }
        if (src[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (isspace(c)) {
            ++i;
            continue;
        }

        if (src[i] == '"') {
            size_t start = i++;
            while (src[i] && src[i] != '"') {
                if (src[i] == '\\' && src[i + 1]) i += 2;
                else {
                    if (src[i] == '\n') ++line;
                    ++i;
                }
            }
            if (src[i] == '"') ++i;
            add_token(TOK_STRING, src + start, i - start, line);
            continue;
        }

        if (isalpha(c) || src[i] == '_') {
            size_t start = i++;
            while (isalnum((unsigned char)src[i]) || src[i] == '_') ++i;
            add_token(TOK_IDENT, src + start, i - start, line);
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
            add_token(TOK_NUMBER, src + start, i - start, line);
            continue;
        }

        const char *two[] = {"==", "!=", "<=", ">=", "->", "<<", ">>", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="};
        bool matched = false;
        for (size_t k = 0; k < sizeof(two) / sizeof(two[0]); ++k) {
            if (src[i] == two[k][0] && src[i + 1] == two[k][1]) {
                add_token(TOK_SYMBOL, two[k], 2, line);
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        add_token(TOK_SYMBOL, src + i, 1, line);
        ++i;
    }

    tokens[token_count].type = TOK_EOF;
    strcpy(tokens[token_count].text, "EOF");
    tokens[token_count].line = line;
}

static Expr *new_expr(ExprKind kind) {
    Expr *e = (Expr *)xmalloc(sizeof(*e));
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    return e;
}

static void free_type(CType *t);

static void free_expr(Expr *expr) {
    if (!expr) return;
    switch (expr->kind) {
        case EXPR_INT:
            break;
        case EXPR_STRING:
            free(expr->as.str_val);
            break;
        case EXPR_IDENT:
            free(expr->as.ident);
            break;
        case EXPR_UNARY:
            free_expr(expr->as.unary.operand);
            break;
        case EXPR_BINARY:
            free_expr(expr->as.binary.lhs);
            free_expr(expr->as.binary.rhs);
            break;
        case EXPR_CALL:
            free_expr(expr->as.call.callee);
            for (int i = 0; i < expr->as.call.arg_count; ++i) free_expr(expr->as.call.args[i]);
            free(expr->as.call.args);
            break;
        case EXPR_MEMBER:
            free_expr(expr->as.member.base);
            free(expr->as.member.member);
            break;
        case EXPR_INDEX:
            free_expr(expr->as.index.base);
            free_expr(expr->as.index.index);
            break;
    }
    free(expr);
}

static void free_stmt_list(Stmt *stmt) {
    while (stmt) {
        Stmt *next = stmt->next;
        switch (stmt->kind) {
            case ST_LET:
                free(stmt->as.let_stmt.name);
                free(stmt->as.let_stmt.ctor_type);
                free_expr(stmt->as.let_stmt.expr);
                if (stmt->as.let_stmt.inferred_type) {
                    free_type(stmt->as.let_stmt.inferred_type);
                    free(stmt->as.let_stmt.inferred_type);
                }
                break;
            case ST_RETURN:
            case ST_EXPR:
                free_expr(stmt->as.raw_stmt.expr);
                break;
            case ST_PRINT:
                break;
            case ST_IF:
                free_expr(stmt->as.if_stmt.cond);
                free_stmt_list(stmt->as.if_stmt.then_body);
                free_stmt_list(stmt->as.if_stmt.else_body);
                break;
            case ST_WHILE:
                free_expr(stmt->as.while_stmt.cond);
                free_stmt_list(stmt->as.while_stmt.body);
                break;
            case ST_BLOCK:
                free_stmt_list(stmt->as.block_stmt.body);
                break;
        }
        free(stmt);
        stmt = next;
    }
}

static Stmt *new_stmt(StmtKind kind) {
    Stmt *s = (Stmt *)xmalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    return s;
}

static void append_stmt(Stmt **head, Stmt **tail, Stmt *s) {
    if (!*head) *head = s;
    else (*tail)->next = s;
    *tail = s;
}

typedef struct {
    int pos;
    int end;
} ExprParser;

static bool token_is_binary_operator(int i) {
    if (i < 0 || i >= token_count) return false;
    static const char *ops[] = {
        "=", "+=", "-=", "*=", "/=", "%=",
        "||", "&&", "|", "^", "&", "==", "!=",
        "<", ">", "<=", ">=", "<<", ">>", "+", "-", "*", "/", "%"
    };
    for (size_t k = 0; k < sizeof(ops) / sizeof(ops[0]); ++k)
        if (strcmp(tokens[i].text, ops[k]) == 0) return true;
    return false;
}

static int binary_precedence(const char *op) {
    if (strcmp(op, "=") == 0 || strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 ||
        strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0 || strcmp(op, "%=") == 0) return 1;
    if (strcmp(op, "||") == 0) return 2;
    if (strcmp(op, "&&") == 0) return 3;
    if (strcmp(op, "|") == 0) return 4;
    if (strcmp(op, "^") == 0) return 5;
    if (strcmp(op, "&") == 0) return 6;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) return 7;
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) return 8;
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) return 9;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) return 10;
    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) return 11;
    return 0;
}

static Expr *parse_expression_bp(ExprParser *p, int min_bp);

static Expr *parse_primary(ExprParser *p) {
    if (p->pos >= p->end) die_at(p->pos, "expected expression");

    int i = p->pos;
    Expr *e = NULL;

    if (tokens[i].type == TOK_NUMBER) {
        e = new_expr(EXPR_INT);
        e->as.int_val = strtoll(tokens[i].text, NULL, 0);
        ++p->pos;
    } else if (tokens[i].type == TOK_STRING) {
        e = new_expr(EXPR_STRING);
        e->as.str_val = xstrdup(tokens[i].text);
        ++p->pos;
    } else if (tokens[i].type == TOK_IDENT) {
        e = new_expr(EXPR_IDENT);
        e->as.ident = xstrdup(tokens[i].text);
        ++p->pos;
    } else if (tok_is(i, "(")) {
        ++p->pos;
        e = parse_expression_bp(p, 1);
        if (!tok_is(p->pos, ")")) {
            free_expr(e);
            die_at(p->pos, "expected ')' after expression");
        }
        ++p->pos;
    } else {
        die_at(i, "expected expression");
    }

    for (;;) {
        if (p->pos >= p->end) break;

        if (tok_is(p->pos, ".") || tok_is(p->pos, "->")) {
            int arrow = tok_is(p->pos, "->") ? 1 : 0;
            ++p->pos;
            if (p->pos >= p->end || tokens[p->pos].type != TOK_IDENT) {
                free_expr(e);
                die_at(p->pos, "expected member name after '.' or '->'");
            }
            Expr *m = new_expr(EXPR_MEMBER);
            m->as.member.base = e;
            m->as.member.member = xstrdup(tokens[p->pos].text);
            m->as.member.arrow = arrow;
            e = m;
            ++p->pos;
            continue;
        }

        if (tok_is(p->pos, "(")) {
            ++p->pos;
            Expr **args = NULL;
            int argc = 0;
            if (!tok_is(p->pos, ")")) {
                for (;;) {
                    Expr *arg = parse_expression_bp(p, 1);
                    args = (Expr **)xrealloc(args, sizeof(*args) * (size_t)(argc + 1));
                    args[argc++] = arg;
                    if (tok_is(p->pos, ",")) {
                        ++p->pos;
                        continue;
                    }
                    break;
                }
            }
            if (!tok_is(p->pos, ")")) {
                free_expr(e);
                for (int k = 0; k < argc; ++k) free_expr(args[k]);
                free(args);
                die_at(p->pos, "expected ')' after call arguments");
            }
            ++p->pos;
            Expr *call = new_expr(EXPR_CALL);
            call->as.call.callee = e;
            call->as.call.args = args;
            call->as.call.arg_count = argc;
            e = call;
            continue;
        }

        if (tok_is(p->pos, "[")) {
            ++p->pos;
            Expr *index = parse_expression_bp(p, 1);
            if (!tok_is(p->pos, "]")) {
                free_expr(e);
                free_expr(index);
                die_at(p->pos, "expected ']' after index expression");
            }
            ++p->pos;
            Expr *ix = new_expr(EXPR_INDEX);
            ix->as.index.base = e;
            ix->as.index.index = index;
            e = ix;
            continue;
        }

        break;
    }

    return e;
}

static Expr *parse_unary(ExprParser *p) {
    if (p->pos < p->end) {
        const char *op = tokens[p->pos].text;
        if (strcmp(op, "&") == 0 || strcmp(op, "*") == 0 || strcmp(op, "+") == 0 ||
            strcmp(op, "-") == 0 || strcmp(op, "!") == 0 || strcmp(op, "~") == 0) {
            ++p->pos;
            Expr *e = new_expr(EXPR_UNARY);
            snprintf(e->as.unary.op, sizeof(e->as.unary.op), "%s", op);
            e->as.unary.operand = parse_unary(p);
            return e;
        }
    }
    return parse_primary(p);
}

static Expr *parse_expression_bp(ExprParser *p, int min_bp) {
    Expr *lhs = parse_unary(p);

    while (p->pos < p->end && token_is_binary_operator(p->pos)) {
        const char *op = tokens[p->pos].text;
        int prec = binary_precedence(op);
        if (prec < min_bp) break;
        bool right_assoc = prec == 1;
        ++p->pos;
        Expr *rhs = parse_expression_bp(p, right_assoc ? prec : prec + 1);
        Expr *b = new_expr(EXPR_BINARY);
        snprintf(b->as.binary.op, sizeof(b->as.binary.op), "%s", op);
        b->as.binary.lhs = lhs;
        b->as.binary.rhs = rhs;
        lhs = b;
    }

    return lhs;
}

static Expr *parse_expr_range(int start, int end) {
    if (start >= end) die_at(start, "expected expression");
    ExprParser p;
    p.pos = start;
    p.end = end;
    Expr *e = parse_expression_bp(&p, 1);
    if (p.pos != end) {
        free_expr(e);
        die_at(p.pos, "unexpected token after expression");
    }
    return e;
}

static int scan_until_statement_end(int i, int end) {
    int paren = 0, bracket = 0;
    while (i < end) {
        if (tok_is(i, "(")) ++paren;
        else if (tok_is(i, ")") && paren > 0) --paren;
        else if (tok_is(i, "[")) ++bracket;
        else if (tok_is(i, "]") && bracket > 0) --bracket;
        if (paren == 0 && bracket == 0 && tok_is(i, ";")) break;
        if (paren == 0 && bracket == 0 && tok_is(i, "}")) break;
        ++i;
    }
    return i;
}

static Stmt *parse_block(int open, int close);

static Stmt *parse_statements(int start, int end) {
    Stmt *head = NULL, *tail = NULL;
    int i = start;

    while (i < end) {
        if (tok_is(i, ";")) {
            ++i;
            continue;
        }

        if (tok_is(i, "let") && i + 1 < end && tokens[i + 1].type == TOK_IDENT) {
            Stmt *s = new_stmt(ST_LET);
            s->as.let_stmt.name = xstrdup(tokens[i + 1].text);
            s->as.let_stmt.is_cpu_ctor = false;
            s->as.let_stmt.ctor_type = NULL;
            s->as.let_stmt.type_start = s->as.let_stmt.type_end = -1;
            s->as.let_stmt.has_explicit_type = false;
            s->as.let_stmt.inferred_type = NULL;
            i += 2;
            if (tok_is(i, ":")) {
                ++i;
                s->as.let_stmt.has_explicit_type = true;
                s->as.let_stmt.type_start = i;
                while (i < end && !tok_is(i, "=")) ++i;
                s->as.let_stmt.type_end = i;
            }
            if (!tok_is(i, "=")) die_at(i, "expected '=' after let declaration");
            ++i;
            int expr_start = i;
            int expr_end = scan_until_statement_end(i, end);
            if (expr_end == expr_start) die_at(i, "expected initializer expression");

            /* `let value: Type = Type()` is a zero-initialized C* struct constructor.
             * Keep ordinary zero-argument function calls as expressions.
             * The legacy `let cpu = CPU()` form remains supported for names that
             * look like type names (leading uppercase).
             */
            bool ctor_candidate = tokens[expr_start].type == TOK_IDENT &&
                                  expr_end == expr_start + 3 &&
                                  tok_is(expr_start + 1, "(") && tok_is(expr_start + 2, ")");
            bool ctor_match = false;
            if (ctor_candidate) {
                if (s->as.let_stmt.has_explicit_type &&
                    s->as.let_stmt.type_start < s->as.let_stmt.type_end &&
                    tokens[s->as.let_stmt.type_start].type == TOK_IDENT &&
                    strcmp(tokens[s->as.let_stmt.type_start].text, tokens[expr_start].text) == 0) {
                    ctor_match = true;
                } else if (!s->as.let_stmt.has_explicit_type &&
                           isupper((unsigned char)tokens[expr_start].text[0])) {
                    ctor_match = true;
                }
            }
            if (ctor_match) {
                s->as.let_stmt.is_cpu_ctor = true;
                s->as.let_stmt.ctor_type = xstrdup(tokens[expr_start].text);
            } else {
                s->as.let_stmt.expr = parse_expr_range(expr_start, expr_end);
            }
            append_stmt(&head, &tail, s);
            i = expr_end;
            if (i < end && tok_is(i, ";")) ++i;
            continue;
        }

        if (tok_is(i, "print")) {
            Stmt *s = new_stmt(ST_PRINT);
            ++i;
            if (tok_is(i, "(")) ++i;
            if (i >= end || tokens[i].type != TOK_STRING) die_at(i, "print expects a string literal");
            s->as.print_stmt.string_token = i++;
            if (tok_is(i, ")")) ++i;
            if (tok_is(i, ";")) ++i;
            append_stmt(&head, &tail, s);
            continue;
        }

        if (tok_is(i, "return")) {
            Stmt *s = new_stmt(ST_RETURN);
            ++i;
            int expr_start = i;
            int expr_end = scan_until_statement_end(i, end);
            if (expr_end == expr_start) die_at(i, "expected return expression");
            s->as.raw_stmt.expr = parse_expr_range(expr_start, expr_end);
            append_stmt(&head, &tail, s);
            i = expr_end;
            if (i < end && tok_is(i, ";")) ++i;
            continue;
        }

        if (tok_is(i, "if") || tok_is(i, "while")) {
            bool is_while = tok_is(i, "while");
            Stmt *s = new_stmt(is_while ? ST_WHILE : ST_IF);
            ++i;

            int cond_start, cond_end, body_open;
            if (tok_is(i, "(")) {
                int cond_open = i;
                int cond_close = find_matching_paren(cond_open, end);
                if (cond_close >= end) die_at(i, "unterminated condition");
                cond_start = cond_open + 1;
                cond_end = cond_close;
                body_open = cond_close + 1;
            } else {
                cond_start = i;
                int paren_depth = 0;
                while (i < end) {
                    if (tok_is(i, "(")) ++paren_depth;
                    else if (tok_is(i, ")") && paren_depth > 0) --paren_depth;
                    if (paren_depth == 0 && tok_is(i, "{")) break;
                    ++i;
                }
                cond_end = i;
                body_open = i;
            }

            if (body_open >= end || !tok_is(body_open, "{")) die_at(body_open, "expected '{' after condition");
            Expr *cond = parse_expr_range(cond_start, cond_end);
            i = body_open;
            int body_close = find_matching_brace(body_open, end);
            if (body_close >= end) {
                free_expr(cond);
                die_at(i, "unterminated control-flow block");
            }
            if (is_while) {
                s->as.while_stmt.cond = cond;
                s->as.while_stmt.body = parse_block(body_open, body_close);
            } else {
                s->as.if_stmt.cond = cond;
                s->as.if_stmt.then_body = parse_block(body_open, body_close);
            }
            i = body_close + 1;

            if (!is_while && tok_is(i, "else")) {
                ++i;
                if (!tok_is(i, "{")) die_at(i, "expected '{' after else");
                int else_open = i;
                int else_close = find_matching_brace(else_open, end);
                if (else_close >= end) die_at(i, "unterminated else block");
                s->as.if_stmt.else_body = parse_block(else_open, else_close);
                i = else_close + 1;
            }
            append_stmt(&head, &tail, s);
            continue;
        }

        if (tok_is(i, "{")) {
            int close = find_matching_brace(i, end);
            if (close >= end) die_at(i, "unterminated block");
            Stmt *s = new_stmt(ST_BLOCK);
            s->as.block_stmt.body = parse_block(i, close);
            append_stmt(&head, &tail, s);
            i = close + 1;
            continue;
        }

        {
            int begin = i;
            int expr_end = scan_until_statement_end(i, end);
            if (expr_end == begin) die_at(i, "unexpected token in statement");
            Stmt *s = new_stmt(ST_EXPR);
            s->as.raw_stmt.expr = parse_expr_range(begin, expr_end);
            append_stmt(&head, &tail, s);
            i = expr_end;
            if (i < end && tok_is(i, ";")) ++i;
        }
    }

    return head;
}

static Stmt *parse_block(int open, int close) {
    return parse_statements(open + 1, close);
}

static void program_add_struct(Program *p, StructDecl d) {
    p->structs = (StructDecl *)xrealloc(p->structs, sizeof(*p->structs) * (p->struct_count + 1));
    p->structs[p->struct_count++] = d;
}

static void program_add_enum(Program *p, EnumDecl d) {
    p->enums = (EnumDecl *)xrealloc(p->enums, sizeof(*p->enums) * (p->enum_count + 1));
    p->enums[p->enum_count++] = d;
}

static void program_add_function(Program *p, FunctionDecl d) {
    p->functions = (FunctionDecl *)xrealloc(p->functions, sizeof(*p->functions) * (p->function_count + 1));
    p->functions[p->function_count++] = d;
}

static void program_add_comptime(Program *p, ComptimeDecl d) {
    p->comptimes = (ComptimeDecl *)xrealloc(p->comptimes, sizeof(*p->comptimes) * (p->comptime_count + 1));
    p->comptimes[p->comptime_count++] = d;
}

static Program parse_program(void) {
    Program p;
    memset(&p, 0, sizeof(p));

    for (int i = 0; i < token_count && tokens[i].type != TOK_EOF; ) {
        if (tok_is(i, "include")) {
            /* Includes are handled directly by the backend; skip them here. */
            ++i;
            if (tok_is(i, "<")) {
                while (i < token_count && !tok_is(i, ">")) ++i;
                if (i < token_count) ++i;
            } else if (i < token_count && tokens[i].type == TOK_STRING) ++i;
            continue;
        }

        if (tok_is(i, "module")) {
            ++i;
            if (i >= token_count || tokens[i].type != TOK_IDENT) die_at(i, "expected module name");
            ++i;
            while (i < token_count && (tok_is(i, ".") || tokens[i].type == TOK_IDENT)) {
                if (tok_is(i, ".")) {
                    ++i;
                    if (i >= token_count || tokens[i].type != TOK_IDENT) die_at(i, "expected identifier after module '.'");
                } else {
                    ++i;
                }
                if (i < token_count && tok_is(i, ";")) break;
            }
            if (!tok_is(i, ";")) die_at(i, "expected ';' after module declaration");
            ++i;
            continue;
        }

        if (tok_is(i, "enum")) {
            if (i + 1 >= token_count || tokens[i + 1].type != TOK_IDENT)
                die_at(i, "expected enum name");
            EnumDecl d;
            d.name = xstrdup(tokens[i + 1].text);
            int open = i + 2;
            if (!tok_is(open, "{")) die_at(open, "expected '{' after enum name");
            int close = find_matching_brace(open, token_count);
            if (close >= token_count) die_at(open, "unterminated enum");
            d.members_start = open + 1;
            d.members_end = close;
            program_add_enum(&p, d);
            i = close + 1;
            if (tok_is(i, ";")) ++i;
            continue;
        }

        if (tok_is(i, "struct")) {
            if (i + 1 >= token_count || tokens[i + 1].type != TOK_IDENT) die_at(i, "expected struct name");
            StructDecl d;
            d.name = xstrdup(tokens[i + 1].text);
            int open = i + 2;
            if (!tok_is(open, "{")) die_at(open, "expected '{' after struct name");
            int close = find_matching_brace(open, token_count);
            if (close >= token_count) die_at(open, "unterminated struct");
            d.fields_start = open + 1;
            d.fields_end = close;
            program_add_struct(&p, d);
            i = close + 1;
            if (tok_is(i, ";")) ++i;
            continue;
        }

        if (tok_is(i, "comptime")) {
            int j = i + 1;
            if (!tok_is(j, "for")) die_at(j, "expected 'for' after comptime");
            ++j;
            if (tok_is(j, "(")) ++j;
            if (j >= token_count || tokens[j].type != TOK_IDENT) die_at(j, "expected comptime loop variable");
            ComptimeDecl d;
            d.loop_var = xstrdup(tokens[j].text);
            ++j;
            if (!tok_is(j, "in")) die_at(j, "expected 'in' in comptime for");
            ++j;
            if (tokens[j].type != TOK_NUMBER) die_at(j, "expected comptime start value");
            d.start_value = (int)strtol(tokens[j].text, NULL, 0);
            ++j;
            if (!tok_is(j, "to")) die_at(j, "expected 'to' in comptime for");
            ++j;
            if (tokens[j].type != TOK_NUMBER) die_at(j, "expected comptime end value");
            d.end_value = (int)strtol(tokens[j].text, NULL, 0);
            ++j;
            if (tok_is(j, ")")) ++j;
            if (!tok_is(j, "{")) die_at(j, "expected '{' after comptime for");
            d.body_start = j;
            d.body_end = find_matching_brace(j, token_count);
            if (d.body_end >= token_count) die_at(j, "unterminated comptime block");
            program_add_comptime(&p, d);
            i = d.body_end + 1;
            continue;
        }

        if (tok_is(i, "extern") && tok_is(i + 1, "fn")) {
            i += 1;
        }

        if (tok_is(i, "fn")) {
            int is_extern = false;
            if (i > 0 && tok_is(i - 1, "extern")) is_extern = true;
            if (i + 1 >= token_count || tokens[i + 1].type != TOK_IDENT) die_at(i, "expected function name");
            FunctionDecl d;
            memset(&d, 0, sizeof(d));
            d.name = xstrdup(tokens[i + 1].text);
            d.is_extern = is_extern;
            int j = i + 2;
            while (j < token_count && !tok_is(j, "(")) ++j;
            if (j >= token_count) die_at(i, "expected '(' in function declaration");
            int param_open = j;
            int param_close = find_matching_paren(param_open, token_count);
            if (param_close >= token_count) die_at(param_open, "unterminated parameter list");
            d.params_start = param_open + 1;
            d.params_end = param_close;

            /* C* currently spells the return type after the parameter list: fn f(...) -> int. */
            j = param_close + 1;
            if (tok_is(j, "->")) {
                d.return_start = j + 1;
                j += 1;
                if (is_extern) {
                    while (j < token_count && !tok_is(j, ";")) ++j;
                } else {
                    while (j < token_count && !tok_is(j, "{")) ++j;
                }
                d.return_end = j;
            } else {
                d.return_start = d.return_end = j;
            }
            if (is_extern) {
                if (!tok_is(j, ";")) die_at(j, "expected ';' after extern function declaration");
                d.body = NULL;
                program_add_function(&p, d);
                i = j + 1;
                continue;
            }

            if (!tok_is(j, "{")) die_at(j, "expected '{' after function declaration");
            int body_close = find_matching_brace(j, token_count);
            if (body_close >= token_count) die_at(j, "unterminated function body");
            d.body = parse_block(j, body_close);
            program_add_function(&p, d);
            i = body_close + 1;
            continue;
        }

        die_at(i, "unexpected top-level token");
    }

    return p;
}

static void free_program(Program *p) {
    for (size_t i = 0; i < p->struct_count; ++i) free(p->structs[i].name);
    for (size_t i = 0; i < p->enum_count; ++i) free(p->enums[i].name);
    for (size_t i = 0; i < p->function_count; ++i) {
        free(p->functions[i].name);
        free_stmt_list(p->functions[i].body);
    }
    for (size_t i = 0; i < p->comptime_count; ++i) free(p->comptimes[i].loop_var);
    free(p->structs);
    free(p->enums);
    free(p->functions);
    free(p->comptimes);
    memset(p, 0, sizeof(*p));
}

static void emit_token(FILE *out, int i) {
    if (tokens[i].type == TOK_SYMBOL) fprintf(out, "%s", tokens[i].text);
    else fprintf(out, "%s ", tokens[i].text);
}

static void emit_range(FILE *out, int start, int end, int loop_val) {
    for (int i = start; i < end; ++i) {
        if (is_interpolation_open(i, end)) {
            fprintf(out, "%d", loop_val);
            i += 2;
            continue;
        }
        if (tokens[i].type == TOK_IDENT && i + 3 < end && tok_is(i + 1, "{") &&
            (tokens[i + 2].type == TOK_IDENT || tokens[i + 2].type == TOK_NUMBER) && tok_is(i + 3, "}") &&
            tokens[i].text[0] != '\0' && tokens[i].text[strlen(tokens[i].text) - 1] == '_') {
            fprintf(out, "%s%d", tokens[i].text, loop_val);
            i += 3;
            continue;
        }
        emit_token(out, i);
    }
}

static int emit_comptime_if(FILE *out, int if_idx, int body_end, const char *loop_var, int loop_val) {
    int cond_start = if_idx + 1;
    int cond_end = cond_start;
    int then_open = -1;

    /* C* accepts both `if (expr) {}` and the compact comptime form `if expr {}`. */
    if (tok_is(cond_start, "(")) {
        int cond_close = find_matching_paren(cond_start, body_end);
        if (cond_close >= body_end) return if_idx;
        /* Keep the outer parentheses in the AST range. */
        cond_end = cond_close + 1;
        then_open = cond_close + 1;
    } else {
        while (cond_end < body_end && !tok_is(cond_end, "{")) ++cond_end;
        then_open = cond_end;
    }
    if (then_open < 0 || then_open >= body_end) return if_idx;
    if (then_open >= body_end) return if_idx;
    int then_close = find_matching_brace(then_open, body_end);
    if (then_close >= body_end) return if_idx;

    int next = then_close + 1;
    int else_open = -1;
    int else_close = -1;
    if (next < body_end && tok_is(next, "else")) {
        int candidate = next + 1;
        if (tok_is(candidate, "{")) {
            else_open = candidate;
            else_close = find_matching_brace(else_open, body_end);
        }
    }

    Expr *cond_ast = parse_expr_range(cond_start, cond_end);
    long long result = eval_comptime_ast(cond_ast, loop_var, loop_val);
    free_expr(cond_ast);
    int start = result ? then_open + 1 : (else_open >= 0 ? else_open + 1 : -1);
    int end = result ? then_close : else_close;

    if (start >= 0 && end > start) {
        if (tok_is(start, "int") && start + 1 < end && strncmp(tokens[start + 1].text, "op_", 3) == 0)
            fprintf(out, "static inline CSTAR_UNUSED ");
        emit_range(out, start, end, loop_val);
        fprintf(out, "\n");
    }

    return else_close >= 0 ? else_close : then_close;
}

static void emit_comptime(FILE *out, const ComptimeDecl *d) {
    for (int value = d->start_value; value < d->end_value; ++value) {
        int handled_if = 0;
        for (int k = d->body_start + 1; k < d->body_end; ++k) {
            if (tok_is(k, "if")) {
                int last = emit_comptime_if(out, k, d->body_end, d->loop_var, value);
                k = last;
                handled_if = 1;
            } else if (tok_is(k, "int") && k + 1 < d->body_end && strncmp(tokens[k + 1].text, "op_", 3) == 0) {
                fprintf(out, "static inline CSTAR_UNUSED ");
                emit_range(out, k, d->body_end, value);
                fprintf(out, "\n");
                handled_if = 1;
                break;
            }
        }
        (void)handled_if;
    }
}

static void register_symbol(const char *name, int is_string) {
    for (int i = 0; i < symbol_count; ++i) {
        if (strcmp(symbols[i].name, name) == 0) {
            symbols[i].is_string = is_string;
            return;
        }
    }
    if (symbol_count >= MAX_SYMBOLS) return;
    symbols[symbol_count].name = xstrdup(name);
    symbols[symbol_count].is_string = is_string;
    ++symbol_count;
}

static int symbol_is_string(const char *name) {
    for (int i = 0; i < symbol_count; ++i)
        if (strcmp(symbols[i].name, name) == 0) return symbols[i].is_string;
    return 0;
}

static void clear_symbols(void) {
    for (int i = 0; i < symbol_count; ++i) free((void *)symbols[i].name);
    symbol_count = 0;
}

static void emit_expr(FILE *out, const Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_INT:
            fprintf(out, "%lld", e->as.int_val);
            break;
        case EXPR_STRING:
            fprintf(out, "%s", e->as.str_val);
            break;
        case EXPR_IDENT:
            fprintf(out, "%s", e->as.ident);
            break;
        case EXPR_UNARY:
            fprintf(out, "%s", e->as.unary.op);
            emit_expr(out, e->as.unary.operand);
            break;
        case EXPR_BINARY:
            fputc('(', out);
            emit_expr(out, e->as.binary.lhs);
            fprintf(out, " %s ", e->as.binary.op);
            emit_expr(out, e->as.binary.rhs);
            fputc(')', out);
            break;
        case EXPR_CALL:
            emit_expr(out, e->as.call.callee);
            fputc('(', out);
            for (int i = 0; i < e->as.call.arg_count; ++i) {
                if (i) fputs(", ", out);
                emit_expr(out, e->as.call.args[i]);
            }
            fputc(')', out);
            break;
        case EXPR_MEMBER:
            emit_expr(out, e->as.member.base);
            fputs(e->as.member.arrow ? "->" : ".", out);
            fprintf(out, "%s", e->as.member.member);
            break;
        case EXPR_INDEX:
            emit_expr(out, e->as.index.base);
            fputc('[', out);
            emit_expr(out, e->as.index.index);
            fputc(']', out);
            break;
    }
}

static void emit_print(FILE *out, int string_token) {
    const char *raw = tokens[string_token].text;
    size_t len = strlen(raw);
    if (len < 2) {
        fprintf(out, "    printf(\"\\n\");\n");
        return;
    }

    char content[1024];
    size_t n = len - 2;
    if (n >= sizeof(content)) n = sizeof(content) - 1;
    memcpy(content, raw + 1, n);
    content[n] = '\0';

    char fmt[2048] = "";
    char args[2048] = "";
    size_t fp = 0, ap = 0;

    for (size_t p = 0; p < n; ) {
        if (content[p] == '{') {
            ++p;
            char var[256];
            size_t vp = 0;
            while (p < n && content[p] != '}' && vp + 1 < sizeof(var)) var[vp++] = content[p++];
            var[vp] = '\0';
            if (p < n && content[p] == '}') ++p;

            bool is_str = symbol_is_string(var) != 0;
            fp += (size_t)snprintf(fmt + fp, sizeof(fmt) - fp, is_str ? "%%s" : "%%lld");
            ap += (size_t)snprintf(args + ap, sizeof(args) - ap, is_str ? "%s, " : "(long long)%s, ", var);
        } else if (content[p] == '%') {
            if (fp + 2 < sizeof(fmt)) { fmt[fp++] = '%'; fmt[fp++] = '%'; }
            ++p;
        } else {
            if (fp + 1 < sizeof(fmt)) fmt[fp++] = content[p];
            ++p;
        }
    }

    fmt[fp] = '\0';
    if (ap) args[ap - 2] = '\0';
    if (ap) fprintf(out, "    printf(\"%s\\n\", %s);\n", fmt, args);
    else fprintf(out, "    printf(\"%s\\n\");\n", fmt);
}

static void emit_c_type_tokens(FILE *out, int start, int end);

static void emit_c_type_value(FILE *out, const CType *t) {
    if (!t) { fputs("int", out); return; }
    switch (t->kind) {
        case TYPE_VOID: fputs("void", out); break;
        case TYPE_BOOL: fputs("bool", out); break;
        case TYPE_INT:
            if (t->name[0]) fputs(t->name, out);
            else if (t->bits == 64) fputs("i64", out);
            else fputs("int", out);
            break;
        case TYPE_UINT:
            if (t->name[0]) fputs(t->name, out);
            else if (t->bits == 8) fputs("u8", out);
            else if (t->bits == 16) fputs("u16", out);
            else if (t->bits == 32) fputs("u32", out);
            else fputs("u64", out);
            break;
        case TYPE_STRING: fputs("const char *", out); break;
        case TYPE_STRUCT: fputs(t->name, out); break;
        case TYPE_ENUM: fputs(t->name, out); break;
        case TYPE_ARRAY:
            emit_c_type_value(out, t->base);
            break;
        case TYPE_PTR:
            emit_c_type_value(out, t->base);
            fputs(" *", out);
            break;
        default: fputs("int", out); break;
    }
}

static void emit_c_decl(FILE *out, const CType *t, const char *name) {
    if (!t) { fprintf(out, "int %s", name); return; }
    if (t->kind == TYPE_ARRAY) {
        emit_c_decl(out, t->base, name);
        fprintf(out, "[%zu]", t->array_count);
        return;
    }
    if (t->kind == TYPE_PTR) {
        emit_c_type_value(out, t->base);
        fprintf(out, " *%s", name);
        return;
    }
    emit_c_type_value(out, t);
    fprintf(out, " %s", name);
}

static void emit_stmt_list(FILE *out, const Stmt *stmt, int indent);

static void emit_indent(FILE *out, int indent) {
    for (int i = 0; i < indent; ++i) fputs("    ", out);
}

static void emit_stmt_list(FILE *out, const Stmt *stmt, int indent) {
    for (Stmt *s = (Stmt *)stmt; s; s = s->next) {
        switch (s->kind) {
            case ST_LET:
                emit_indent(out, indent);
                if (s->as.let_stmt.is_cpu_ctor) {
                    fprintf(out, "%s %s = {0};\n", s->as.let_stmt.ctor_type ? s->as.let_stmt.ctor_type : "CPU", s->as.let_stmt.name);
                } else if (s->as.let_stmt.has_explicit_type) {
                    int array_open = -1;
                    for (int ti = s->as.let_stmt.type_start; ti + 2 < s->as.let_stmt.type_end; ++ti) {
                        if (tok_is(ti, "[") && tokens[ti + 1].type == TOK_NUMBER && tok_is(ti + 2, "]")) {
                            array_open = ti;
                            break;
                        }
                    }
                    if (array_open >= 0) {
                        emit_c_type_tokens(out, s->as.let_stmt.type_start, array_open);
                        fprintf(out, " %s", s->as.let_stmt.name);
                        for (int ti = array_open; ti < s->as.let_stmt.type_end; ++ti) fputs(tokens[ti].text, out);
                        fputs(" = ", out);
                    } else {
                        emit_c_type_tokens(out, s->as.let_stmt.type_start, s->as.let_stmt.type_end);
                        fprintf(out, " %s = ", s->as.let_stmt.name);
                    }
                    bool array_zero = false;
                    if (s->as.let_stmt.expr && s->as.let_stmt.expr->kind == EXPR_INT &&
                        s->as.let_stmt.expr->as.int_val == 0 && array_open >= 0) {
                        array_zero = true;
                    }
                    if (array_zero) fputs("{0}", out);
                    else emit_expr(out, s->as.let_stmt.expr);
                    fputs(";\n", out);
                } else {
                    emit_c_decl(out, s->as.let_stmt.inferred_type, s->as.let_stmt.name);
                    fputs(" = ", out);
                    emit_expr(out, s->as.let_stmt.expr);
                    fprintf(out, ";\n");
                }
                break;

            case ST_PRINT:
                emit_print(out, s->as.print_stmt.string_token);
                break;

            case ST_RETURN:
                emit_indent(out, indent);
                fputs("return ", out);
                emit_expr(out, s->as.raw_stmt.expr);
                fputs(";\n", out);
                break;

            case ST_EXPR:
                emit_indent(out, indent);
                emit_expr(out, s->as.raw_stmt.expr);
                fputs(";\n", out);
                break;

            case ST_IF:
                emit_indent(out, indent);
                fputs("if (", out);
                if (s->as.if_stmt.cond->kind == EXPR_BINARY) {
                    const Expr *c = s->as.if_stmt.cond;
                    emit_expr(out, c->as.binary.lhs);
                    fprintf(out, " %s ", c->as.binary.op);
                    emit_expr(out, c->as.binary.rhs);
                } else {
                    emit_expr(out, s->as.if_stmt.cond);
                }
                fputs(") {\n", out);
                emit_stmt_list(out, s->as.if_stmt.then_body, indent + 1);
                emit_indent(out, indent);
                if (s->as.if_stmt.else_body) {
                    fputs("} else {\n", out);
                    emit_stmt_list(out, s->as.if_stmt.else_body, indent + 1);
                    emit_indent(out, indent);
                }
                fputs("}\n", out);
                break;

            case ST_WHILE:
                emit_indent(out, indent);
                fputs("while (", out);
                if (s->as.while_stmt.cond->kind == EXPR_BINARY) {
                    const Expr *c = s->as.while_stmt.cond;
                    emit_expr(out, c->as.binary.lhs);
                    fprintf(out, " %s ", c->as.binary.op);
                    emit_expr(out, c->as.binary.rhs);
                } else {
                    emit_expr(out, s->as.while_stmt.cond);
                }
                fputs(") {\n", out);
                emit_stmt_list(out, s->as.while_stmt.body, indent + 1);
                emit_indent(out, indent);
                fputs("}\n", out);
                break;

            case ST_BLOCK:
                emit_indent(out, indent);
                fputs("{\n", out);
                emit_stmt_list(out, s->as.block_stmt.body, indent + 1);
                emit_indent(out, indent);
                fputs("}\n", out);
                break;
        }
    }
}

static void emit_function_params(FILE *out, int start, int end) {
    fputs("(", out);
    bool first = true;
    int i = start;
    while (i < end) {
        int begin = i;
        int paren_depth = 0;
        while (i < end) {
            if (tok_is(i, "(")) ++paren_depth;
            else if (tok_is(i, ")") && paren_depth > 0) --paren_depth;
            if (paren_depth == 0 && tok_is(i, ",")) break;
            ++i;
        }
        if (!first) fputs(", ", out);
        first = false;

        int count = i - begin;
        if (count >= 3 && tok_is(begin + 1, ":")) {
            fprintf(out, "%s %s", tokens[begin + 2].text, tokens[begin].text);
            for (int k = begin + 3; k < i; ++k) fprintf(out, "%s", tokens[k].text);
        } else {
            for (int k = begin; k < i; ++k) {
                if (k > begin) fputc(' ', out);
                fprintf(out, "%s", tokens[k].text);
            }
        }
        if (i < end && tok_is(i, ",")) ++i;
    }
    fputs(")", out);
}

static void register_stmt_strings(const Stmt *stmt) {
    for (const Stmt *s = stmt; s; s = s->next) {
        if (s->kind == ST_LET) {
            register_symbol(s->as.let_stmt.name, s->as.let_stmt.expr && s->as.let_stmt.expr->kind == EXPR_STRING);
        } else if (s->kind == ST_IF) {
            register_stmt_strings(s->as.if_stmt.then_body);
            register_stmt_strings(s->as.if_stmt.else_body);
        } else if (s->kind == ST_WHILE) {
            register_stmt_strings(s->as.while_stmt.body);
        } else if (s->kind == ST_BLOCK) {
            register_stmt_strings(s->as.block_stmt.body);
        }
    }
}


static CType type_unknown(void) {
    CType t; memset(&t, 0, sizeof(t)); t.kind = TYPE_UNKNOWN; return t;
}

static CType type_simple(TypeKind kind, int bits, int is_signed, const char *name) {
    CType t; memset(&t, 0, sizeof(t));
    t.kind = kind; t.bits = bits; t.is_signed = is_signed;
    if (name) snprintf(t.name, sizeof(t.name), "%s", name);
    return t;
}

static CType type_pointer(CType base) {
    CType t; memset(&t, 0, sizeof(t));
    t.kind = TYPE_PTR; t.base = (CType *)xmalloc(sizeof(CType));
    *t.base = base;
    return t;
}

static CType type_array(CType base, size_t count) {
    CType t; memset(&t, 0, sizeof(t));
    t.kind = TYPE_ARRAY;
    t.base = (CType *)xmalloc(sizeof(CType));
    *t.base = base;
    t.array_count = count;
    return t;
}

static CType clone_type(CType t) {
    if ((t.kind == TYPE_PTR || t.kind == TYPE_ARRAY) && t.base) {
        if (t.kind == TYPE_PTR) return type_pointer(clone_type(*t.base));
        return type_array(clone_type(*t.base), t.array_count);
    }
    return t;
}

static void free_type(CType *t) {
    if (!t) return;
    if ((t->kind == TYPE_PTR || t->kind == TYPE_ARRAY) && t->base) {
        free_type(t->base);
        free(t->base);
        t->base = NULL;
    }
}

static bool is_integral_type(const CType *t) {
    return t && (t->kind == TYPE_INT || t->kind == TYPE_UINT || t->kind == TYPE_BOOL || t->kind == TYPE_ENUM);
}

static CType parse_type_range(int start, int end, const Program *p) {
    if (start >= end) return type_unknown();
    int i = start;
    int ptr_depth = 0;
    while (i < end && tok_is(i, "*")) { ++ptr_depth; ++i; }
    if (i >= end || tokens[i].type != TOK_IDENT) return type_unknown();
    const char *name = tokens[i].text;
    ++i;
    while (i < end && tok_is(i, "*")) { ++ptr_depth; ++i; }
    CType t = type_unknown();
    if (strcmp(name, "void") == 0) t = type_simple(TYPE_VOID, 0, 0, name);
    else if (strcmp(name, "bool") == 0) t = type_simple(TYPE_BOOL, 1, 0, name);
    else if (strcmp(name, "int") == 0) t = type_simple(TYPE_INT, 32, 1, name);
    else if (strcmp(name, "u8") == 0) t = type_simple(TYPE_UINT, 8, 0, name);
    else if (strcmp(name, "u16") == 0) t = type_simple(TYPE_UINT, 16, 0, name);
    else if (strcmp(name, "u32") == 0) t = type_simple(TYPE_UINT, 32, 0, name);
    else if (strcmp(name, "u64") == 0) t = type_simple(TYPE_UINT, 64, 0, name);
    else if (strcmp(name, "i8") == 0) t = type_simple(TYPE_INT, 8, 1, name);
    else if (strcmp(name, "i16") == 0) t = type_simple(TYPE_INT, 16, 1, name);
    else if (strcmp(name, "i32") == 0) t = type_simple(TYPE_INT, 32, 1, name);
    else if (strcmp(name, "i64") == 0) t = type_simple(TYPE_INT, 64, 1, name);
    else if (strcmp(name, "char") == 0) t = type_simple(TYPE_INT, 8, 1, name);
    else {
        for (size_t ei = 0; ei < p->enum_count; ++ei) {
            if (strcmp(p->enums[ei].name, name) == 0) {
                t = type_simple(TYPE_ENUM, 32, 1, name);
                break;
            }
        }
        for (size_t si = 0; si < p->struct_count && t.kind == TYPE_UNKNOWN; ++si) {
            if (strcmp(p->structs[si].name, name) == 0) {
                t = type_simple(TYPE_STRUCT, 0, 0, name);
                break;
            }
        }
    }
    if (t.kind == TYPE_UNKNOWN) return t;
    while (ptr_depth-- > 0) t = type_pointer(t);

    while (i + 2 < end && tok_is(i, "[") && tokens[i + 1].type == TOK_NUMBER && tok_is(i + 2, "]")) {
        long long count = strtoll(tokens[i + 1].text, NULL, 0);
        if (count <= 0) return type_unknown();
        t = type_array(t, (size_t)count);
        i += 3;
    }
    return t;
}

static bool types_compatible(const CType *a, const CType *b) {
    if (!a || !b) return false;
    if (a->kind == TYPE_UNKNOWN || b->kind == TYPE_UNKNOWN) return true;
    if (is_integral_type(a) && is_integral_type(b)) return true;
    if (a->kind == TYPE_STRING && b->kind == TYPE_STRING) return true;
    if (a->kind == TYPE_ENUM && b->kind == TYPE_ENUM) return strcmp(a->name, b->name) == 0;
    if (a->kind == TYPE_STRUCT && b->kind == TYPE_STRUCT) return strcmp(a->name, b->name) == 0;
    if (a->kind == TYPE_PTR && b->kind == TYPE_PTR) {
        if (a->base->kind == TYPE_VOID || b->base->kind == TYPE_VOID) return true;
        return types_compatible(a->base, b->base);
    }
    if (a->kind == TYPE_ARRAY && b->kind == TYPE_ARRAY) {
        return a->array_count == b->array_count && types_compatible(a->base, b->base);
    }
    return false;
}

static const StructDecl *find_struct(const Program *p, const char *name) {
    for (size_t i = 0; i < p->struct_count; ++i)
        if (strcmp(p->structs[i].name, name) == 0) return &p->structs[i];
    return NULL;
}

static CType find_struct_field_type(const Program *p, const CType *base, const char *member, int token_index_for_error) {
    const CType *b = base;
    if (b && b->kind == TYPE_PTR) b = b->base;
    if (!b || b->kind != TYPE_STRUCT) { die_at(token_index_for_error, "member access requires a struct or struct pointer"); }
    const StructDecl *sd = find_struct(p, b->name);
    if (!sd) { die_at(token_index_for_error, "unknown struct type"); }
    int i = sd->fields_start;
    while (i < sd->fields_end) {
        int seg = i;
        while (i < sd->fields_end && !tok_is(i, ";")) ++i;
        int stop = i;
        int colon = -1;
        for (int k = seg; k < stop; ++k) if (tok_is(k, ":")) { colon = k; break; }
        if (colon >= 0 && colon == seg + 1) {
            int name_end = colon;
            const char *field_name = tokens[seg].text;
            if (strcmp(field_name, member) == 0) return parse_type_range(colon + 1, stop, p);
            (void)name_end;
        } else if (stop - seg >= 2) {
            int name_idx = -1;
            int array_suffix = stop;
            for (int k = seg; k < stop; ++k) {
                if (tok_is(k, "[")) { array_suffix = k; break; }
            }
            for (int k = array_suffix - 1; k >= seg; --k) {
                if (tokens[k].type == TOK_IDENT) { name_idx = k; break; }
            }
            if (name_idx >= 0 && strcmp(tokens[name_idx].text, member) == 0) {
                CType field = parse_type_range(seg, name_idx, p);
                int k = name_idx + 1;
                while (k + 2 < stop && tok_is(k, "[") && tokens[k + 1].type == TOK_NUMBER && tok_is(k + 2, "]")) {
                    long long count = strtoll(tokens[k + 1].text, NULL, 0);
                    if (count <= 0) { free_type(&field); die_at(token_index_for_error, "array size must be positive"); }
                    field = type_array(field, (size_t)count);
                    k += 3;
                }
                return field;
            }
        }
        if (i < sd->fields_end) ++i;
    }
    die_at(token_index_for_error, "unknown struct member");
    return type_unknown();
}

typedef struct {
    LocalSymbol *items;
    int count;
} TypeEnv;

static void env_push(TypeEnv *env, const char *name, CType type) {
    for (int i = 0; i < env->count; ++i) {
        if (strcmp(env->items[i].name, name) == 0) { env->items[i].type = clone_type(type); return; }
    }
    env->items = (LocalSymbol *)xrealloc(env->items, sizeof(LocalSymbol) * (size_t)(env->count + 1));
    env->items[env->count].name = xstrdup(name);
    env->items[env->count].type = clone_type(type);
    ++env->count;
}

static bool env_get(TypeEnv *env, const char *name, CType *out) {
    for (int i = env->count - 1; i >= 0; --i) if (strcmp(env->items[i].name, name) == 0) { *out = clone_type(env->items[i].type); return true; }
    return false;
}

static void env_free(TypeEnv *env) {
    for (int i = 0; i < env->count; ++i) { free(env->items[i].name); free_type(&env->items[i].type); }
    free(env->items); env->items = NULL; env->count = 0;
}

static CType function_return_type(const Program *p, const char *name) {
    for (size_t i = 0; i < p->function_count; ++i) {
        if (strcmp(p->functions[i].name, name) != 0) continue;
        FunctionDecl *fn = &p->functions[i];
        if (fn->return_start < fn->return_end) return parse_type_range(fn->return_start, fn->return_end, p);
        return type_simple(TYPE_INT, 32, 1, "int");
    }
    return type_unknown();
}

typedef struct {
    int params_start;
    int params_end;
    int return_start;
    int return_end;
} GeneratedFunctionInfo;

static bool find_comptime_generated_function(const Program *p, const char *name, GeneratedFunctionInfo *out) {
    if (!p || !name) return false;

    for (size_t ci = 0; ci < p->comptime_count; ++ci) {
        const ComptimeDecl *d = &p->comptimes[ci];
        for (int k = d->body_start + 1; k + 4 < d->body_end; ++k) {
            if (tokens[k].type != TOK_IDENT) continue;
            if (!tok_is(k + 1, "{") || !tok_is(k + 2, d->loop_var) ||
                !tok_is(k + 3, "}") || !tok_is(k + 4, "(")) continue;

            char generated[256];
            for (int value = d->start_value; value < d->end_value; ++value) {
                snprintf(generated, sizeof(generated), "%s%d", tokens[k].text, value);
                if (strcmp(generated, name) != 0) continue;

                int close = find_matching_paren(k + 4, d->body_end);
                if (close >= d->body_end) return false;

                int ret_start = k - 1;
                while (ret_start > d->body_start && !tok_is(ret_start - 1, ";") &&
                       !tok_is(ret_start - 1, "{") && !tok_is(ret_start - 1, "}")) --ret_start;

                if (out) {
                    out->params_start = k + 5;
                    out->params_end = close;
                    out->return_start = ret_start;
                    out->return_end = k;
                }
                return true;
            }
        }
    }
    return false;
}

static int function_param_count(const FunctionDecl *fn) {
    if (fn->params_end == fn->params_start + 1 && tok_is(fn->params_start, "void")) return 0;
    int i = fn->params_start, count = 0;
    while (i < fn->params_end) {
        while (i < fn->params_end && tok_is(i, ",")) ++i;
        if (i >= fn->params_end) break;
        int depth = 0;
        while (i < fn->params_end) {
            if (tok_is(i, "(") || tok_is(i, "[")) ++depth;
            else if (tok_is(i, ")") || tok_is(i, "]")) { if (depth > 0) --depth; }
            if (depth == 0 && tok_is(i, ",")) break;
            ++i;
        }
        ++count;
        if (i < fn->params_end) ++i;
    }
    return count;
}

static bool function_param_at(const FunctionDecl *fn, const Program *p, int wanted, char *name_out, size_t name_cap, CType *type_out) {
    if (fn->params_end == fn->params_start + 1 && tok_is(fn->params_start, "void")) return false;
    int i = fn->params_start, index = 0;
    while (i < fn->params_end) {
        while (i < fn->params_end && tok_is(i, ",")) ++i;
        if (i >= fn->params_end) break;
        int begin = i, depth = 0;
        while (i < fn->params_end) {
            if (tok_is(i, "(") || tok_is(i, "[")) ++depth;
            else if (tok_is(i, ")") || tok_is(i, "]")) { if (depth > 0) --depth; }
            if (depth == 0 && tok_is(i, ",")) break;
            ++i;
        }
        if (index == wanted) {
            int colon = -1;
            for (int k = begin; k < i; ++k) if (tok_is(k, ":")) { colon = k; break; }
            if (colon != begin + 1 || tokens[begin].type != TOK_IDENT) return false;
            snprintf(name_out, name_cap, "%s", tokens[begin].text);
            *type_out = parse_type_range(colon + 1, i, p);
            return true;
        }
        ++index;
        if (i < fn->params_end) ++i;
    }
    return false;
}

static bool find_enum_constant(const Program *p, const char *name, const char **enum_name_out) {
    for (size_t ei = 0; ei < p->enum_count; ++ei) {
        const EnumDecl *d = &p->enums[ei];
        for (int i = d->members_start; i < d->members_end; ) {
            if (tokens[i].type == TOK_IDENT) {
                if (strcmp(tokens[i].text, name) == 0) {
                    if (enum_name_out) *enum_name_out = d->name;
                    return true;
                }
                ++i;
                while (i < d->members_end && !tok_is(i, ",")) ++i;
                if (i < d->members_end) ++i;
            } else {
                ++i;
            }
        }
    }
    return false;
}

static bool expr_is_lvalue(const Expr *e) {
    if (!e) return false;
    return e->kind == EXPR_IDENT || e->kind == EXPR_MEMBER ||
           e->kind == EXPR_INDEX || (e->kind == EXPR_UNARY && strcmp(e->as.unary.op, "*") == 0);
}

static CType check_expr(const Program *p, TypeEnv *env, const Expr *e);

static CType check_expr(const Program *p, TypeEnv *env, const Expr *e) {
    if (!e) return type_unknown();
    switch (e->kind) {
        case EXPR_INT: return type_simple(TYPE_INT, 32, 1, "int");
        case EXPR_STRING: return type_simple(TYPE_STRING, 0, 0, "string");
        case EXPR_IDENT: {
            CType t; if (env_get(env, e->as.ident, &t)) return t;
            const char *enum_name = NULL;
            if (find_enum_constant(p, e->as.ident, &enum_name))
                return type_simple(TYPE_ENUM, 32, 1, enum_name);
            CType ft = function_return_type(p, e->as.ident);
            if (ft.kind != TYPE_UNKNOWN) return ft;
            return type_unknown();
        }
        case EXPR_UNARY: {
            CType a = check_expr(p, env, e->as.unary.operand);
            const char *op = e->as.unary.op;
            if (strcmp(op, "&") == 0) return type_pointer(a);
            if (strcmp(op, "*") == 0) {
                if (a.kind != TYPE_PTR) die_at(-1, "cannot dereference a non-pointer");
                return clone_type(*a.base);
            }
            if (strcmp(op, "!") == 0 || strcmp(op, "~") == 0 || strcmp(op, "+") == 0 || strcmp(op, "-") == 0) {
                if (!is_integral_type(&a)) die_at(-1, "unary operator requires an integer value");
                return a;
            }
            return type_unknown();
        }
        case EXPR_MEMBER: {
            CType base = check_expr(p, env, e->as.member.base);
            if (e->as.member.arrow && base.kind != TYPE_PTR) die_at(-1, "'->' requires a pointer");
            if (!e->as.member.arrow && base.kind == TYPE_PTR) die_at(-1, "'.' requires a struct value; use '->' for pointers");
            return find_struct_field_type(p, &base, e->as.member.member, -1);
        }
        case EXPR_INDEX: {
            CType base = check_expr(p, env, e->as.index.base);
            CType index = check_expr(p, env, e->as.index.index);
            if (!is_integral_type(&index)) die_at(-1, "array index must be an integer");
            if (base.kind == TYPE_ARRAY || base.kind == TYPE_PTR) {
                CType r = clone_type(*base.base);
                free_type(&base);
                free_type(&index);
                return r;
            }
            free_type(&base);
            free_type(&index);
            die_at(-1, "indexing requires an array or pointer");
            return type_unknown();
        }
        case EXPR_CALL: {
            if (e->as.call.callee->kind == EXPR_IDENT) {
                const char *name = e->as.call.callee->as.ident;
                const FunctionDecl *fn = NULL;
                for (size_t i = 0; i < p->function_count; ++i) if (strcmp(p->functions[i].name, name) == 0) { fn = &p->functions[i]; break; }
                if (!fn) {
                    GeneratedFunctionInfo gi;
                    if (find_comptime_generated_function(p, name, &gi)) {
                        FunctionDecl generated;
                        memset(&generated, 0, sizeof(generated));
                        generated.params_start = gi.params_start;
                        generated.params_end = gi.params_end;

                        int expected = function_param_count(&generated);
                        if (expected != e->as.call.arg_count) die_at(-1, "comptime-generated function argument count mismatch");
                        for (int i = 0; i < e->as.call.arg_count; ++i) {
                            char pn[128];
                            CType pt = type_unknown();
                            if (function_param_at(&generated, p, i, pn, sizeof(pn), &pt)) {
                                CType at = check_expr(p, env, e->as.call.args[i]);
                                if (!types_compatible(&pt, &at)) die_at(-1, "comptime-generated function argument type mismatch");
                                free_type(&pt);
                                free_type(&at);
                            }
                        }
                        CType ret = parse_type_range(gi.return_start, gi.return_end, p);
                        if (ret.kind == TYPE_UNKNOWN) ret = type_simple(TYPE_INT, 32, 1, "int");
                        return ret;
                    }
                }
                if (fn) {
                    int expected = function_param_count(fn);
                    if (expected != e->as.call.arg_count) die_at(-1, "function argument count mismatch");
                    for (int i = 0; i < e->as.call.arg_count; ++i) {
                        char pn[128]; CType pt = type_unknown();
                        if (function_param_at(fn, p, i, pn, sizeof(pn), &pt)) {
                            CType at = check_expr(p, env, e->as.call.args[i]);
                            if (!types_compatible(&pt, &at)) die_at(-1, "function argument type mismatch");
                            free_type(&pt); free_type(&at);
                        }
                    }
                    return function_return_type(p, name);
                }
            }
            (void)check_expr(p, env, e->as.call.callee);
            for (int i = 0; i < e->as.call.arg_count; ++i) { CType t = check_expr(p, env, e->as.call.args[i]); free_type(&t); }
            return type_unknown();
        }
        case EXPR_BINARY: {
            CType a = check_expr(p, env, e->as.binary.lhs);
            CType b = check_expr(p, env, e->as.binary.rhs);
            const char *op = e->as.binary.op;
            if (strcmp(op, "=") == 0 || strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 ||
                strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0 || strcmp(op, "%=") == 0) {
                if (!expr_is_lvalue(e->as.binary.lhs)) die_at(-1, "left side of assignment must be assignable");
                bool zero_array_init = strcmp(op, "=") == 0 && a.kind == TYPE_ARRAY &&
                                       b.kind == TYPE_INT && b.bits == 32;
                if (!zero_array_init && !types_compatible(&a, &b)) die_at(-1, "assignment type mismatch");
                if (strcmp(op, "=") != 0 && !is_integral_type(&a)) die_at(-1, "compound assignment requires an integer value");
                CType r = clone_type(a); free_type(&a); free_type(&b); return r;
            }
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 || strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                if (!types_compatible(&a, &b) && !(is_integral_type(&a) && is_integral_type(&b))) die_at(-1, "incompatible operands");
                CType r = type_simple(TYPE_BOOL, 1, 0, "bool"); free_type(&a); free_type(&b); return r;
            }
            if (!is_integral_type(&a) || !is_integral_type(&b)) die_at(-1, "arithmetic or bitwise operator requires integers");
            CType r = (a.kind == TYPE_UINT || b.kind == TYPE_UINT) ? type_simple(TYPE_UINT, a.bits > b.bits ? a.bits : b.bits, 0, "u64") : a;
            free_type(&a); free_type(&b); return r;
        }
    }
    return type_unknown();
}

static void check_stmt_list(const Program *p, TypeEnv *env, const Stmt *stmt, CType expected_return);

static void check_stmt_list(const Program *p, TypeEnv *env, const Stmt *stmt, CType expected_return) {
    for (Stmt *s = (Stmt *)stmt; s; s = s->next) {
        switch (s->kind) {
            case ST_LET: {
                CType t = type_unknown();
                if (s->as.let_stmt.is_cpu_ctor) {
                    const char *ctor = s->as.let_stmt.ctor_type ? s->as.let_stmt.ctor_type : "CPU";
                    t = parse_type_range(s->as.let_stmt.type_start >= 0 ? s->as.let_stmt.type_start : 0,
                                         s->as.let_stmt.type_start >= 0 ? s->as.let_stmt.type_start : 0, p);
                    for (size_t si = 0; si < p->struct_count; ++si) if (strcmp(p->structs[si].name, ctor) == 0) { t = type_simple(TYPE_STRUCT, 0, 0, ctor); break; }
                    if (s->as.let_stmt.has_explicit_type) {
                        CType declared = parse_type_range(s->as.let_stmt.type_start, s->as.let_stmt.type_end, p);
                        if (!types_compatible(&declared, &t)) die_at(-1, "constructor type does not match declaration");
                        free_type(&t); t = clone_type(declared); free_type(&declared);
                    }
                } else {
                    t = check_expr(p, env, s->as.let_stmt.expr);
                    if (s->as.let_stmt.has_explicit_type) {
                        CType declared = parse_type_range(s->as.let_stmt.type_start, s->as.let_stmt.type_end, p);
                        if (declared.kind == TYPE_UNKNOWN) die_at(-1, "unknown type in let declaration");
                        bool zero_array_init = declared.kind == TYPE_ARRAY && t.kind == TYPE_INT &&
                                               t.bits == 32 && s->as.let_stmt.expr &&
                                               s->as.let_stmt.expr->kind == EXPR_INT &&
                                               s->as.let_stmt.expr->as.int_val == 0;
                        if (!zero_array_init && !types_compatible(&declared, &t)) die_at(-1, "initializer type does not match declaration");
                        free_type(&t); t = clone_type(declared); free_type(&declared);
                    }
                }
                if (t.kind == TYPE_UNKNOWN && !s->as.let_stmt.has_explicit_type) die_at(-1, "cannot infer type of let initializer");
                if (s->as.let_stmt.inferred_type) {
                    free_type(s->as.let_stmt.inferred_type);
                    free(s->as.let_stmt.inferred_type);
                }
                s->as.let_stmt.inferred_type = (CType *)xmalloc(sizeof(CType));
                *s->as.let_stmt.inferred_type = clone_type(t);
                env_push(env, s->as.let_stmt.name, t); free_type(&t); break;
            }
            case ST_RETURN: {
                CType t = check_expr(p, env, s->as.raw_stmt.expr);
                if (!types_compatible(&expected_return, &t)) die_at(-1, "return type does not match function return type");
                free_type(&t); break;
            }
            case ST_EXPR: { CType t = check_expr(p, env, s->as.raw_stmt.expr); free_type(&t); break; }
            case ST_PRINT: break;
            case ST_IF: {
                CType t = check_expr(p, env, s->as.if_stmt.cond);
                if (!is_integral_type(&t)) die_at(-1, "if condition must be an integer or bool");
                free_type(&t); check_stmt_list(p, env, s->as.if_stmt.then_body, expected_return); check_stmt_list(p, env, s->as.if_stmt.else_body, expected_return); break;
            }
            case ST_WHILE: {
                CType t = check_expr(p, env, s->as.while_stmt.cond);
                if (!is_integral_type(&t)) die_at(-1, "while condition must be an integer or bool");
                free_type(&t); check_stmt_list(p, env, s->as.while_stmt.body, expected_return); break;
            }
            case ST_BLOCK: check_stmt_list(p, env, s->as.block_stmt.body, expected_return); break;
        }
    }
}

static void typecheck_program(const Program *p) {
    for (size_t fi = 0; fi < p->function_count; ++fi) {
        const FunctionDecl *fn = &p->functions[fi];
        CType ret = function_return_type(p, fn->name);
        TypeEnv env = {0};
        int param_count = function_param_count(fn);
        for (int pi = 0; pi < param_count; ++pi) {
            char pn[128]; CType pt = type_unknown();
            if (function_param_at(fn, p, pi, pn, sizeof(pn), &pt) && pt.kind != TYPE_UNKNOWN) { env_push(&env, pn, pt); free_type(&pt); }
        }
        if (!fn->is_extern) check_stmt_list(p, &env, fn->body, ret);
        free_type(&ret); env_free(&env);
    }
}

static void emit_c_type_tokens(FILE *out, int start, int end) {
    if (start < 0 || end <= start) { fputs("int", out); return; }
    bool first = true;
    for (int i = start; i < end; ++i) {
        if (tok_is(i, "*")) { fputs(first ? "*" : " *", out); first = false; continue; }
        if (!first) fputc(' ', out);
        fputs(tokens[i].text, out); first = false;
    }
}

static void emit_program(FILE *out, Program *p) {
    fputs("#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <stdbool.h>\n\n", out);
    fputs("#if defined(__GNUC__) || defined(__clang__)\n#define CSTAR_UNUSED __attribute__((unused))\n#else\n#define CSTAR_UNUSED\n#endif\n\n", out);
    fputs("typedef uint64_t u64;\ntypedef uint32_t u32;\ntypedef uint16_t u16;\ntypedef uint8_t u8;\n", out);
    fputs("typedef int64_t i64; typedef int32_t i32; typedef int16_t i16; typedef int8_t i8;\n", out);
    fputs("\n", out);

    /* Enums are emitted before structs so enum types are available to later declarations. */
    for (size_t e = 0; e < p->enum_count; ++e) {
        EnumDecl *d = &p->enums[e];
        fprintf(out, "typedef enum %s {\n", d->name);
        for (int i = d->members_start; i < d->members_end; ++i) {
            emit_token(out, i);
            if (tok_is(i, ",")) fputc('\n', out);
        }
        fprintf(out, "} %s;\n\n", d->name);
    }

    for (size_t s = 0; s < p->struct_count; ++s) {
        StructDecl *d = &p->structs[s];
        fprintf(out, "typedef struct %s {\n", d->name);
        for (int i = d->fields_start; i < d->fields_end; ++i) {
            if (tok_is(i, ";")) fputs(";\n", out);
            else emit_token(out, i);
        }
        fprintf(out, "} %s;\n\n", d->name);
    }

    for (size_t c = 0; c < p->comptime_count; ++c) emit_comptime(out, &p->comptimes[c]);

    /* C ABI declarations are emitted as ordinary C prototypes. */
    for (size_t f = 0; f < p->function_count; ++f) {
        FunctionDecl *fn = &p->functions[f];
        if (!fn->is_extern) continue;
        if (fn->return_start < fn->return_end) {
            for (int k = fn->return_start; k < fn->return_end; ++k) {
                fputs(tokens[k].text, out);
                if (k + 1 < fn->return_end) fputc(' ', out);
            }
        } else {
            fputs("int", out);
        }
        fputc(' ', out);
        fputs(fn->name, out);
        if (fn->params_start >= fn->params_end) fputs("(void)", out);
        else emit_function_params(out, fn->params_start, fn->params_end);
        fputs(";\n", out);
    }
    if (p->function_count) fputc('\n', out);

    for (size_t f = 0; f < p->function_count; ++f) {
        FunctionDecl *fn = &p->functions[f];
        if (fn->is_extern) continue;
        if (fn->return_start < fn->return_end && tok_is(fn->return_start, "->")) {
            for (int k = fn->return_start + 1; k < fn->return_end; ++k) {
                fprintf(out, "%s", tokens[k].text);
                if (k + 1 < fn->return_end) fputc(' ', out);
            }
            fputc(' ', out);
        } else {
            fputs("int ", out);
        }
        fprintf(out, "%s", fn->name);
        if (fn->params_start >= fn->params_end) fputs("(void)", out);
        else emit_function_params(out, fn->params_start, fn->params_end);
        fputs(" {\n", out);

        /* Register string locals before printing statements with interpolation. */
        register_stmt_strings(fn->body);

        emit_stmt_list(out, fn->body, 1);
        fputs("}\n\n", out);
        clear_symbols();
    }
}

static void emit_includes_directly(FILE *out) {
    /* Keep explicit C includes from the C* source. */
    for (int i = 0; i < token_count; ++i) {
        if (!tok_is(i, "include")) continue;
        ++i;
        if (tok_is(i, "<")) {
            fputs("#include <", out);
            ++i;
            while (i < token_count && !tok_is(i, ">")) {
                fputs(tokens[i].text, out);
                ++i;
            }
            fputs(">\n", out);
        } else if (i < token_count && tokens[i].type == TOK_STRING) {
            fprintf(out, "#include %s\n", tokens[i].text);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "C* Compiler v0.11\nUsage: %s <file.cppo>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("C*");
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return 1;
    }
    rewind(fp);

    char *src = (char *)xmalloc((size_t)size + 1);
    size_t got = fread(src, 1, (size_t)size, fp);
    fclose(fp);
    src[got] = '\0';

    ImportState imports;
    memset(&imports, 0, sizeof(imports));
    import_list_add(&imports.active, &imports.active_count, argv[1]);
    char *expanded_src = expand_imports_recursive(src, argv[1], &imports, 0);
    import_state_pop_active(&imports, argv[1]);
    import_list_add(&imports.done, &imports.done_count, argv[1]);
    free(src);

    lex(expanded_src);
    free(expanded_src);
    import_state_free(&imports);

    Program program = parse_program();
    typecheck_program(&program);

    FILE *out = fopen("output.c", "w");
    if (!out) {
        perror("output.c");
        free_program(&program);
        return 1;
    }

    emit_includes_directly(out);
    if (token_count > 0) {
        /* emit_program supplies the standard runtime typedefs and AST-generated code. */
        emit_program(out, &program);
    }
    fclose(out);

    free_program(&program);
    clear_symbols();

    printf("[C* Compiler v0.11] compiled successfully, output.c generated\n");
    return 0;
}
