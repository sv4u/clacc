/**
 * @file clacc.h
 * @brief Shared types and data structures for the clacc compiler.
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

/**
 * @brief A single parsed token from the clac source.
 *
 * Each token carries an instruction tag (@c operator), an optional integer
 * payload (@c i), and a pointer to the original source text (@c raw).
 */
typedef struct tok {
    int32_t i;       /**< Integer value (for INT tokens) or function index (for UFUNC). */
    ubyte operator;  /**< Instruction tag from the @ref instructions enum. */
    char *raw;       /**< Original source text of this token (not owned). */
} tok;

/**
 * @brief Singly-linked list of tokens within a function body or main body.
 *
 * The first node is a sentinel; actual tokens begin at @c next.
 */
typedef struct stringNode tokenList;
struct stringNode {
    struct stringNode *next;  /**< Next token in the list, or NULL. */
    tok *token;               /**< Parsed token at this position. */
};

/**
 * @brief Singly-linked list of function segments from the parsed source.
 *
 * Each node represents one semicolon-delimited segment. The first node
 * is a sentinel; actual segments begin at @c next.
 */
typedef struct node list;
struct node {
    struct node *next;       /**< Next segment, or NULL. */
    tokenList *tokens;       /**< Token list for this segment (sentinel-headed). */
    char *raw;               /**< Raw source text of the segment (not owned). */
    char *name;              /**< Function name if this is a definition, else NULL. */
};

/**
 * @brief Top-level representation of a parsed clac program.
 *
 * Produced by parse() and consumed by the code generator in clacc.c.
 */
typedef struct clac_file_header clac_file;
struct clac_file_header {
    list *functions;           /**< Sentinel-headed list of function segments. */
    tokenList *mainFunction;   /**< Token list for the main body (last segment). */
    int functionCount;         /**< Total number of semicolon-delimited segments. */
};

/**
 * @brief Internal instruction tags assigned during tokenization.
 *
 * Values 0x01--0x0F correspond to built-in clac operators. INT (0x10)
 * marks integer literals, UNK (0x11) marks unresolved identifiers,
 * UFUNC (0x12) marks resolved user-defined function calls, and
 * USER_DEFINED (0xFF) marks the @c : token that begins a function
 * definition.
 */
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

/**
 * @brief Growable buffer for emitting C0 VM bytecode.
 *
 * Starts at 256 bytes and doubles on overflow. Managed by the
 * codebuf_init / emit / emit_i16_be helpers in clacc.c.
 */
typedef struct {
    ubyte *data;   /**< Heap-allocated bytecode bytes. */
    size_t len;    /**< Number of bytes currently written. */
    size_t cap;    /**< Allocated capacity in bytes. */
} codebuf;

/**
 * @brief Pool of 32-bit integer constants referenced by ILDC instructions.
 *
 * Duplicates are coalesced: intpool_add returns the existing index if
 * the value is already present.
 */
typedef struct {
    int32_t *values;  /**< Heap-allocated array of constant values. */
    size_t count;     /**< Number of constants currently stored. */
    size_t cap;       /**< Allocated capacity (number of int32_t slots). */
} intpool;

/**
 * @brief Mutable state threaded through the compilation pass.
 *
 * One compile_ctx is used per compiled function (or for the entire
 * program in inline mode). It accumulates bytecode, tracks which
 * features the program uses, and records the high-water mark for
 * local variable slots.
 */
typedef struct {
    codebuf code;        /**< Bytecode buffer for this function / program. */
    intpool ints;        /**< Shared integer constant pool. */
    bool uses_print;     /**< True if any print token was compiled. */
    int num_vars;        /**< High-water mark for VLOAD/VSTORE indices. */
    bool has_error;      /**< Set to true on any compilation error. */
    bool is_main_body;   /**< True when compiling the main entry function. */
} compile_ctx;

#endif /* _CLACC_H_ */
