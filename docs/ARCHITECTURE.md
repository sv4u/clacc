# clacc Architecture

This document describes the internal architecture of the `clacc` compiler, covering the
compilation pipeline, data structures, compilation modes, and bytecode output format.

## Compilation Pipeline

```
┌─────────────────┐
│  .clac source   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐    parse.c
│     Parser      │    - Read file, strip // comments
│                 │    - Split by ";" into segments
│  1. Tokenize    │    - Classify tokens (INT, operator, UFUNC, UNK)
│  2. Resolve     │    - Two-pass function reference resolution
└────────┬────────┘
         │  clac_file { functions, mainFunction, functionCount }
         ▼
┌─────────────────┐    clacc.c
│ Call Graph       │    - DFS over UFUNC references
│ Analysis         │    - Detect cycles → recursion
└────────┬────────┘
         │  bool recursive
         ├────────────────────────────┐
         ▼                            ▼
┌─────────────────┐          ┌─────────────────┐
│  Inline Mode    │          │ Heap-Stack Mode  │
│  (no recursion) │          │ (with recursion) │
│                 │          │                  │
│ Single function │          │ N+1 functions    │
│ Recursive call- │          │ INVOKESTATIC     │
│ site inlining   │          │ Shared heap array│
└────────┬────────┘          └────────┬────────┘
         │                            │
         ▼                            ▼
┌─────────────────────────────────────────┐
│              BC0 Output                  │
│  Hex-encoded text file for C0 VM         │
│  Version 11 (64-bit architecture)        │
└──────────────────────────────────────────┘
```

## Parser Internals (`parse.c`)

The parser transforms a `.clac` source file into a `clac_file` structure through several
stages:

### Stage 1: File Reading and Comment Stripping

The entire file is read into a single buffer. Line comments (`// ...`) are replaced with
spaces in-place by `strip_line_comments()`. This avoids complicating the tokenizer with
comment handling.

### Stage 2: Segment Splitting (`splitFile`)

The buffer is split on semicolons (`;`), producing a linked list of segments. Each segment
is either a function definition (beginning with `:`) or the main body (the final segment).
The segment count is stored in `clac_file.functionCount`.

### Stage 3: Tokenization (`tokenizeFunction`)

Each segment is tokenized by splitting on whitespace. Tokens are classified as:

| Classification | Condition | `operator` field |
|----------------|-----------|-----------------|
| Integer literal | `strtol` succeeds | `INT` (0x10) |
| Built-in operator | Matches keyword (`+`, `print`, `if`, etc.) | Specific opcode |
| Function header | Token is `:` | `USER_DEFINED` (0xFF) |
| Known function | Found in hash dictionary | `UFUNC` (0x12) |
| Unknown | Not recognized | `UNK` (0x11) |

Function names are registered in the hash dictionary (`hdict`) during tokenization. The
`comment` function receives special treatment: its body is not tokenized (allowing
free-form text).

### Stage 4: Reference Resolution (`fixFunctionRefs`)

A second pass over all token lists resolves `UNK` tokens. If a previously-unknown token
now appears in the hash dictionary (because it was defined in a later segment), it is
upgraded to `UFUNC` with the correct function index. Unresolvable tokens in non-comment
functions trigger an "undefined identifier" error.

## Call Graph Analysis

Before code generation, `detect_recursion()` performs DFS-based cycle detection on the
call graph:

1. Each function that contains `UFUNC` tokens forms an edge in the call graph.
2. Standard DFS with an `on_stack` array detects back edges (cycles).
3. The main body is checked as an additional root.
4. If any cycle exists, the compiler switches to heap-based stack mode.

This analysis runs in O(V + E) time where V is the function count and E is the total
number of function call tokens.

## Inline Mode (No Recursion)

When no recursion is detected, the compiler produces a single BC0 function. User-defined
function calls are compiled by recursively expanding the function body at each call site.

### Key Characteristics

- **Single function output**: The entire program compiles to one BC0 function (function
  index 0) with `num_args = 0`.
- **Recursive compilation**: `compile_n_tokens()` calls itself when it encounters a `UFUNC`
  token, inlining the called function's body.
- **Token-level control flow**: `if` and `else` operate on individual tokens (not blocks),
  matching the original clac interpreter semantics.
- **Compile-time pick**: When an `INT` token is immediately followed by `PICK`, the
  compiler generates a fixed-depth stack copy using VSTORE/VLOAD sequences instead of a
  runtime loop.

### Control Flow Compilation

The `if` token generates:

```
BIPUSH 0
IF_CMPEQ <offset-to-false>    ; jump if top == 0
<then-token bytecode>
GOTO <offset-to-end>           ; (only if else follows)
<else-token bytecode>           ; (only if else follows)
```

Each branch compiles exactly one source token (which may be a function call, expanding to
many bytecodes).

