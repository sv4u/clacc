# Development Guide

This guide covers setting up a development environment, building, testing, generating
code coverage reports, and producing API documentation for the `clacc` project.

## Prerequisites

### Required

| Tool | Version | Purpose |
|------|---------|---------|
| GCC or Clang | C99 support | Compiling clacc and c0vm-lite |
| GNU Make | 3.81+ | Build system |

### Optional

| Tool | Version | Purpose |
|------|---------|---------|
| lcov | 1.14+ | Code coverage report generation |
| Doxygen | 1.9+ | API documentation generation |
| Graphviz | any | Call graphs in Doxygen output (set `HAVE_DOT=YES` in Doxyfile) |

### Platform-Specific Setup

#### macOS

```bash
# Xcode command line tools provide clang and make
xcode-select --install

# Optional: install lcov and doxygen via Homebrew
brew install lcov doxygen graphviz
```

#### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install build-essential gcc make

# Optional: install lcov and doxygen
sudo apt-get install lcov doxygen graphviz
```

#### Fedora / RHEL

```bash
sudo dnf install gcc make

# Optional
sudo dnf install lcov doxygen graphviz
```

## Building

### Build the Compiler

```bash
make            # uses gcc by default
make CC=clang   # or use clang
```

This produces the `clacc` binary in the project root.

### Build the Bytecode Runner

```bash
make c0vm-lite
```

This builds `tools/c0vm-lite/c0vm-lite`, a minimal C0 bytecode interpreter that
supports the ~24 opcodes emitted by `clacc`.

### Build Everything

```bash
make clacc c0vm-lite
```

### Clean

```bash
make clean
```

## Running

```bash
# Compile a clac program to stdout
./clacc program.clac

# Compile to a file
./clacc program.clac -o program.bc0

# Run with c0vm-lite
./clacc program.clac -o program.bc0
./tools/c0vm-lite/c0vm-lite program.bc0
```

## Testing

### Test Suite Overview

The test suite lives in `tests/` and contains 43 test cases organized by category:

| Category | Count | Description |
|----------|-------|-------------|
| `arithmetic/` | 7 | Arithmetic operators (`+`, `-`, `*`, `/`, `%`, `<`) |
| `stack/` | 6 | Stack operations (`drop`, `swap`, `rot`, `pick`) |
| `control_flow/` | 6 | `if`/`else` conditionals |
| `functions/` | 5 | User-defined functions, recursion |
| `error/` | 8 | Expected compile-time and runtime errors |
| `edge_cases/` | 3 | Comments, large numbers, boundary conditions |
| `regression/` | 7 | Regression tests from example programs |
| root | 1 | `math.clac` |

### Running Tests

```bash
# Run the full test suite (Tier 1: clacc + c0vm-lite)
make test

# Verbose output (shows individual test results)
./tests/run_tests.sh --verbose

# Filter by category
./tests/run_tests.sh --filter stack
./tests/run_tests.sh --filter error

# Run with a full c0vm (Tier 2)
make test-full C0VM=/path/to/c0vm
```

### Test Format

Each test is a `.clac` file with either:

- A companion `.expected` file containing the expected stdout output, or
- An embedded `// TEST:` directive:

| Directive | Meaning |
|-----------|---------|
| `// TEST: expect_output` | Compare stdout against `.expected` file |
| `// TEST: expect_success` | Expect exit code 0 |
| `// TEST: expect_error` | Expect nonzero exit code |
| `// TEST: expect_compile_error` | Expect compilation to fail |

### Adding a New Test

1. Create a `.clac` file in the appropriate category directory under `tests/`.
2. Add the expected behavior:
   - For output tests: create a matching `.expected` file.
   - For other tests: add a `// TEST:` directive as the first comment.
3. Run `make test` to verify.

Example:

```bash
# tests/arithmetic/my_test.clac
echo '3 4 + print quit' > tests/arithmetic/my_test.clac
echo '7' > tests/arithmetic/my_test.expected
make test
```

## Code Coverage

Code coverage measures which lines and branches of `clacc` and `c0vm-lite` are exercised
by the test suite. This requires `lcov` (which includes `genhtml`).

### Generating a Local Coverage Report

```bash
make coverage
```

This performs the following steps:

