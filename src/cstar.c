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
 * C* v0.6
 *
 * The frontend now parses ordinary runtime expressions into an AST.
 * Comptime templates intentionally remain token-oriented for now so the
 * proven specialization path stays stable while expression handling gains
 * real precedence, calls, member access, unary operators, and assignment.
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
    EXPR_MEMBER
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
    } as;
};

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
            Expr *expr;
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
    int return_start;
    int return_end;
    int params_start;
    int params_end;
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
    FunctionDecl *functions;
    size_t function_count;
    ComptimeDecl *comptimes;
    size_t comptime_count;
} Program;

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

static int eval_expr(int start, int end, const char *loop_var, int loop_val) {
    if (start >= end) return 0;

    if (tok_is(start, "(") && find_matching_paren(start, end) == end - 1)
        return eval_expr(start + 1, end - 1, loop_var, loop_val);

    if (end == start + 1) {
        if (tokens[start].type == TOK_NUMBER) return (int)strtol(tokens[start].text, NULL, 0);
        if (tokens[start].type == TOK_IDENT && strcmp(tokens[start].text, loop_var) == 0) return loop_val;
        return 0;
    }

    const char *cmp_ops[] = {"==", "!=", "<", ">", "<=", ">="};
    for (int opn = 0; opn < 6; ++opn) {
        for (int i = end - 1; i >= start; --i) {
            if (strcmp(tokens[i].text, cmp_ops[opn]) == 0) {
                int a = eval_expr(start, i, loop_var, loop_val);
                int b = eval_expr(i + 1, end, loop_var, loop_val);
                switch (opn) {
                    case 0: return a == b;
                    case 1: return a != b;
                    case 2: return a < b;
                    case 3: return a > b;
                    case 4: return a <= b;
                    default: return a >= b;
                }
            }
        }
    }

    const char *arith_ops[] = {"+", "-", "%", "*", "/"};
    for (int opn = 0; opn < 5; ++opn) {
        for (int i = end - 1; i >= start; --i) {
            if (strcmp(tokens[i].text, arith_ops[opn]) == 0) {
                int a = eval_expr(start, i, loop_var, loop_val);
                int b = eval_expr(i + 1, end, loop_var, loop_val);
                switch (opn) {
                    case 0: return a + b;
                    case 1: return a - b;
                    case 2: return b ? a % b : 0;
                    case 3: return a * b;
                    default: return b ? a / b : 0;
                }
            }
        }
    }

    return 0;
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
    }
    free(expr);
}

static void free_stmt_list(Stmt *stmt) {
    while (stmt) {
        Stmt *next = stmt->next;
        switch (stmt->kind) {
            case ST_LET:
                free(stmt->as.let_stmt.name);
                free_expr(stmt->as.let_stmt.expr);
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
            i += 2;
            if (!tok_is(i, "=")) die_at(i, "expected '=' after let name");
            ++i;
            int expr_start = i;
            int expr_end = scan_until_statement_end(i, end);
            if (expr_end == expr_start) die_at(i, "expected initializer expression");
            if ((expr_end == expr_start + 1 && tokens[expr_start].type == TOK_IDENT && strcmp(tokens[expr_start].text, "CPU") == 0) ||
                (expr_end == expr_start + 3 && tokens[expr_start].type == TOK_IDENT && strcmp(tokens[expr_start].text, "CPU") == 0 &&
                 tok_is(expr_start + 1, "(") && tok_is(expr_start + 2, ")")))
                s->as.let_stmt.is_cpu_ctor = true;
            else
                s->as.let_stmt.expr = parse_expr_range(expr_start, expr_end);
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

        if (tok_is(i, "fn")) {
            if (i + 1 >= token_count || tokens[i + 1].type != TOK_IDENT) die_at(i, "expected function name");
            FunctionDecl d;
            memset(&d, 0, sizeof(d));
            d.name = xstrdup(tokens[i + 1].text);
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
                while (j < token_count && !tok_is(j, "{")) ++j;
                d.return_end = j;
            } else {
                d.return_start = d.return_end = j;
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
    for (size_t i = 0; i < p->function_count; ++i) {
        free(p->functions[i].name);
        free_stmt_list(p->functions[i].body);
    }
    for (size_t i = 0; i < p->comptime_count; ++i) free(p->comptimes[i].loop_var);
    free(p->structs);
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
            (tokens[i + 2].type == TOK_IDENT || tokens[i + 2].type == TOK_NUMBER) && tok_is(i + 3, "}")) {
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
        cond_end = cond_close;
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

    int result = eval_expr(cond_start, cond_end, loop_var, loop_val);
    int start = result ? then_open + 1 : (else_open >= 0 ? else_open + 1 : -1);
    int end = result ? then_close : else_close;

    if (start >= 0 && end > start) {
        if (tok_is(start, "int") && start + 1 < end && strncmp(tokens[start + 1].text, "op_", 3) == 0)
            fprintf(out, "static inline ");
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
                fprintf(out, "static inline ");
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

static void emit_stmt_list(FILE *out, const Stmt *stmt, int indent);

static void emit_indent(FILE *out, int indent) {
    for (int i = 0; i < indent; ++i) fputs("    ", out);
}

static void emit_stmt_list(FILE *out, const Stmt *stmt, int indent) {
    for (const Stmt *s = stmt; s; s = s->next) {
        switch (s->kind) {
            case ST_LET:
                emit_indent(out, indent);
                if (s->as.let_stmt.is_cpu_ctor) {
                    fprintf(out, "CPU %s = {0};\n", s->as.let_stmt.name);
                } else {
                    fprintf(out, "__auto_type %s = ", s->as.let_stmt.name);
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
                emit_expr(out, s->as.if_stmt.cond);
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
                emit_expr(out, s->as.while_stmt.cond);
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

static void emit_program(FILE *out, Program *p) {
    fputs("#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n\n", out);
    fputs("typedef uint64_t u64;\ntypedef uint32_t u32;\ntypedef uint16_t u16;\ntypedef uint8_t u8;\n\n", out);

    /* Includes are intentionally preserved in source order only in a later backend pass. */
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

    for (size_t f = 0; f < p->function_count; ++f) {
        FunctionDecl *fn = &p->functions[f];
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
        emit_function_params(out, fn->params_start, fn->params_end);
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
        fprintf(stderr, "C* Compiler v0.6\nUsage: %s <file.cppo>\n", argv[0]);
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

    lex(src);
    free(src);

    Program program = parse_program();

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

    printf("[C* Compiler v0.6] compiled successfully, output.c generated\n");
    return 0;
}
