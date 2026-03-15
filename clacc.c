/* clacc.c - Code generation and bytecode output for the clacc compiler.
 *
 * Compiles a parsed clac token stream into C0 bytecode (.bc0). Supports
 * two modes: inline compilation (no recursion) and heap-based stack
 * compilation (recursive programs). Mode is selected automatically via
 * DFS cycle detection on the call graph.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lib/xalloc.h"
#include "lib/hdict.h"
#include "clacc.h"
#include "parse.h"
#include "lib/c0vm.h"

/* ===== C0 VM opcodes used directly (supplement lib/c0vm.h) ===== */

#define C0_DUP    0x59
#define C0_POP    0x57
#define C0_SWAP   0x5F
#define C0_NOP    0x00

/* ===== Bytecode buffer operations ===== */

static void codebuf_init(codebuf *buf) {
    buf->cap = 256;
    buf->len = 0;
    buf->data = xmalloc(buf->cap);
}

static void codebuf_grow(codebuf *buf) {
    buf->cap *= 2;
    buf->data = realloc(buf->data, buf->cap);
    if (!buf->data) { fprintf(stderr, "Out of memory\n"); exit(1); }
}

static void emit(codebuf *buf, ubyte byte) {
    if (buf->len >= buf->cap) codebuf_grow(buf);
    buf->data[buf->len++] = byte;
}

static void emit_i16_be(codebuf *buf, int16_t val) {
    emit(buf, (ubyte)((val >> 8) & 0xFF));
    emit(buf, (ubyte)(val & 0xFF));
}

static size_t codebuf_pos(codebuf *buf) {
    return buf->len;
}

static void patch_i16_be(codebuf *buf, size_t pos, int16_t val) {
    buf->data[pos]     = (ubyte)((val >> 8) & 0xFF);
    buf->data[pos + 1] = (ubyte)(val & 0xFF);
}

/* ===== Integer pool operations ===== */

static void intpool_init(intpool *pool) {
    pool->cap = 16;
    pool->count = 0;
    pool->values = xmalloc(pool->cap * sizeof(int32_t));
}

static size_t intpool_add(intpool *pool, int32_t value) {
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->values[i] == value) return i;
    }
    if (pool->count >= pool->cap) {
        pool->cap *= 2;
        pool->values = realloc(pool->values, pool->cap * sizeof(int32_t));
        if (!pool->values) { fprintf(stderr, "Out of memory\n"); exit(1); }
    }
    pool->values[pool->count] = value;
    return pool->count++;
}

/* ===== Function body access ===== */

static tokenList *get_function_body(clac_file *cfile, int func_index) {
    list *cur = cfile->functions->next;
    for (int i = 1; i < func_index && cur; i++) cur = cur->next;
    if (!cur) return NULL;
    tokenList *body = cur->tokens;
    if (!body) return NULL;
    tokenList *first = body->next;
    if (!first) return NULL;
    if (first->token->operator == USER_DEFINED) first = first->next;
    return first;
}

/* Returns true if func_index refers to the main body (last segment). */
static bool is_main_body(clac_file *cfile, int func_index) {
    return func_index == cfile->functionCount;
}

/* Returns true if the function is a comment (named "comment")
 * or is otherwise not a compilable user-defined function. */
static bool is_comment_function(clac_file *cfile, int func_index) {
    if (is_main_body(cfile, func_index)) return true;
    list *cur = cfile->functions->next;
    for (int i = 1; i < func_index && cur; i++) cur = cur->next;
    if (!cur) return true;
    if (cur->name && strcmp(cur->name, "comment") == 0) return true;
    tokenList *body = cur->tokens;
    if (!body || !body->next) return true;
    tokenList *first = body->next;
    if (first->token->operator != USER_DEFINED) return true;
    return false;
}

/* Detect recursion via DFS cycle detection on the call graph */
static bool has_recursion_dfs(clac_file *cfile, int func_index,
                              bool *visited, bool *on_stack) {
    if (is_comment_function(cfile, func_index)) return false;
    visited[func_index] = true;
    on_stack[func_index] = true;
    tokenList *body = get_function_body(cfile, func_index);
    tokenList *cur = body;
    while (cur) {
        if (cur->token->operator == UFUNC) {
            int callee = cur->token->i;
            if (on_stack[callee]) return true;
            if (!visited[callee] &&
                has_recursion_dfs(cfile, callee, visited, on_stack))
                return true;
        }
        cur = cur->next;
    }
    on_stack[func_index] = false;
    return false;
}