1. Cleans previous coverage data and binaries
2. Rebuilds `clacc` and `c0vm-lite` with GCC's `--coverage` flag
3. Runs the full test suite (generating `.gcda` profiling data)
4. Collects coverage data with `lcov`
5. Generates an HTML report with `genhtml`
6. Prints a summary to the terminal

The HTML report is written to `coverage-report/index.html`. Open it in a browser:

```bash
open coverage-report/index.html          # macOS
xdg-open coverage-report/index.html      # Linux
```

### Full Coverage (Including c0vm)

To include the full c0vm interpreter in coverage analysis:

```bash
make coverage-full C0VM_DIR=../c0vm
```

This builds both `clacc` and `c0vm` with coverage instrumentation, runs Tier 2 tests,
and produces a combined report.

### Cleaning Coverage Artifacts

```bash
make coverage-clean
```

### Coverage in CI

The GitHub Actions CI workflow automatically generates a coverage report on every push
and pull request. The report is uploaded as a build artifact named `coverage-report`.

To download and view:

1. Navigate to the **Actions** tab in the GitHub repository.
2. Select the latest workflow run.
3. Under **Artifacts**, download `coverage-report`.
4. Extract and open `index.html`.

### Interpreting the Report

The coverage report shows:

- **Line coverage**: Percentage of source lines executed during testing.
- **Branch coverage**: Percentage of conditional branches taken (both true and false
  paths).
- **Function coverage**: Percentage of functions called at least once.

Color coding:

- **Green**: Covered (executed during tests)
- **Red**: Not covered (never executed)
- **Yellow**: Partially covered (some branches taken but not all)

Low-coverage areas may indicate:

- Missing test cases for specific features
- Error handling paths that lack corresponding error tests
- Dead code that can be removed

## API Documentation

The project uses [Doxygen](https://www.doxygen.nl/) for API documentation. All public
functions and data structures in the header files have Doxygen-style comments.

### Generating Documentation

```bash
make docs
```

This produces browsable HTML documentation in `docs/html/index.html`.

```bash
open docs/html/index.html          # macOS
xdg-open docs/html/index.html      # Linux
```

### Cleaning Documentation

```bash
make docs-clean
```

### Doxygen Configuration

The `Doxyfile` in the project root controls documentation generation. Key settings:

| Setting | Value | Purpose |
|---------|-------|---------|
| `INPUT` | `. lib` | Source directories to scan |
| `OPTIMIZE_OUTPUT_FOR_C` | `YES` | C-specific output formatting |
| `SOURCE_BROWSER` | `YES` | Include hyperlinked source listings |
| `HAVE_DOT` | `NO` | Set to `YES` if Graphviz is installed for call graphs |

## Project Structure

```
clacc/
├── clacc.c              Compiler: code generation, bc0 output, main()
├── clacc.h              Shared types: tok, tokenList, list, clac_file, codebuf
├── parse.c              Parser: file reading, tokenization, reference resolution
├── parse.h              Parser interface
├── Makefile             Build configuration with coverage and docs targets
├── Doxyfile             Doxygen configuration
├── SPEC.md              clac language specification
├── lib/
│   ├── c0vm.h           C0 VM opcodes and bc0 format definitions
│   ├── c0vm_abort.h     Runtime error reporting
│   ├── contracts.h      Debug assertion macros
│   ├── hdict.c/h        Hash dictionary (function name lookup)
│   └── xalloc.c/h       Checked allocation wrappers
├── tools/
│   └── c0vm-lite/       Minimal bytecode runner (~24 opcodes)
├── tests/
│   ├── run_tests.sh     Test harness
│   ├── arithmetic/      Arithmetic operator tests
│   ├── stack/           Stack operation tests
│   ├── control_flow/    if/else tests
│   ├── functions/       User-defined function and recursion tests
│   ├── error/           Expected error tests
│   ├── edge_cases/      Boundary condition tests
│   └── regression/      Regression tests from example programs
├── def/                 Example clac programs
├── docs/
│   ├── ARCHITECTURE.md  Compiler internals and design documentation
│   ├── DEVELOPMENT.md   This file
│   └── html/            Generated Doxygen output (gitignored)
└── .github/workflows/   CI/CD pipeline
```

## Development Workflow

1. **Make changes** to source files.
2. **Build**: `make`
3. **Test**: `make test`
4. **Check coverage**: `make coverage` (ensure new code is tested).
5. **Update docs**: `make docs` (if you changed public APIs).
6. **Commit** with a descriptive message following conventional commits.
