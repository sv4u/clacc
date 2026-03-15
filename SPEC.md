# clac Language Specification

## Overview

**clac** is a reverse polish notation (RPN) calculator language originating from Carnegie
Mellon University's **15-122: Principles of Imperative Computation** course. The name
"clac" is "calc" spelled backwards. In 15-122, students implement a clac interpreter in
[C0](https://c0.cs.cmu.edu/docs/c0-reference.pdf) (a safe subset of C) as a programming
assignment that exercises stacks, queues, and hash dictionaries.

`clacc` is a compiler that translates clac programs to C0 bytecode (`.bc0`), which is then
executed by the C0 virtual machine (`c0vm`).

## How It Works

clac evaluates expressions in **postfix notation** (also called reverse polish notation).
Operands are pushed onto a stack, and operators consume their arguments from the stack and
push results back. For example, the infix expression `(3 + 4) * 5` is written as:

```
3 4 + 5 *
```

This pushes 3, pushes 4, adds them (result 7), pushes 5, and multiplies (result 35).

## Lexical Structure

A clac program is a sequence of **tokens** separated by whitespace (spaces, newlines, tabs).
Tokens are one of:

- **Integer literals**: sequences of digits, optionally preceded by `-` (e.g., `42`, `-7`, `0`)
- **Operators**: built-in operation keywords (e.g., `+`, `print`, `if`)
- **Function names**: user-defined identifiers that are not integer literals or operators
- **Function definitions**: delimited by `:` ... `;`

Semicolons (`;`) separate function definitions from each other and from the main body.

### Comments

**Line comments** start with `//` and extend to the end of the line:

```
// This is a comment
42 print   // inline comment
```

**Legacy comments** use the `: comment ... ;` convention from the original 15-122
assignment. A function named `comment` may contain arbitrary tokens without triggering
an undefined-identifier error:

```
: comment this function's body is treated as free-form text ;
```

Both forms are supported. Line comments with `//` are preferred for new code.

## Data Types

clac operates on **32-bit signed integers**. All stack elements and arithmetic use `int32_t`
semantics with two's complement representation.

## Stack Operations

The data stack is the central data structure. Operations consume values from the top of the
stack and push results back.

### Literals

An integer literal pushes its value onto the stack.

```
42          → stack: [42]
5 10        → stack: [5, 10]
```

### Arithmetic

All arithmetic operations pop two operands from the stack and push the result.
The first operand pushed is the left operand:

| Operation | Syntax | Effect | Description |
|-----------|--------|--------|-------------|
| Add       | `+`    | `a b → (a+b)` | Integer addition |
| Subtract  | `-`    | `a b → (a-b)` | Integer subtraction |
| Multiply  | `*`    | `a b → (a*b)` | Integer multiplication |
| Divide    | `/`    | `a b → (a/b)` | Integer division (truncates toward zero) |
| Modulo    | `%`    | `a b → (a%b)` | Integer remainder |
| Less-than | `<`    | `a b → (a<b ? 1 : 0)` | Comparison, pushes 1 or 0 |

```
5 3 -       → 5 - 3 = 2
10 3 /      → 10 / 3 = 3
3 5 <       → 1  (3 < 5 is true)
```

### Stack Manipulation

| Operation | Syntax | Effect | Description |
|-----------|--------|--------|-------------|
| Drop      | `drop` | `a →` | Remove the top element |
| Swap      | `swap` | `a b → b a` | Swap the top two elements |
| Rotate    | `rot`  | `a b c → b c a` | Rotate third element to top |
| Pick      | `pick` | `... xN ... x1 N → ... xN ... x1 xN` | Pop N, copy Nth element (1-indexed) to top |

**pick** pops N from the stack at runtime, then copies the Nth element (counted from the
new top, 1-indexed) without removing it:

```
: dup 1 pick ;       → duplicates the top element (N=1)
: over 2 pick ;      → copies the second element to the top (N=2)
```

### I/O

| Operation | Syntax | Effect | Description |
|-----------|--------|--------|-------------|
| Print     | `print` | `a →` | Pop and print the top value, followed by a newline |
| Quit      | `quit`  | `→`   | Exit the program immediately |

## Control Flow

### if

`if` pops the top of the stack. If the value is **nonzero**, execution continues normally.
If the value is **zero**, the next **two** tokens are skipped (dequeued and discarded).

### else

`else` unconditionally skips the next **one** token.

### The if-else Pattern

When combined, `if` and `else` form a standard conditional construct:

```
<condition> if <then-token> else <else-token>
```

**Nonzero (true) path**: Execute `<then-token>`, encounter `else` which skips
`<else-token>`.

**Zero (false) path**: `if` skips `<then-token>` and the `else` keyword, landing on
`<else-token>` which executes normally.

Each branch is a **single token**, typically a user-defined function call.

#### Example

```
: noop ;
: negate 0 swap - ;
: abs dup 0 < if negate else noop ;
```

This computes the absolute value:

- `dup 0 <`: check if negative
- If negative (nonzero): `negate` runs, `else` skips `noop`
- If non-negative (zero): `if` skips `negate` and `else`, `noop` runs

#### Common Patterns

Conditional with a no-op else:

```
<condition> if <action> else noop
```

Conditional with cleanup on the false path:

```
<condition> if <continue-action> else drop
```

## User-Defined Functions

### Syntax

```
: <name> <body> ;
```

Defines a function named `<name>` whose body is a sequence of tokens. The `:` and `;`
delimiters separate function definitions. The function name must not be an integer literal
or a built-in operator name.

### Calling

A user-defined function is called by writing its name as a token:

```
: square dup * ;
5 square print     → prints 25
```

### Shared Stack

All functions operate on the same shared data stack. A function call does not create a new
stack frame for data -- it directly manipulates the caller's stack. This is the defining
characteristic of clac's execution model, inherited from stack-based languages like Forth.

### Recursion

Functions may call themselves or other functions recursively:

```
: fact 1 swap fact_return? ;
: fact_return? dup if fact_body else drop ;
: fact_body dup rot * swap 1 - fact_return? ;
```

### Standard Library Idioms

Common functions typically defined by the programmer:

```
: dup 1 pick ;           → Duplicate top element
: noop ;                 → Do nothing (empty function body)
: over 2 pick ;          → Copy second element to top
: negate 0 swap - ;      → Negate top element
```

Note that `dup` is **not** a built-in -- it is implemented as `1 pick`.

## Program Structure

A clac program consists of:

1. Zero or more function definitions (each delimited by `:` and `;`)
2. A main body (the final segment after the last `;`)

The main body is executed when the program runs. The program exits when:

- The `quit` operation is executed
- The main body finishes (returning the top of the stack, or 0 if empty)

### Example Program

```
: comment factorial function ;
: dup 1 pick ;
: noop ;
: fact 1 swap fact_return? ;
: fact_return? dup if fact_body else drop ;
: fact_body dup rot * swap 1 - fact_return? ;

5 fact print
quit
```

Output: `120`

## Compilation

The `clacc` compiler translates clac source to C0 bytecode (`.bc0` format), which is
executed by the C0 virtual machine (`c0vm`).

### Compilation Modes

**Inline mode** (no recursion): User-defined function bodies are compiled at each call
site (recursive compilation). Since `if`/`else` branches are single tokens, the original
program structure is preserved without a separate inlining pass.

**Heap-based stack mode** (with recursion): When the compiler detects recursive call
chains via call graph analysis, it allocates a heap-based integer array as the shared data
stack. Each function is compiled as a separate C0 VM function that receives the array
pointer and stack pointer as arguments via `INVOKESTATIC`.

The compiler automatically selects the appropriate mode.

## Formal Grammar

```
program     ::= (funcdef ";")* main_body
funcdef     ::= ":" NAME token*
main_body   ::= token*
token       ::= INTEGER | OPERATOR | NAME
comment     ::= "//" (any character except newline)* newline
INTEGER     ::= "-"? [0-9]+
OPERATOR    ::= "+" | "-" | "*" | "/" | "%" | "<"
              | "print" | "quit" | "drop" | "swap" | "rot"
              | "if" | "else" | "pick"
NAME        ::= (non-integer, non-operator identifier)
```

## Error Handling

The compiler reports errors and exits with a nonzero code for:

- **Undefined identifiers**: tokens that are not integers, operators, or defined functions
- **Missing function name**: `:` not followed by a name (e.g., `: ;`)
- **Numeric function names**: `: 42 ... ;` (function names must not be pure integers)
- **Nested function definitions**: `: foo : bar ... ; ;`
- **Incomplete control flow**: `if` or `else` at the end of input without enough tokens
- **Empty function body**: a user-defined function with no tokens (e.g., `: foo ;`)
- **File not found**: source file does not exist

## Origins

clac is a programming assignment from **15-122: Principles of Imperative Computation** at
Carnegie Mellon University. In the course, students implement a clac interpreter in C0 using
stacks, queues, and hash dictionaries. The `clacc` compiler extends this concept by
compiling clac programs to C0 bytecode rather than interpreting them directly.

For more on 15-122 and C0, see [https://c0.cs.cmu.edu/](https://c0.cs.cmu.edu/).