static bool detect_recursion(clac_file *cfile) {
    int n = cfile->functionCount + 1;
    bool *visited = xcalloc(n, sizeof(bool));
    bool *on_stack = xcalloc(n, sizeof(bool));
    bool found = false;
    for (int i = 1; i <= cfile->functionCount && !found; i++) {
        if (!visited[i] && !is_comment_function(cfile, i)) {
            found = has_recursion_dfs(cfile, i, visited, on_stack);
        }
    }
    /* Also check the main function for UFUNC calls into recursive chains */
    tokenList *main_tok = cfile->mainFunction;
    if (main_tok) main_tok = main_tok->next;
    while (main_tok && !found) {
        if (main_tok->token->operator == UFUNC) {
            int callee = main_tok->token->i;
            if (!visited[callee] && !is_comment_function(cfile, callee)) {
                found = has_recursion_dfs(cfile, callee, visited, on_stack);
            }
        }
        main_tok = main_tok->next;
    }
    free(visited);
    free(on_stack);
    return found;
}

/* ===== Compilation: emit C0 VM bytecode for each clac token ===== */

/* Forward declaration */
static tokenList *compile_n_tokens(compile_ctx *ctx, tokenList *start,
                                   int count, clac_file *cfile);

static void emit_push_int(compile_ctx *ctx, int32_t value) {
    if (value >= -128 && value <= 127) {
        emit(&ctx->code, BIPUSH);
        emit(&ctx->code, (ubyte)(int8_t)value);
    } else {
        size_t idx = intpool_add(&ctx->ints, value);
        emit(&ctx->code, ILDC);
        emit_i16_be(&ctx->code, (int16_t)idx);
    }
}

static void emit_rot(compile_ctx *ctx) {
    /* rot: (a b c -- b c a) where c is on top.
     * Implementation: save c, swap a<->b, restore c, swap b<->c */
    if (ctx->num_vars < 1) ctx->num_vars = 1;
    emit(&ctx->code, VSTORE); emit(&ctx->code, 0);  /* save c → stack: a b */
    emit(&ctx->code, C0_SWAP);                       /* stack: b a */
    emit(&ctx->code, VLOAD);  emit(&ctx->code, 0);  /* stack: b a c */
    emit(&ctx->code, C0_SWAP);                       /* stack: b c a */
}

static void emit_pick(compile_ctx *ctx, int32_t n) {
    /* 1-indexed pick: pop n, copy the nth element from the top of remaining stack.
     * 1 pick = dup, 2 pick = copy second from top, etc. */
    if (n == 1) {
        emit(&ctx->code, C0_DUP);
        return;
    }
    /* General case: save top n-1 elements, dup the target, restore.
     * For n=2: VSTORE t0, DUP, VLOAD t0, SWAP
     * For n=3: VSTORE t0, VSTORE t1, DUP, VLOAD t1, SWAP, VLOAD t0, SWAP
     * ... etc.  Need n-1 local variable slots. */
    int base_var = 0;
    if (ctx->num_vars < base_var + (n - 1))
        ctx->num_vars = base_var + (n - 1);

    for (int i = 0; i < n - 1; i++) {
        emit(&ctx->code, VSTORE); emit(&ctx->code, (ubyte)(base_var + i));
    }
    emit(&ctx->code, C0_DUP);
    for (int i = n - 2; i >= 0; i--) {
        emit(&ctx->code, VLOAD); emit(&ctx->code, (ubyte)(base_var + i));
        emit(&ctx->code, C0_SWAP);
    }
}

static void emit_lt(compile_ctx *ctx) {
    /* Less-than: pop b, pop a, push (a < b ? 1 : 0) */
    emit(&ctx->code, IF_ICMPLT);
    size_t patch_pos = codebuf_pos(&ctx->code);
    emit_i16_be(&ctx->code, 0); /* placeholder */

    /* false branch: push 0, goto end */
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
    emit(&ctx->code, GOTO);
    size_t goto_pos = codebuf_pos(&ctx->code);
    emit_i16_be(&ctx->code, 0); /* placeholder */

    /* true branch: push 1 */
    size_t true_start = codebuf_pos(&ctx->code);
    patch_i16_be(&ctx->code, patch_pos,
                 (int16_t)(true_start - (patch_pos - 1)));
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 1);

    size_t end = codebuf_pos(&ctx->code);
    patch_i16_be(&ctx->code, goto_pos,
                 (int16_t)(end - (goto_pos - 1)));
}

static void emit_print(compile_ctx *ctx) {
    ctx->uses_print = true;
    /* printint: pop value, call native printint, discard return */
    emit(&ctx->code, INVOKENATIVE);
    emit_i16_be(&ctx->code, 0);  /* native pool index 0 = printint */
    emit(&ctx->code, C0_POP);
    /* println: push empty string, call native println, discard return */
    emit(&ctx->code, ALDC);
    emit_i16_be(&ctx->code, 0);  /* string pool index 0 = "" */
    emit(&ctx->code, INVOKENATIVE);
    emit_i16_be(&ctx->code, 1);  /* native pool index 1 = println */
    emit(&ctx->code, C0_POP);
}

static void emit_quit(compile_ctx *ctx) {
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
    emit(&ctx->code, RETURN);
}

/*
 * Compile a single non-control-flow token.
 * Returns true on success.
 */
