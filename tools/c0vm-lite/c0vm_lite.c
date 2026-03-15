/*
 * c0vm-lite: minimal C0 bytecode interpreter for clacc-generated programs.
 *
 * Supports only the ~24 opcodes that the clacc compiler emits.
 * NOT the full C0VM -- this is a purpose-built tool for testing clacc output.
 *
 * Usage: c0vm-lite <file.bc0>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

/* ===== Value representation ===== */

typedef enum { VAL_INT, VAL_PTR } val_tag;

typedef struct {
    val_tag tag;
    union c0val_u { int32_t i; void *p; } u;
} c0val;

static c0val mkint(int32_t i)  { c0val v; v.tag = VAL_INT; v.u.i = i; return v; }
static c0val mkptr(void *p)    { c0val v; v.tag = VAL_PTR; v.u.p = p; return v; }

static int32_t asint(c0val v) {
    if (v.tag != VAL_INT) {
        fprintf(stderr, "c0vm-lite: expected int on stack, got pointer\n");
        exit(2);
    }
    return v.u.i;
}

static void *asptr(c0val v) {
    if (v.tag != VAL_PTR) {
        fprintf(stderr, "c0vm-lite: expected pointer on stack, got int\n");
        exit(2);
    }
    return v.u.p;
}

/* ===== Operand stack (array-based) ===== */

#define STACK_MAX 4096

typedef struct {
    c0val data[STACK_MAX];
    int top;
} opstack;

static void stack_init(opstack *s) { s->top = 0; }

static void stack_push(opstack *s, c0val v) {
    if (s->top >= STACK_MAX) {
        fprintf(stderr, "c0vm-lite: operand stack overflow\n");
        exit(2);
    }
    s->data[s->top++] = v;
}

static c0val stack_pop(opstack *s) {
    if (s->top <= 0) {
        fprintf(stderr, "c0vm-lite: operand stack underflow\n");
        exit(2);
    }
    return s->data[--s->top];
}

/* stack_empty is unused but kept for completeness */
static bool stack_empty(opstack *s) __attribute__((unused));
static bool stack_empty(opstack *s) { return s->top == 0; }

/* ===== Call stack ===== */

#define CALL_MAX 1024

typedef struct {
    opstack S;
    uint8_t *P;
    uint16_t pc;
    c0val *V;
} frame;

static frame call_stack[CALL_MAX];
static int call_depth = 0;

/* ===== Array representation ===== */

typedef struct {
    int32_t count;
    int32_t elt_size;
    void *elems;
} c0_array;

/* ===== bc0 file structures ===== */

typedef struct {
    uint8_t num_args;
    uint8_t num_vars;
    uint16_t code_length;
    uint8_t *code;
} func_info;

typedef struct {
    uint16_t num_args;
    uint16_t func_table_index;
} native_info;

typedef struct {
    int32_t *int_pool;
    uint16_t int_count;
    char *string_pool;
    uint16_t string_count;
    func_info *functions;
    uint16_t func_count;
    native_info *natives;
    uint16_t native_count;
} bc0_file;

/* ===== Hex file parser ===== */

static bool parse_hex_char(int c, uint8_t *out) {
    if ('0' <= c && c <= '9')      { *out = (uint8_t)(c - '0');      return true; }
    if ('A' <= c && c <= 'F')      { *out = (uint8_t)(c - 'A' + 10); return true; }
    if ('a' <= c && c <= 'f')      { *out = (uint8_t)(c - 'a' + 10); return true; }
    return false;
}

