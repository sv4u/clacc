/**
 * @file c0vm.h
 * @brief C0 VM structs, function signatures, and opcodes.
 *
 * Defines the bc0 binary format structures, C0 value types, and the
 * complete C0 VM instruction set. Used by both the clacc compiler
 * (for code generation) and the c0vm/c0vm-lite interpreters.
 *
 * @authors William Lovas, Rob Simmons, Tom Cortina
 * @note Adapted from 15-122, Principles of Imperative Computation.
 */

#ifndef _C0VM_H_
#define _C0VM_H_

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include "c0vm_abort.h"

#define BYTECODE_VERSION 9

typedef int8_t byte;
/* typedef uint8_t ubyte; */

/** @name bc0 file format structures
 *  @{ */

/**
 * @brief Top-level representation of a loaded .bc0 bytecode file.
 *
 * Contains the magic number, version, integer and string constant pools,
 * and the function and native function tables.
 */
struct bc0_file {
  uint32_t magic;        /**< Magic number: 0xC0C0FFEE. */
  uint16_t version;      /**< Encoded version: (version_number << 1) | arch. */

  uint16_t int_count;    /**< Number of entries in the integer constant pool. */
  int32_t *int_pool;     /**< Integer constant pool (ILDC operands). */

  /** String pool stores all strings consecutively with NUL terminators. */
  uint16_t string_count;
  char *string_pool;     /**< Concatenated string literals (ALDC operands). */

  uint16_t function_count;              /**< Number of bytecode functions. */
  struct function_info *function_pool;  /**< Array of function descriptors. */
  uint16_t native_count;                /**< Number of native function stubs. */
  struct native_info *native_pool;      /**< Array of native function descriptors. */
};

/**
 * @brief Descriptor for a single bytecode function in the function pool.
 */
struct function_info {
  uint16_t num_args;     /**< Number of arguments (passed via INVOKESTATIC). */
  uint16_t num_vars;     /**< Total local variable slots (including args). */
  uint16_t code_length;  /**< Length of the bytecode array in bytes. */
  ubyte *code;           /**< Bytecode instruction stream. */
};

/**
 * @brief Descriptor for a native (FFI) function in the native pool.
 */
struct native_info {
  uint16_t num_args;             /**< Number of arguments. */
  uint16_t function_table_index; /**< Index into the C0 runtime function table. */
};

/** @} */

/** @name C0 value representation
 *  @{ */

/** @brief Discriminator for the c0_value tagged union. */
enum c0_val_kind { C0_INTEGER, C0_POINTER };

/**
 * @brief Tagged union representing a C0 runtime value.
 *
 * All operand stack entries, local variables, and function arguments
 * use this representation.
 */
typedef struct c0_value {
  enum c0_val_kind kind;   /**< C0_INTEGER or C0_POINTER. */
  union {
    int32_t i;   /**< Integer payload (when kind == C0_INTEGER). */
    void *p;     /**< Pointer payload (when kind == C0_POINTER). */
  } payload;
} c0_value;

/** @brief Wrap a 32-bit integer as a C0 value. */
static inline c0_value int2val(int32_t i) {
  c0_value v;
  v.kind = C0_INTEGER;
  v.payload.i = i;
  return v;
}

/** @brief Extract the integer from a C0 value, aborting on type mismatch. */
static inline int32_t val2int(c0_value v) {
  if (v.kind != C0_INTEGER)
    c0_memory_error("Invalid cast from c0_value (a pointer) to an integer");
  return v.payload.i;
}

/** @brief Wrap a pointer as a C0 value. */
static inline c0_value ptr2val(void *p) {
  c0_value v;
  v.kind = C0_POINTER;
  v.payload.p = p;
  return v;
}

/** @brief Extract the pointer from a C0 value, aborting on type mismatch. */
static inline void *val2ptr(c0_value v) {
  if (v.kind != C0_POINTER)
    c0_memory_error("Invalid cast from c0_value (an integer) to a pointer");
  return v.payload.p;
}

/** @brief Test two C0 values for equality (same kind and same payload). */
static inline bool val_equal(c0_value v1, c0_value v2) {
  return v1.kind == v2.kind && (v1.kind == C0_INTEGER
                                ? val2int(v1) == val2int(v2)
                                : val2ptr(v1) == val2ptr(v2));
}

/** @} */

/** @name C0 VM instruction opcodes
 *  @{ */

enum c0_instructions {
/* arithmetic operations */
  IADD = 0x60,
  IAND = 0x7E,
  IDIV = 0x6C,
  IMUL = 0x68,
  IOR = 0x80,
  IREM = 0x70,
  ISHL = 0x78,
  ISHR = 0x7A,
  ISUB = 0x64,
  IXOR = 0x82,

/* stack operations */
  DUP = 0x59,
  POP = 0x57,
  /* SWAP = 0x5F, */

/* memory allocation */
  NEWARRAY = 0xBC,
  ARRAYLENGTH = 0xBE,
  NEW = 0xBB,

/* memory access */
  AADDF = 0x62,
  AADDS = 0x63,
  IMLOAD = 0x2E,
  AMLOAD = 0x2F,
  IMSTORE = 0x4E,
  AMSTORE = 0x4F,
  CMLOAD = 0x34,
  CMSTORE = 0x55,

/* local variables */
  VLOAD = 0x15,
  VSTORE = 0x36,

/* constants */
  ACONST_NULL = 0x01,
  BIPUSH = 0x10,
  ILDC = 0x13,
  ALDC = 0x14,

/* control flow */
  NOP = 0x00,
  IF_CMPEQ = 0x9F,
  IF_CMPNE = 0xA0,
  IF_ICMPLT = 0xA1,
  IF_ICMPGE = 0xA2,
  IF_ICMPGT = 0xA3,
  IF_ICMPLE = 0xA4,
  GOTO = 0xA7,
  ATHROW = 0xBF,
  ASSERT = 0xCF,

/* function calls and returns */
  INVOKESTATIC = 0xB8,
  INVOKENATIVE = 0xB7,
  RETURN = 0xB0
};

/** @} */

/**
 * @brief In-memory representation of a C0 array.
 *
 * Used by NEWARRAY/AADDS/ARRAYLENGTH to implement heap-allocated
 * arrays in the C0 VM.
 */
struct c0_array_header {
  int count;      /**< Number of elements. */
  int elt_size;   /**< Size of each element in bytes. */
  void *elems;    /**< Pointer to the element storage. */
};
typedef struct c0_array_header c0_array;

/** @name Interface functions (used in c0vm-main.c)
 *  @{ */

/** @brief Load a .bc0 file from disk into a bc0_file structure. */
struct bc0_file *read_program(char *filename);

/** @brief Free all memory associated with a loaded bc0_file. */
void free_program(struct bc0_file *program);

/** @brief Execute the loaded bytecode program, returning main's exit code. */
int execute(struct bc0_file *bc0);

/** @} */

#endif /* _C0VM_H_ */