static bool compile_simple_token(compile_ctx *ctx, tok *token) {
    switch (token->operator) {
    case PRINT:
        emit_print(ctx);
        break;
    case QUIT:
        emit_quit(ctx);
        break;
    case PLUS:
        emit(&ctx->code, IADD);
        break;
    case MINUS:
        emit(&ctx->code, ISUB);
        break;
    case MULT:
        emit(&ctx->code, IMUL);
        break;
    case DIV:
        emit(&ctx->code, IDIV);
        break;
    case MOD:
        emit(&ctx->code, IREM);
        break;
    case LT:
        emit_lt(ctx);
        break;
    case DROP:
        emit(&ctx->code, C0_POP);
        break;
    case SWAP:
        emit(&ctx->code, C0_SWAP);
        break;
    case ROT:
        emit_rot(ctx);
        break;
    case INT:
        emit_push_int(ctx, token->i);
        break;
    case UNK:
        /* Silently skip unknown tokens (comments, etc.) */
        break;
    default:
        fprintf(stderr, "Unhandled token: 0x%02x\n", token->operator);
        return false;
    }
    return true;
}

/*
 * Compile a sequence of N source tokens (or all remaining if count < 0).
 *
 * Matches the original 15-122 clac interpreter semantics:
 *   - if: pops condition; if zero, skips 2 tokens; if nonzero, continues.
 *   - else: unconditionally skips 1 token.
 *   - Combined pattern: <cond> if <then-tok> else <else-tok>
 *   - UFUNC: recursively compiles function body at call site (no pre-inlining).
 *   - pick: pops N from stack; compile-time optimization when preceded by literal.
 *
 * Returns a pointer to the next unconsumed token.
 */
static tokenList *compile_n_tokens(compile_ctx *ctx, tokenList *start,
                                   int count, clac_file *cfile) {
    tokenList *cur = start;
    int compiled = 0;

    while (cur && (count < 0 || compiled < count)) {

        /* IF: pop condition, if zero jump past 2 tokens.
         * When followed by <then> else <else>, forms standard if-else. */
        if (cur->token->operator == IF) {
            tokenList *tok1 = cur->next;
            if (!tok1) {
                fprintf(stderr, "Error: 'if' at end of input\n");
                ctx->has_error = true;
                return NULL;
            }

            emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
            emit(&ctx->code, IF_CMPEQ);
            size_t if_patch = codebuf_pos(&ctx->code);
            emit_i16_be(&ctx->code, 0);

            tokenList *after_then = compile_n_tokens(ctx, tok1, 1, cfile);
            if (ctx->has_error) return NULL;

            if (after_then && after_then->token->operator == ELSE) {
                tokenList *else_body = after_then->next;
                if (!else_body) {
                    fprintf(stderr, "Error: 'else' at end of input\n");
                    ctx->has_error = true;
                    return NULL;
                }

                emit(&ctx->code, GOTO);
                size_t goto_patch = codebuf_pos(&ctx->code);
                emit_i16_be(&ctx->code, 0);

                size_t else_pos = codebuf_pos(&ctx->code);
                patch_i16_be(&ctx->code, if_patch,
                             (int16_t)(else_pos - (if_patch - 1)));

                tokenList *after_else =
                    compile_n_tokens(ctx, else_body, 1, cfile);

                size_t end_pos = codebuf_pos(&ctx->code);
                patch_i16_be(&ctx->code, goto_patch,
                             (int16_t)(end_pos - (goto_patch - 1)));

                int consumed = 1; /* if */
                for (tokenList *t = tok1; t != after_then; t = t->next)
                    consumed++;
                consumed++; /* else keyword */
                for (tokenList *t = else_body; t != after_else; t = t->next)
                    consumed++;
                cur = after_else;
                compiled += consumed;
            } else {
                /* No else: false path skips 2 tokens (tok1 + tok2) */
                if (after_then) {
                    tokenList *after_second =
                        compile_n_tokens(ctx, after_then, 1, cfile);

                    size_t end_pos = codebuf_pos(&ctx->code);
                    patch_i16_be(&ctx->code, if_patch,
                                 (int16_t)(end_pos - (if_patch - 1)));

                    int consumed = 1; /* if */
                    for (tokenList *t = tok1; t != after_then; t = t->next)
                        consumed++;
                    for (tokenList *t = after_then; t != after_second;
                         t = t->next)
                        consumed++;
                    cur = after_second;
                    compiled += consumed;
                } else {
                    size_t end_pos = codebuf_pos(&ctx->code);
                    patch_i16_be(&ctx->code, if_patch,
                                 (int16_t)(end_pos - (if_patch - 1)));
                    int consumed = 1;
                    for (tokenList *t = tok1; t != NULL; t = t->next)
                        consumed++;
                    cur = NULL;
                    compiled += consumed;
                }
            }
            continue;
        }

        /* ELSE: unconditionally skip the next 1 token */
        if (cur->token->operator == ELSE) {
            if (cur->next) {
                tokenList *skipped = cur->next;
                cur = skipped->next;
                compiled += 2;
            } else {
                cur = NULL;
                compiled++;
            }
            continue;
        }

        /* UFUNC: recursively compile the function body at the call site */
        if (cur->token->operator == UFUNC) {
            int idx = cur->token->i;
            if (!is_comment_function(cfile, idx)) {
                tokenList *body = get_function_body(cfile, idx);
                if (body)
                    compile_n_tokens(ctx, body, -1, cfile);
            }
            cur = cur->next;
            compiled++;
            continue;
        }

        /* INT + PICK pair: compile-time pick optimization */
        if (cur->token->operator == INT &&
            cur->next && cur->next->token->operator == PICK) {
            int32_t n = cur->token->i;
            if (n < 1) {
                fprintf(stderr, "Error: pick index must be >= 1\n");
                ctx->has_error = true;
                return NULL;
            }
            emit_pick(ctx, n);
            cur = cur->next->next;
            compiled += 2;
            continue;
        }

        /* Standalone PICK: not supported in inline mode */
        if (cur->token->operator == PICK) {
            fprintf(stderr,
                    "Error: runtime 'pick' not supported in inline mode "
                    "(use a literal argument, e.g. '1 pick')\n");
            ctx->has_error = true;
            return NULL;
        }

        if (!compile_simple_token(ctx, cur->token)) {
            ctx->has_error = true;
            return NULL;
        }

        cur = cur->next;
        compiled++;
    }

    return cur;
}