static bool read_byte(FILE *f, uint8_t *out) {
    int c;
    do {
        c = fgetc(f);
        if (c == '#') { while ((c = fgetc(f)) != '\n' && c != EOF); }
    } while (c != EOF && isspace(c));

    if (c == EOF) return false;

    uint8_t hi, lo;
    if (!parse_hex_char(c, &hi)) {
        fprintf(stderr, "c0vm-lite: bad hex char '%c'\n", c);
        exit(1);
    }
    c = fgetc(f);
    if (c == EOF || !parse_hex_char(c, &lo)) {
        fprintf(stderr, "c0vm-lite: incomplete hex byte\n");
        exit(1);
    }
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

static uint8_t must_read_u8(FILE *f) {
    uint8_t b;
    if (!read_byte(f, &b)) {
        fprintf(stderr, "c0vm-lite: unexpected end of file\n");
        exit(1);
    }
    return b;
}

static uint16_t must_read_u16(FILE *f) {
    uint8_t hi = must_read_u8(f);
    uint8_t lo = must_read_u8(f);
    return (uint16_t)((hi << 8) | lo);
}

static uint32_t must_read_u32(FILE *f) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) b[i] = must_read_u8(f);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static bc0_file *load_bc0(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "c0vm-lite: cannot open '%s'\n", path);
        exit(1);
    }

    uint8_t magic[4];
    for (int i = 0; i < 4; i++) magic[i] = must_read_u8(f);
    if (magic[0] != 0xC0 || magic[1] != 0xC0 ||
        magic[2] != 0xFF || magic[3] != 0xEE) {
        fprintf(stderr, "c0vm-lite: bad magic number\n");
        fclose(f);
        exit(1);
    }

    uint16_t version = must_read_u16(f);
    uint16_t vers = version >> 1;
    if (vers < 9 || vers > 11) {
        fprintf(stderr, "c0vm-lite: unsupported version %u\n", vers);
        fclose(f);
        exit(1);
    }

    bc0_file *bc = calloc(1, sizeof(bc0_file));

    bc->int_count = must_read_u16(f);
    bc->int_pool = calloc(bc->int_count, sizeof(int32_t));
    for (int i = 0; i < bc->int_count; i++)
        bc->int_pool[i] = (int32_t)must_read_u32(f);

    bc->string_count = must_read_u16(f);
    bc->string_pool = calloc(bc->string_count + 1, 1);
    for (int i = 0; i < bc->string_count; i++)
        bc->string_pool[i] = (char)must_read_u8(f);

    bc->func_count = must_read_u16(f);
    bc->functions = calloc(bc->func_count, sizeof(func_info));
    for (int i = 0; i < bc->func_count; i++) {
        if (vers >= 11) {
            bc->functions[i].num_args = must_read_u8(f);
            bc->functions[i].num_vars = must_read_u8(f);
        } else {
            bc->functions[i].num_args = (uint8_t)must_read_u16(f);
            bc->functions[i].num_vars = (uint8_t)must_read_u16(f);
        }
        bc->functions[i].code_length = must_read_u16(f);
        bc->functions[i].code = calloc(bc->functions[i].code_length, 1);
        for (int k = 0; k < bc->functions[i].code_length; k++)
            bc->functions[i].code[k] = must_read_u8(f);
    }

    bc->native_count = must_read_u16(f);
    bc->natives = calloc(bc->native_count, sizeof(native_info));
    for (int i = 0; i < bc->native_count; i++) {
        bc->natives[i].num_args = must_read_u16(f);
        bc->natives[i].func_table_index = must_read_u16(f);
    }

    fclose(f);
    return bc;
}

static void free_bc0(bc0_file *bc) {
    free(bc->int_pool);
    free(bc->string_pool);
    for (int i = 0; i < bc->func_count; i++)
        free(bc->functions[i].code);
    free(bc->functions);
    free(bc->natives);
    free(bc);
}

/* ===== Opcode constants ===== */

enum {
    OP_NOP          = 0x00,
    OP_BIPUSH       = 0x10,
    OP_ILDC         = 0x13,
    OP_ALDC         = 0x14,
    OP_VLOAD        = 0x15,
    OP_IMLOAD       = 0x2E,
    OP_VSTORE       = 0x36,
    OP_IMSTORE      = 0x4E,
    OP_POP          = 0x57,
    OP_DUP          = 0x59,
    OP_SWAP         = 0x5F,
    OP_IADD         = 0x60,
    OP_AADDS        = 0x63,
    OP_ISUB         = 0x64,
    OP_IMUL         = 0x68,
    OP_IDIV         = 0x6C,
    OP_IREM         = 0x70,
    OP_IF_CMPEQ     = 0x9F,
    OP_IF_ICMPLT    = 0xA1,
    OP_GOTO         = 0xA7,
    OP_RETURN       = 0xB0,
    OP_INVOKENATIVE = 0xB7,
    OP_INVOKESTATIC = 0xB8,
    OP_NEWARRAY     = 0xBC,
};

/* Native function table indices (from C0 runtime).
 * The bc0 native pool stores num_args (u16) + function_table_index (u16).
 * clacc emits:  printint → 00 01 00 09, println → 00 01 00 0A.
 * That means num_args=1, index=9 for printint; num_args=1, index=10 for println. */
#define NATIVE_PRINTINT_IDX  0x0009
#define NATIVE_PRINTLN_IDX   0x000A