## Heap-Based Stack Mode (With Recursion)

When recursion is detected, the compiler allocates a heap-resident integer array as the
shared data stack. Each clac function compiles to a separate BC0 function.

### Architecture

- **V[0]** = array pointer (the shared data stack)
- **V[1]** = stack pointer index (integer offset into the array)
- **V[2+]** = temporary variables for rot, swap, pick

Every clac operation translates to a sequence that manipulates the heap array through
array load/store instructions:

| clac operation | Heap mode implementation |
|----------------|------------------------|
| Integer literal | `heap_push_int()`: push value, increment sp |
| `+`, `-`, etc. | `heap_binop()`: pop two, operate, push result |
| `drop` | `heap_drop()`: decrement sp |
| `swap` | `heap_swap()`: pop two to temps, push in swapped order |
| `rot` | `heap_rot()`: pop three to temps, push rotated |
| `pick` | `heap_pick()`: read arr[sp - n] and push |
| Function call | INVOKESTATIC with (arr, sp) arguments |

### Function Calling Convention

Each compiled function:

- Receives 2 arguments: `V[0]` = array pointer, `V[1]` = stack pointer
- Returns the updated stack pointer as its return value
- The caller saves and restores its own `V[0]` and `V[1]` around the call

The `quit` operation returns a negative sentinel value (-1) as the stack pointer. Callers
propagate this by checking `sp < 0` after each `INVOKESTATIC`.

### Function Mapping

The compiler builds a mapping from clac function indices to BC0 function pool indices:

- BC0 function 0 = main (always present)
- BC0 functions 1, 2, ... = non-comment user-defined functions in source order
- Comment functions are mapped to -1 (skipped)

## BC0 Output Format

The compiler targets C0 bytecode version 11 (64-bit architecture). The output is a
hex-encoded text file with this structure:

```
C0 C0 FF EE              # magic number
00 17                     # version: (11 << 1) | 1 = 0x17

00 02                     # int pool count
00 00 00 2A               # int_pool[0] = 42
00 00 04 00               # int_pool[1] = 1024

00 01                     # string pool byte count
00                        # "" (empty string for println)

00 01                     # function count

# <main>
00                        # num_args = 0
02                        # num_vars = 2
00 0A                     # code_length = 10
10 2A 59 60 B7 00 00 57   # bytecode...
14 00 00 B7 00 01 57 B0

00 02                     # native count
00 01 00 09               # printint (1 arg, table index 9)
00 01 00 0A               # println  (1 arg, table index 10)
```

### Native Functions

The compiler emits at most two native function stubs:

- **printint** (index 0x0009, 1 argument): prints an integer without a newline
- **println** (index 0x000A, 1 argument): prints a string followed by a newline

The `print` clac operation emits: `INVOKENATIVE printint`, `POP`, `ALDC ""`,
`INVOKENATIVE println`, `POP` -- printing the integer followed by a newline.

## Source File Responsibilities

| File | Role |
|------|------|
| `clacc.c` | Code generation (inline + heap modes), BC0 output, `main()` |
| `clacc.h` | Shared types: `tok`, `tokenList`, `list`, `clac_file`, `codebuf`, `intpool`, `compile_ctx` |
| `parse.c` | File reading, comment stripping, tokenization, function reference resolution |
| `parse.h` | Parser interface (`parse()` function) |
| `lib/hdict.c/h` | Hash dictionary for function name lookup |
| `lib/xalloc.c/h` | Checked allocation wrappers |
| `lib/c0vm.h` | C0 VM opcode definitions and BC0 format structures |
| `lib/c0vm_abort.h` | Runtime error reporting functions |
| `lib/contracts.h` | Debug assertion macros |

## c0vm-lite Interpreter

The `tools/c0vm-lite/` directory contains a minimal C0 bytecode interpreter that supports
only the ~24 opcodes emitted by `clacc`. It is used for Tier 1 testing.

### Supported Opcodes

| Category | Opcodes |
|----------|---------|
| Constants | NOP, BIPUSH, ILDC, ALDC |
| Local variables | VLOAD, VSTORE |
| Stack | POP, DUP, SWAP |
| Arithmetic | IADD, ISUB, IMUL, IDIV, IREM |
| Memory | IMLOAD, IMSTORE, AADDS, NEWARRAY |
| Control flow | IF_CMPEQ, IF_ICMPLT, GOTO |
| Functions | INVOKESTATIC, INVOKENATIVE, RETURN |

### Design Decisions

- **Tagged values**: Uses a `c0val` tagged union (VAL_INT / VAL_PTR) to track value types
  and catch type errors at runtime.
- **Array-based stacks**: Both the operand stack and call stack use fixed-size arrays
  (4096 and 1024 entries respectively).
- **Hex parser**: Reads the hex-encoded BC0 format directly, skipping comments (lines
  starting with `#`) and whitespace.