/* ===== BC0 hex output helpers ===== */

static void write_hex_byte(FILE *out, ubyte b) {
    fprintf(out, "%02X", b);
}

static void write_hex_u16_be(FILE *out, uint16_t val) {
    fprintf(out, "%02X %02X", (val >> 8) & 0xFF, val & 0xFF);
}

static void write_hex_i32_be(FILE *out, int32_t val) {
    uint32_t u = (uint32_t)val;
    fprintf(out, "%02X %02X %02X %02X",
            (u >> 24) & 0xFF, (u >> 16) & 0xFF,
            (u >> 8) & 0xFF, u & 0xFF);
}

/* ===== Heap-based stack mode (for recursive programs) =====
 *
 * All clac operations manipulate a heap-allocated integer array instead of
 * the C0 VM's native operand stack. This allows functions called via
 * INVOKESTATIC to share the same data stack.
 *
 * Convention: V[0] = arr (pointer), V[1] = sp (stack pointer index).
 * Temp variables start at V[2].
 */

#define HEAP_STACK_SIZE 1024
#define HEAP_VAR_ARR 0
#define HEAP_VAR_SP  1
#define HEAP_TEMP_BASE 2

static void heap_pop(compile_ctx *ctx) {
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 1);
    emit(&ctx->code, ISUB);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_ARR);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, AADDS);
    emit(&ctx->code, IMLOAD);
}

static void heap_push(compile_ctx *ctx) {
    /* Value is on top of VM operand stack */
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_ARR);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, AADDS);
    emit(&ctx->code, C0_SWAP);
    emit(&ctx->code, IMSTORE);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 1);
    emit(&ctx->code, IADD);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_VAR_SP);
}

static void heap_push_int(compile_ctx *ctx, int32_t value) {
    if (value >= -128 && value <= 127) {
        emit(&ctx->code, BIPUSH);
        emit(&ctx->code, (ubyte)(int8_t)value);
    } else {
        size_t idx = intpool_add(&ctx->ints, value);
        emit(&ctx->code, ILDC);
        emit_i16_be(&ctx->code, (int16_t)idx);
    }
    heap_push(ctx);
}

static void heap_binop(compile_ctx *ctx, ubyte op) {
    heap_pop(ctx);   /* b (was top of heap) */
    heap_pop(ctx);   /* a (was second) — VM stack: b, a (a on top) */
    if (op == ISUB || op == IDIV || op == IREM)
        emit(&ctx->code, C0_SWAP); /* need a below, b on top for correct order */
    emit(&ctx->code, op);
    heap_push(ctx);
}

static void heap_drop(compile_ctx *ctx) {
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 1);
    emit(&ctx->code, ISUB);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_VAR_SP);
}

static void heap_swap(compile_ctx *ctx) {
    if (ctx->num_vars < HEAP_TEMP_BASE + 2) ctx->num_vars = HEAP_TEMP_BASE + 2;
    heap_pop(ctx);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE);
    heap_pop(ctx);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE + 1);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE);
    heap_push(ctx);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE + 1);
    heap_push(ctx);
}

static void heap_rot(compile_ctx *ctx) {
    /* (a b c -- b c a) */
    if (ctx->num_vars < HEAP_TEMP_BASE + 3) ctx->num_vars = HEAP_TEMP_BASE + 3;
    heap_pop(ctx);  /* c */
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE);
    heap_pop(ctx);  /* b */
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE + 1);
    heap_pop(ctx);  /* a */
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE + 2);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE + 1); /* b */
    heap_push(ctx);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE);     /* c */
    heap_push(ctx);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE + 2); /* a */
    heap_push(ctx);
}

