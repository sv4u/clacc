/* clacc.h - Shared types and data structures for the clacc compiler.
 *
 * Defines token representation, parse tree nodes, the bytecode buffer,
 * integer constant pool, and compilation context used by both the
 * parser (parse.c) and code generator (clacc.c).
 */

#ifndef _CLACC_H_
#define _CLACC_H_

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>

typedef uint8_t ubyte;

typedef struct tok {
    int32_t i;
    ubyte operator;
    char *raw;
} tok;

typedef struct stringNode tokenList;
struct stringNode {
    struct stringNode *next;
    tok *token;
};

typedef struct node list;
struct node {
    struct node *next;
    tokenList *tokens;
    char *raw;
    char *name;
};

typedef struct clac_file_header clac_file;
struct clac_file_header {
    list *functions;
    tokenList *mainFunction;
    int functionCount;
};

enum instructions {
    PRINT = 0x01,
    QUIT  = 0x02,
    PLUS  = 0x03,
    MINUS = 0x04,
    MULT  = 0x05,
    DIV   = 0x06,
    MOD   = 0x07,
    LT    = 0x09,
    DROP  = 0x0A,
    SWAP  = 0x5F, /* to match c0vm.h */
    ROT   = 0x0C,
    IF    = 0x0D,
    PICK  = 0x0E,
    ELSE  = 0x0F,

    INT   = 0x10,
    UNK   = 0x11,
    UFUNC = 0x12,

    /* Special operands */
    UNUSED       = 0xEE,
    USED         = 0xEF,
    USER_DEFINED = 0xFF
};

/* --- Bytecode buffer for binary code generation --- */

typedef struct {
    ubyte *data;
    size_t len;
    size_t cap;
} codebuf;

/* --- Integer constant pool --- */

typedef struct {
    int32_t *values;
    size_t count;
    size_t cap;
} intpool;

/* --- Compilation context --- */

typedef struct {
    codebuf code;
    intpool ints;
    bool uses_print;
    int num_vars;
    bool has_error;
} compile_ctx;

#endif /* _CLACC_H_ */