/* ===== Interpreter ===== */

static int execute(bc0_file *bc) {
    opstack S;
    stack_init(&S);
    uint8_t *P = bc->functions[0].code;
    uint16_t pc = 0;
    c0val *V = calloc(bc->functions[0].num_vars, sizeof(c0val));
    call_depth = 0;

    for (;;) {
        uint8_t op = P[pc];

        switch (op) {

        case OP_NOP:
            pc++;
            break;

        case OP_BIPUSH: {
            int8_t val = (int8_t)P[pc + 1];
            stack_push(&S, mkint((int32_t)val));
            pc += 2;
            break;
        }

        case OP_ILDC: {
            uint16_t idx = (uint16_t)((P[pc+1] << 8) | P[pc+2]);
            if (idx >= bc->int_count) {
                fprintf(stderr, "c0vm-lite: int pool index %u out of range\n", idx);
                exit(2);
            }
            stack_push(&S, mkint(bc->int_pool[idx]));
            pc += 3;
            break;
        }

        case OP_ALDC: {
            uint16_t idx = (uint16_t)((P[pc+1] << 8) | P[pc+2]);
            if (idx >= bc->string_count) {
                fprintf(stderr, "c0vm-lite: string pool index %u out of range\n", idx);
                exit(2);
            }
            stack_push(&S, mkptr(&bc->string_pool[idx]));
            pc += 3;
            break;
        }

        case OP_VLOAD: {
            uint8_t idx = P[pc + 1];
            stack_push(&S, V[idx]);
            pc += 2;
            break;
        }

        case OP_VSTORE: {
            uint8_t idx = P[pc + 1];
            V[idx] = stack_pop(&S);
            pc += 2;
            break;
        }

        case OP_IMLOAD: {
            void *addr = asptr(stack_pop(&S));
            int32_t val = *(int32_t *)addr;
            stack_push(&S, mkint(val));
            pc++;
            break;
        }

        case OP_IMSTORE: {
            c0val val = stack_pop(&S);
            void *addr = asptr(stack_pop(&S));
            *(int32_t *)addr = asint(val);
            pc++;
            break;
        }

        case OP_POP:
            stack_pop(&S);
            pc++;
            break;

        case OP_DUP: {
            c0val v = stack_pop(&S);
            stack_push(&S, v);
            stack_push(&S, v);
            pc++;
            break;
        }

        case OP_SWAP: {
            c0val b = stack_pop(&S);
            c0val a = stack_pop(&S);
            stack_push(&S, b);
            stack_push(&S, a);
            pc++;
            break;
        }

        case OP_IADD: {
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            stack_push(&S, mkint(a + b));
            pc++;
            break;
        }

        case OP_ISUB: {
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            stack_push(&S, mkint(a - b));
            pc++;
            break;
        }

        case OP_IMUL: {
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            stack_push(&S, mkint(a * b));
            pc++;
            break;
        }

        case OP_IDIV: {
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            if (b == 0) {
                fprintf(stderr, "c0vm-lite: division by zero\n");
                exit(2);
            }
            if (a == INT32_MIN && b == -1) {
                fprintf(stderr, "c0vm-lite: arithmetic overflow\n");
                exit(2);
            }
            stack_push(&S, mkint(a / b));
            pc++;
            break;
        }

        case OP_IREM: {
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            if (b == 0) {
                fprintf(stderr, "c0vm-lite: modulo by zero\n");
                exit(2);
            }
            if (a == INT32_MIN && b == -1) {
                fprintf(stderr, "c0vm-lite: arithmetic overflow\n");
                exit(2);
            }
            stack_push(&S, mkint(a % b));
            pc++;
            break;
        }

        case OP_IF_CMPEQ: {
            int16_t offset = (int16_t)((P[pc+1] << 8) | P[pc+2]);
            c0val v2 = stack_pop(&S);
            c0val v1 = stack_pop(&S);
            bool eq = (v1.tag == v2.tag) &&
                      ((v1.tag == VAL_INT) ? v1.u.i == v2.u.i
                                           : v1.u.p == v2.u.p);
            pc = eq ? (uint16_t)(pc + offset) : (uint16_t)(pc + 3);
            break;
        }

        case OP_IF_ICMPLT: {
            int16_t offset = (int16_t)((P[pc+1] << 8) | P[pc+2]);
            int32_t b = asint(stack_pop(&S));
            int32_t a = asint(stack_pop(&S));
            pc = (a < b) ? (uint16_t)(pc + offset) : (uint16_t)(pc + 3);
            break;
        }

        case OP_GOTO: {
            int16_t offset = (int16_t)((P[pc+1] << 8) | P[pc+2]);
            pc = (uint16_t)(pc + offset);
            break;
        }

        case OP_RETURN: {
            c0val retval = stack_pop(&S);
            free(V);

            if (call_depth == 0) {
                return asint(retval);
            }

            call_depth--;
            frame *fr = &call_stack[call_depth];
            S = fr->S;
            P = fr->P;
            pc = fr->pc;
            V = fr->V;
            stack_push(&S, retval);
            break;
        }

        case OP_INVOKENATIVE: {
            uint16_t idx = (uint16_t)((P[pc+1] << 8) | P[pc+2]);
            pc += 3;

            if (idx >= bc->native_count) {
                fprintf(stderr, "c0vm-lite: native index %u out of range\n", idx);
                exit(2);
            }

            uint16_t fti = bc->natives[idx].func_table_index;

            switch (fti) {
            case NATIVE_PRINTINT_IDX: {
                int32_t v = asint(stack_pop(&S));
                printf("%d", v);
                stack_push(&S, mkint(0));
                break;
            }
            case NATIVE_PRINTLN_IDX: {
                char *s = (char *)asptr(stack_pop(&S));
                printf("%s\n", s);
                stack_push(&S, mkint(0));
                break;
            }
            default:
                fprintf(stderr,
                        "c0vm-lite: unsupported native 0x%04X\n", fti);
                exit(2);
            }
            break;
        }

        case OP_INVOKESTATIC: {
            uint16_t fi = (uint16_t)((P[pc+1] << 8) | P[pc+2]);
            pc += 3;

            if (fi >= bc->func_count) {
                fprintf(stderr, "c0vm-lite: function index %u out of range\n", fi);
                exit(2);
            }

            if (call_depth >= CALL_MAX) {
                fprintf(stderr, "c0vm-lite: call stack overflow\n");
                exit(2);
            }

            func_info *target = &bc->functions[fi];

            /* Pop arguments from caller BEFORE saving the frame.
             * S is a value type, so saving after pop ensures the
             * restored stack won't contain stale argument slots. */
            c0val args[256];
            for (int i = target->num_args - 1; i >= 0; i--)
                args[i] = stack_pop(&S);

            call_stack[call_depth].S = S;
            call_stack[call_depth].P = P;
            call_stack[call_depth].pc = pc;
            call_stack[call_depth].V = V;
            call_depth++;

            stack_init(&S);
            V = calloc(target->num_vars, sizeof(c0val));

            for (int i = 0; i < target->num_args; i++)
                V[i] = args[i];

            P = target->code;
            pc = 0;
            break;
        }

        case OP_NEWARRAY: {
            uint8_t elt_size = P[pc + 1];
            pc += 2;
            int32_t count = asint(stack_pop(&S));
            if (count < 0) {
                fprintf(stderr, "c0vm-lite: negative array size\n");
                exit(2);
            }
            c0_array *arr = malloc(sizeof(c0_array));
            arr->count = count;
            arr->elt_size = elt_size;
            arr->elems = calloc((size_t)count, elt_size);
            stack_push(&S, mkptr(arr));
            break;
        }

        case OP_AADDS: {
            int32_t index = asint(stack_pop(&S));
            c0_array *arr = (c0_array *)asptr(stack_pop(&S));
            if (index < 0 || index >= arr->count) {
                fprintf(stderr,
                        "c0vm-lite: array index %d out of bounds "
                        "(size %d)\n", index, arr->count);
                exit(2);
            }
            void *addr = (char *)arr->elems + index * arr->elt_size;
            stack_push(&S, mkptr(addr));
            pc++;
            break;
        }

        default:
            fprintf(stderr,
                    "c0vm-lite: unsupported opcode 0x%02X at pc=%u\n",
                    op, pc);
            exit(2);
        }
    }
}

/* ===== Entry point ===== */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: c0vm-lite <file.bc0>\n");
        return 1;
    }

    bc0_file *bc = load_bc0(argv[1]);

    if (bc->func_count == 0) {
        fprintf(stderr, "c0vm-lite: no functions in bytecode\n");
        free_bc0(bc);
        return 1;
    }

    int result = execute(bc);
    free_bc0(bc);
    return result;
}