static void heap_pick(compile_ctx *ctx, int32_t n) {
    /* 1-indexed compile-time pick: copy the nth element from top */
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_ARR);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, BIPUSH); emit(&ctx->code, (ubyte)n);
    emit(&ctx->code, ISUB);    /* sp - n */
    emit(&ctx->code, AADDS);   /* &arr[sp - n] */
    emit(&ctx->code, IMLOAD);  /* value */
    heap_push(ctx);
}

static void heap_pick_runtime(compile_ctx *ctx) {
    /* Runtime pick: pop n from heap stack, copy nth element from new top */
    if (ctx->num_vars < HEAP_TEMP_BASE + 1) ctx->num_vars = HEAP_TEMP_BASE + 1;
    heap_pop(ctx);
    emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_TEMP_BASE);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_ARR);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
    emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_TEMP_BASE);
    emit(&ctx->code, ISUB);    /* sp - n */
    emit(&ctx->code, AADDS);   /* &arr[sp - n] */
    emit(&ctx->code, IMLOAD);  /* value */
    heap_push(ctx);
}

static void heap_lt(compile_ctx *ctx) {
    heap_pop(ctx);  /* b */
    heap_pop(ctx);  /* a  — VM: b, a */
    emit(&ctx->code, C0_SWAP); /* VM: a, b */

    emit(&ctx->code, IF_ICMPLT);
    size_t patch = codebuf_pos(&ctx->code);
    emit_i16_be(&ctx->code, 0);

    emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
    emit(&ctx->code, GOTO);
    size_t goto_p = codebuf_pos(&ctx->code);
    emit_i16_be(&ctx->code, 0);

    size_t t = codebuf_pos(&ctx->code);
    patch_i16_be(&ctx->code, patch, (int16_t)(t - (patch - 1)));
    emit(&ctx->code, BIPUSH); emit(&ctx->code, 1);

    size_t e = codebuf_pos(&ctx->code);
    patch_i16_be(&ctx->code, goto_p, (int16_t)(e - (goto_p - 1)));
    heap_push(ctx);
}

static void heap_print(compile_ctx *ctx) {
    ctx->uses_print = true;
    heap_pop(ctx);
    emit(&ctx->code, INVOKENATIVE); emit_i16_be(&ctx->code, 0);
    emit(&ctx->code, C0_POP);
    emit(&ctx->code, ALDC);         emit_i16_be(&ctx->code, 0);
    emit(&ctx->code, INVOKENATIVE); emit_i16_be(&ctx->code, 1);
    emit(&ctx->code, C0_POP);
}

/* Forward declaration */
static tokenList *compile_heap_n_tokens(compile_ctx *ctx, tokenList *start,
                                        int count, clac_file *cfile,
                                        int *func_map);

static bool compile_heap_simple(compile_ctx *ctx, tok *token,
                                int *func_map) {
    switch (token->operator) {
    case PRINT: heap_print(ctx); break;
    case QUIT:
        emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
        emit(&ctx->code, RETURN);
        break;
    case PLUS:  heap_binop(ctx, IADD); break;
    case MINUS: heap_binop(ctx, ISUB); break;
    case MULT:  heap_binop(ctx, IMUL); break;
    case DIV:   heap_binop(ctx, IDIV); break;
    case MOD:   heap_binop(ctx, IREM); break;
    case LT:    heap_lt(ctx); break;
    case DROP:  heap_drop(ctx); break;
    case SWAP:  heap_swap(ctx); break;
    case ROT:   heap_rot(ctx); break;
    case INT:   heap_push_int(ctx, token->i); break;
    case UNK:   break;
    case UFUNC: {
        int clac_idx = token->i;
        int bc0_idx = func_map[clac_idx];
        if (bc0_idx < 0) break; /* comment/unmapped function → no-op */
        emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_ARR);
        emit(&ctx->code, VLOAD);  emit(&ctx->code, HEAP_VAR_SP);
        emit(&ctx->code, INVOKESTATIC);
        emit_i16_be(&ctx->code, (int16_t)bc0_idx);
        emit(&ctx->code, VSTORE); emit(&ctx->code, HEAP_VAR_SP);
        break;
    }
    default:
        fprintf(stderr, "Unhandled heap token: 0x%02x\n", token->operator);
        return false;
    }
    return true;
}

