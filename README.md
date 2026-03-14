# clacc -- clac to C0 bytecode compiler

`clacc` compiles [clac](SPEC.md), a reverse polish notation (RPN) calculator language from
Carnegie Mellon's [15-122](https://www.cs.cmu.edu/~15122/) course, to C0 virtual machine
bytecode (`.bc0`). The output runs on the [C0VM](../c0vm/) interpreter.

## Building

```bash
make
```

Requires GCC with C99 support.

## Usage

```bash
# Compile to stdout
./clacc program.clac

# Compile to file
./clacc program.clac -o program.bc0

# Run with c0vm
./clacc program.clac -o program.bc0 && ../c0vm/c0vm program.bc0
```

## Quick Start

Create a file `hello.clac`:

```
42 print
1 2 + print
quit
```

Compile and run:

```bash
./clacc hello.clac -o hello.bc0
../c0vm/c0vm hello.bc0
```

Output:

```
42
3
```

## clac in Brief

clac is a stack-based calculator that uses reverse polish notation (postfix). Operands are
pushed onto a stack, operators consume from the top and push results back.

```
3 4 + 5 *       → (3 + 4) * 5 = 35
```

### Operations

| Category | Operations |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` `<` |
| Stack | `drop` `swap` `rot` `pick` |
| I/O | `print` `quit` |
| Control | `if` `else` |
| Functions | `: name body ;` |

### Control Flow

`if` pops the top value. If nonzero, the next token executes. If zero, 2 tokens are
skipped. Paired with `else` (which always skips 1 token), this creates an if-else:

```
: noop ;
: abs dup 0 < if negate else noop ;
```

### User-Defined Functions

All functions share the same data stack. `dup` is not built-in -- define it as `1 pick`:

```
: dup 1 pick ;
: square dup * ;
5 square print       → 25
```

## Examples

| File | Description |
|------|-------------|
| `def/demo-print.clac` | Basic stack operations and printing |
| `tests/math.clac` | Arithmetic, comparison, and stack manipulation |
| `def/fact.clac` | Recursive factorial with unit tests |
| `def/fib.clac` | Recursive Fibonacci (definitions only) |
| `def/fib2.clac` | Recursive Fibonacci with unit tests |
| `def/fibalt.clac` | Iterative Fibonacci using stack rotation |
| `def/sum.clac` | Recursive variadic sum |
| `def/square.clac` | Simple function definitions |

## Architecture

### Compilation Modes

**Inline mode** -- For programs without recursion. Function bodies are compiled at each
call site via recursive compilation. Since `if`/`else` each operate on single tokens, the
original program structure is preserved without a separate inlining pass.

**Heap-based stack mode** -- When the call graph contains cycles (recursion), the compiler
allocates a heap-resident integer array as the shared data stack. Each clac function
compiles to a separate C0 VM function receiving `(array_ptr, stack_ptr)` via `INVOKESTATIC`
and returning the updated stack pointer.

Mode selection is automatic via DFS-based cycle detection on the call graph.

### Pipeline

```
.clac source
    │
    ├── Parser (parse.c)
    │     Splits by ";", tokenizes, resolves function references
    │
    ├── Call graph analysis
    │     DFS cycle detection for recursion
    │
    ├── Code generation (clacc.c)
    │     Inline mode: recursive compilation at call sites
    │     Heap mode: per-function compilation with INVOKESTATIC
    │
    └── BC0 output
          Version 11 hex format for c0vm
```

### BC0 Output Format

The compiler targets C0 bytecode version 11 (64-bit architecture), compatible with the
C0VM interpreter. The output is a hex-encoded text file with:

- Integer constant pool
- String pool (for `println`)
- Function pool (1 function for inline mode, N+1 for heap mode)
- Native function pool (`printint`, `println`)

## Language Reference

See [SPEC.md](SPEC.md) for the complete clac language specification, including its origins
in CMU's 15-122 course.

## Project Structure

```
clacc/
├── clacc.c          Compiler: code generation, bc0 output
├── clacc.h          Shared types and data structures
├── parse.c          Parser: tokenization, function resolution
├── parse.h          Parser interface
├── Makefile         Build configuration
├── SPEC.md          clac language specification
├── lib/
│   ├── c0vm.h       C0 VM opcodes and bc0 format definitions
│   ├── hdict.c/h    Hash dictionary (function name lookup)
│   └── xalloc.c/h   Allocation utilities
├── def/             Example programs
└── tests/           Test programs
```
