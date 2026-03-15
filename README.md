# clacc -- clac to C0 bytecode compiler

[![CI](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)

`clacc` compiles [clac](SPEC.md), a reverse polish notation (RPN) calculator language from
Carnegie Mellon's [15-122](https://www.cs.cmu.edu/~15122/) course, to C0 virtual machine
bytecode (`.bc0`). The output runs on the [C0VM](../c0vm/) interpreter.

## Building

```bash
make            # build the compiler
make c0vm-lite  # build the minimal bytecode runner
make test       # build everything and run the test suite
make coverage   # generate code coverage report (requires lcov)
make docs       # generate API documentation (requires Doxygen)
```

Requires GCC or Clang with C99 support. See the [Development Guide](docs/DEVELOPMENT.md)
for platform-specific setup instructions.

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

### Comments

Line comments start with `//` and extend to the end of the line. The legacy
`: comment ... ;` convention is also supported.

```
// This is a comment
42 print   // inline comment
: comment traditional clac comment ;
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
| `def/demo-fail.clac` | Intentional runtime error (print from empty stack) |
| `tests/math.clac` | Arithmetic, comparison, and stack manipulation |
| `def/fact.clac` | Recursive factorial with unit tests |
| `def/fib.clac` | Recursive Fibonacci (definitions only) |
| `def/fib2.clac` | Recursive Fibonacci with unit tests |
| `def/fibalt.clac` | Iterative Fibonacci using stack rotation |
| `def/sum.clac` | Recursive variadic sum |
| `def/square.clac` | Simple function definitions |
| `def/brainfuck.clac` | Brainfuck operations experiment (work in progress) |

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

## Documentation

| Document | Description |
|----------|-------------|
| [SPEC.md](SPEC.md) | Complete clac language specification |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Compiler internals, pipeline, compilation modes |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Build setup, testing, coverage, and API docs |

## Language Reference

See [SPEC.md](SPEC.md) for the complete clac language specification, including its origins
in CMU's 15-122 course.

## Testing

The test suite covers arithmetic, stack operations, control flow, user-defined functions,
error handling, edge cases, and regressions. Tests are defined as `.clac` files with
companion `.expected` output files or embedded `// TEST:` directives.

```bash
make test                        # run all tests with c0vm-lite
make test-full C0VM=/path/to/c0vm  # run with a full c0vm
./tests/run_tests.sh --verbose   # show individual test results
./tests/run_tests.sh --filter stack  # run only stack tests
```

### Test tiers

**Tier 1** uses `c0vm-lite` (a minimal bytecode runner included in the repository) for
quick local testing and CI. It supports the 24 opcodes that `clacc` emits.

**Tier 2** uses a full C0VM implementation for rigorous testing and benchmarks. Developers
who have completed CMU 15-122 can point the test harness at their own `c0vm` binary.

## Code Coverage

Generate a local coverage report (requires [lcov](https://github.com/linux-test-project/lcov)):

```bash
make coverage
open coverage-report/index.html
```

Coverage reports are also generated automatically in CI and uploaded as build artifacts.
See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#code-coverage) for details on interpreting
the report and running full-VM coverage.

## CI/CD

GitHub Actions runs on every push and pull request:

- **Tier 1 (always)**: Builds `clacc` + `c0vm-lite`, runs the full test suite on
  Ubuntu (GCC, Clang) and macOS. No secrets required.
- **Coverage**: Builds with `--coverage`, runs tests, uploads an HTML coverage report
  as a build artifact.
- **Tier 2 (optional)**: When a `C0VM_DEPLOY_KEY` secret is configured, clones a private
  c0vm repository and runs the test suite with the full VM.

## Project Structure

```
clacc/
├── clacc.c              Compiler: code generation, bc0 output
├── clacc.h              Shared types and data structures
├── parse.c              Parser: tokenization, function resolution
├── parse.h              Parser interface
├── Makefile             Build, test, coverage, and docs targets
├── Doxyfile             Doxygen configuration for API docs
├── SPEC.md              clac language specification
├── lib/
│   ├── c0vm.h           C0 VM opcodes and bc0 format definitions
│   ├── hdict.c/h        Hash dictionary (function name lookup)
│   └── xalloc.c/h       Allocation utilities
├── tools/
│   └── c0vm-lite/       Minimal bytecode runner (24 opcodes)
├── tests/
│   ├── run_tests.sh     Test harness
│   ├── arithmetic/      Arithmetic operator tests
│   ├── stack/           Stack operation tests
│   ├── control_flow/    if/else tests
│   ├── functions/       User-defined function tests
│   ├── error/           Compile-time and runtime error tests
│   ├── edge_cases/      Overflow, comments, boundary tests
│   └── regression/      Regression tests from example programs
├── def/                 Example programs
├── docs/
│   ├── ARCHITECTURE.md  Compiler internals and design documentation
│   └── DEVELOPMENT.md   Development setup, testing, and coverage guide
└── .github/workflows/   CI/CD pipeline (build, test, coverage)
```