static tokenList *compile_heap_n_tokens(compile_ctx *ctx, tokenList *start,
                                        int count, clac_file *cfile,
                                        int *func_map) {
    tokenList *cur = start;
    int compiled = 0;

    while (cur && (count < 0 || compiled < count)) {

        /* IF: pop condition from heap; if zero skip 2 tokens */
        if (cur->token->operator == IF) {
            tokenList *tok1 = cur->next;
            if (!tok1) {
                fprintf(stderr, "Error: 'if' at end of input\n");
                ctx->has_error = true;
                return NULL;
            }

            heap_pop(ctx);
            emit(&ctx->code, BIPUSH); emit(&ctx->code, 0);
            emit(&ctx->code, IF_CMPEQ);
            size_t if_patch = codebuf_pos(&ctx->code);
            emit_i16_be(&ctx->code, 0);

            tokenList *after_then =
                compile_heap_n_tokens(ctx, tok1, 1, cfile, func_map);
            if (ctx->has_error) return NULL;

            if (after_then && after_then->token->operator == ELSE) {
                tokenList *else_body = after_then->next;
                if (!else_body) {
                    fprintf(stderr, "Error: 'else' at end of input\n");
                    ctx->has_error = true;
                    return NULL;
                }

                emit(&ctx->code, GOTO);
                size_t goto_p = codebuf_pos(&ctx->code);
                emit_i16_be(&ctx->code, 0);

                size_t ep = codebuf_pos(&ctx->code);
                patch_i16_be(&ctx->code, if_patch,
                             (int16_t)(ep - (if_patch - 1)));

                tokenList *after_else =
                    compile_heap_n_tokens(ctx, else_body, 1, cfile, func_map);
                if (ctx->has_error) return NULL;

                size_t endp = codebuf_pos(&ctx->code);
                patch_i16_be(&ctx->code, goto_p,
                             (int16_t)(endp - (goto_p - 1)));

                int consumed = 1;
                for (tokenList *t = tok1; t != after_then; t = t->next)
                    consumed++;
                consumed++;
                for (tokenList *t = else_body; t != after_else; t = t->next)
                    consumed++;
                cur = after_else;
                compiled += consumed;
            } else {
                if (after_then) {
                    tokenList *after_second =
                        compile_heap_n_tokens(ctx, after_then, 1, cfile,
                                              func_map);
                    if (ctx->has_error) return NULL;

                    size_t end_pos = codebuf_pos(&ctx->code);
                    patch_i16_be(&ctx->code, if_patch,
                                 (int16_t)(end_pos - (if_patch - 1)));

                    int consumed = 1;
                    for (tokenList *t = tok1; t != after_then; t = t->next)
                        consumed++;
                    for (tokenList *t = after_then; t != after_second;
                         t = t->next)
                        consumed++;
                    cur = after_second;
                    compiled += consumed;
                } else {
                    size_t end_pos = codebuf_pos(&ctx->code);
                    patch_i16_be(&ctx->code, if_patch,
                                 (int16_t)(end_pos - (if_patch - 1)));
                    cur = NULL;
                    compiled += 2;
                }
            }
            continue;
        }

        /* ELSE: skip next 1 token */
        if (cur->token->operator == ELSE) {
            if (cur->next) {
                cur = cur->next->next;
                compiled += 2;
            } else {
                cur = NULL;
                compiled++;
            }
            continue;
        }

        /* INT + PICK pair: compile-time pick optimization */
        if (cur->token->operator == INT &&
            cur->next && cur->next->token->operator == PICK) {
            heap_pick(ctx, cur->token->i);
            cur = cur->next->next;
            compiled += 2;
            continue;
        }

        /* Standalone PICK: runtime pick (pop n from heap stack) */
        if (cur->token->operator == PICK) {
            heap_pick_runtime(ctx);
            cur = cur->next;
            compiled++;
            continue;
        }

        if (!compile_heap_simple(ctx, cur->token, func_map)) {
            ctx->has_error = true;
            return NULL;
        }
        cur = cur->next;
        compiled++;
    }
    return cur;
}

/* Compiled function for multi-function bc0 output */
typedef struct {
    codebuf code;
    int num_args;
    int num_vars;
} compiled_func;

static bool build_heap_mode(clac_file *cfile, char *output_path) {
    /* Map clac function indices to bc0 function pool indices.
     * bc0 index 0 = main. Non-comment functions get 1, 2, 3, ... */
    int *func_map = xcalloc(cfile->functionCount + 1, sizeof(int));
    int bc0_count = 1; /* 0 is main */
    for (int i = 1; i <= cfile->functionCount; i++) {
        if (!is_comment_function(cfile, i)) {
            func_map[i] = bc0_count++;
        } else {
            func_map[i] = -1;
        }
    }
    int total_funcs = bc0_count;

    compiled_func *funcs = xcalloc(total_funcs, sizeof(compiled_func));
    intpool shared_ints;
    intpool_init(&shared_ints);
    bool uses_print = false;

    /* Compile each user-defined function */
    for (int i = 1; i <= cfile->functionCount; i++) {
        if (func_map[i] < 0) continue;
        int fi = func_map[i];

        compile_ctx fctx;
        codebuf_init(&fctx.code);
        fctx.ints = shared_ints;
        fctx.uses_print = false;
        fctx.num_vars = HEAP_TEMP_BASE;
        fctx.has_error = false;

        tokenList *body = get_function_body(cfile, i);
        if (body) {
            compile_heap_n_tokens(&fctx, body, -1, cfile, func_map);
        }
        if (fctx.has_error) {
            fprintf(stderr, "Compilation failed in function %d\n", i);
            free(func_map); free(funcs);
            return false;
        }

        /* Return updated sp */
        emit(&fctx.code, VLOAD); emit(&fctx.code, HEAP_VAR_SP);
        emit(&fctx.code, RETURN);

        funcs[fi].code = fctx.code;
        funcs[fi].num_args = 2;
        funcs[fi].num_vars = fctx.num_vars;
        shared_ints = fctx.ints;
        if (fctx.uses_print) uses_print = true;
    }

    /* Compile main function */
    compile_ctx mctx;
    codebuf_init(&mctx.code);
    mctx.ints = shared_ints;
    mctx.uses_print = false;
    mctx.num_vars = HEAP_TEMP_BASE;
    mctx.has_error = false;

    /* Setup: allocate heap stack array */
    size_t sz_idx = intpool_add(&mctx.ints, HEAP_STACK_SIZE);
    emit(&mctx.code, ILDC);
    emit_i16_be(&mctx.code, (int16_t)sz_idx);
    emit(&mctx.code, NEWARRAY); emit(&mctx.code, 4);
    emit(&mctx.code, VSTORE); emit(&mctx.code, HEAP_VAR_ARR);

    emit(&mctx.code, BIPUSH); emit(&mctx.code, 0);
    emit(&mctx.code, VSTORE); emit(&mctx.code, HEAP_VAR_SP);

    /* Compile main body */
    tokenList *main_body = cfile->mainFunction;
    if (main_body) main_body = main_body->next;
    if (main_body) {
        compile_heap_n_tokens(&mctx, main_body, -1, cfile, func_map);
    }
    if (mctx.has_error) {
        fprintf(stderr, "Compilation failed in main body\n");
        free(func_map); free(funcs);
        return false;
    }

    emit(&mctx.code, BIPUSH); emit(&mctx.code, 0);
    emit(&mctx.code, RETURN);

    funcs[0].code = mctx.code;
    funcs[0].num_args = 0;
    funcs[0].num_vars = mctx.num_vars;
    shared_ints = mctx.ints;
    if (mctx.uses_print) uses_print = true;

    /* Write bc0 output */
    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "w");
        if (!out) {
            fprintf(stderr, "Error: cannot open %s\n", output_path);
            free(func_map); free(funcs);
            return false;
        }
    }

    fprintf(out, "C0 C0 FF EE\n");
    fprintf(out, "00 17\n");

    fprintf(out, "\n");
    write_hex_u16_be(out, (uint16_t)shared_ints.count);
    fprintf(out, "             # int pool count\n");
    for (size_t i = 0; i < shared_ints.count; i++) {
        write_hex_i32_be(out, shared_ints.values[i]);
        fprintf(out, "       # %d\n", shared_ints.values[i]);
    }

    fprintf(out, "\n");
    write_hex_u16_be(out, 1);
    fprintf(out, "             # string pool size\n");
    fprintf(out, "00                # \"\"\n");

    fprintf(out, "\n");
    write_hex_u16_be(out, (uint16_t)total_funcs);
    fprintf(out, "             # function count\n");

    for (int f = 0; f < total_funcs; f++) {
        fprintf(out, "\n# <function %d>\n", f);
        write_hex_byte(out, (ubyte)funcs[f].num_args);
        fprintf(out, "                # num_args\n");
        write_hex_byte(out, (ubyte)funcs[f].num_vars);
        fprintf(out, "                # num_vars = %d\n", funcs[f].num_vars);
        write_hex_u16_be(out, (uint16_t)funcs[f].code.len);
        fprintf(out, "             # code_length = %zu\n", funcs[f].code.len);
        for (size_t i = 0; i < funcs[f].code.len; i++) {
            write_hex_byte(out, funcs[f].code.data[i]);
            if (i + 1 < funcs[f].code.len) fprintf(out, " ");
            if ((i + 1) % 16 == 0) fprintf(out, "\n");
        }
        if (funcs[f].code.len % 16 != 0) fprintf(out, "\n");
    }

    int native_count = uses_print ? 2 : 0;
    fprintf(out, "\n");
    write_hex_u16_be(out, (uint16_t)native_count);
    fprintf(out, "             # native count\n");
    if (uses_print) {
        fprintf(out, "00 01 00 09       # printint\n");
        fprintf(out, "00 01 00 0A       # println\n");
    }
    fprintf(out, "\n# Generated by clacc (heap mode)\n");

    if (output_path) fclose(out);

    size_t total_bytes = 0;
    for (int f = 0; f < total_funcs; f++) total_bytes += funcs[f].code.len;
    fprintf(stderr, "Compilation successful: %zu bytes across %d functions\n",
            total_bytes, total_funcs);

    free(func_map);
    free(funcs);
    return true;
}

/* ===== BC0 output generation (inline mode) ===== */

static void write_bc0(FILE *out, compile_ctx *ctx) {
    /* Magic */
    fprintf(out, "C0 C0 FF EE\n");

    /* Version 11, arch 1 (64-bit): (11 << 1) | 1 = 0x17 */
    fprintf(out, "00 17\n");

    /* Integer pool */
    fprintf(out, "\n");
    write_hex_u16_be(out, (uint16_t)ctx->ints.count);
    fprintf(out, "             # int pool count\n");
    for (size_t i = 0; i < ctx->ints.count; i++) {
        write_hex_i32_be(out, ctx->ints.values[i]);
        fprintf(out, "       # %d\n", ctx->ints.values[i]);
    }

    /* String pool: always include at least "" for println */
    fprintf(out, "\n");
    write_hex_u16_be(out, 1);
    fprintf(out, "             # string pool size\n");
    fprintf(out, "00");
    fprintf(out, "                # \"\"\n");

    /* Function pool: 1 function (main) */
    fprintf(out, "\n");
    write_hex_u16_be(out, 1);
    fprintf(out, "             # function count\n");

    /* Main function */
    fprintf(out, "\n# <main>\n");
    write_hex_byte(out, 0); /* num_args = 0 */
    fprintf(out, "                # num_args\n");
    write_hex_byte(out, (ubyte)ctx->num_vars);
    fprintf(out, "                # num_vars = %d\n", ctx->num_vars);
    write_hex_u16_be(out, (uint16_t)ctx->code.len);
    fprintf(out, "             # code_length = %zu\n", ctx->code.len);

    /* Bytecode */
    for (size_t i = 0; i < ctx->code.len; i++) {
        write_hex_byte(out, ctx->code.data[i]);
        if (i + 1 < ctx->code.len) fprintf(out, " ");
        if ((i + 1) % 16 == 0) fprintf(out, "\n");
    }
    if (ctx->code.len % 16 != 0) fprintf(out, "\n");

    /* Native pool */
    fprintf(out, "\n");
    int native_count = ctx->uses_print ? 2 : 0;
    write_hex_u16_be(out, (uint16_t)native_count);
    fprintf(out, "             # native count\n");
    if (ctx->uses_print) {
        fprintf(out, "00 01 00 09       # printint\n");
        fprintf(out, "00 01 00 0A       # println\n");
    }

    fprintf(out, "\n# Generated by clacc\n");
}

/* ===== Main entry point ===== */

int main(int argc, char **argv) {
    clac_file cfile;
    char *output_path = NULL;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <clac_file> [-o output.bc0]\n", argv[0]);
        return 1;
    }

    /* Parse -o flag */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[i + 1];
            i++;
        }
    }

    /* Parse the .clac file */
    cfile.functions = xmalloc(sizeof(list));
    cfile.functions->next = NULL;
    cfile.functionCount = 0;
    if (!parse(argv[1], &cfile)) {
        fprintf(stderr, "Error: failed to parse %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "Parse successful: %d functions + main body\n",
            cfile.functionCount > 0 ? cfile.functionCount - 1 : 0);

    /* Get the main function body (last segment after final ;) */
    tokenList *main_body = cfile.mainFunction;
    if (!main_body || !main_body->next) {
        fprintf(stderr, "Error: empty program\n");
        return 1;
    }

    /* Check for recursion */
    bool recursive = detect_recursion(&cfile);
    if (recursive) {
        fprintf(stderr, "Mode: heap-based stack (recursion detected)\n");
        return build_heap_mode(&cfile, output_path) ? 0 : 1;
    }
    fprintf(stderr, "Mode: inline (no recursion detected)\n");

    /* Initialize compilation context */
    compile_ctx ctx;
    codebuf_init(&ctx.code);
    intpool_init(&ctx.ints);
    ctx.uses_print = false;
    ctx.num_vars = 0;
    ctx.has_error = false;

    /* Compile directly from the token list; UFUNC calls are recursively
     * compiled at each call site (no separate inlining pass). */
    compile_n_tokens(&ctx, main_body->next, -1, &cfile);

    if (ctx.has_error) {
        fprintf(stderr, "Compilation failed\n");
        return 1;
    }

    /* Always emit trailing RETURN. Checking the last byte is unreliable
     * because a data operand can equal 0xB0 (e.g. BIPUSH -80). If a
     * real RETURN already exists (from quit), this is unreachable. */
    emit(&ctx.code, BIPUSH);
    emit(&ctx.code, 0);
    emit(&ctx.code, RETURN);

    fprintf(stderr, "Compilation successful: %zu bytes of bytecode\n",
            ctx.code.len);

    /* Write bc0 output */
    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "w");
        if (!out) {
            fprintf(stderr, "Error: cannot open %s for writing\n",
                    output_path);
            return 1;
        }
    }

    write_bc0(out, &ctx);

    if (output_path) {
        fclose(out);
        fprintf(stderr, "Wrote %s\n", output_path);
    }

    return 0;
}
