# OurScheme — Lisp / Scheme-like Interpreter in C++

A small Lisp / Scheme-like interpreter implemented in modern **C++23**.

The project implements the main stages of an interpreter, including lexical analysis, S-expression parsing, an abstract syntax tree, runtime values, evaluation, environments, user-defined functions, error handling, and indented pretty printing.

## Features

- Interactive REPL
- Scanner / Lexer with token position tracking
- Recursive-descent S-expression parser
- Abstract Syntax Tree using `AtomNode` and `ConsNode`
- Proper lists and dotted pairs
- Quote syntax (`'expr` → `(quote expr)`)
- Multi-line S-expression input
- Runtime value system based on `std::variant`
- Global and local environments
- Variable definition and assignment
- User-defined functions with `lambda` and function-style `define`
- Arithmetic and comparison operations
- List operations and type predicates
- String operations
- `read`, `write`, `eval`, and other I/O-related procedures
- Structured parser and evaluator error handling
- Indented Pretty Print for Lisp values

---

## Interpreter Pipeline

```text
Source Input
    │
    ▼
 Scanner / Lexer
    │
    ▼
   Tokens
    │
    ▼
   Parser
    │
    ▼
Syntax Tree (AST)
    │
    ▼
 Evaluator
    │
    ▼
Runtime Value
    │
    ▼
Pretty Printer
```

---

## Scanner

The scanner converts source text into tokens while recording the token's row and column positions for error reporting.

Supported token categories include:

| Type | Examples |
| --- | --- |
| Parentheses | `(` `)` |
| Integer | `123`, `-10`, `+42` |
| Floating point | `1.25`, `.5`, `10.` |
| String | `"hello"` |
| Dot | `.` |
| Nil / False | `nil`, `#f` |
| True | `t`, `#t` |
| Quote | `'` |
| Symbol | `abc`, `+`, `define`, `hello?` |

Strings support the following escape sequences:

```text
\n    newline
\t    tab
\"    double quote
\\    backslash
```

A semicolon starts a comment and ignores the rest of the current line:

```scheme
(+ 1 2) ; this is a comment
```

---

## Abstract Syntax Tree

The syntax tree is represented using two node types:

- `AtomNode` — stores one atomic token
- `ConsNode` — stores a Lisp pair containing `car` and `cdr`

For example:

```scheme
(1 2 3)
```

is represented conceptually as:

```text
        Cons
       /    \
      1     Cons
           /    \
          2     Cons
               /    \
              3     nil
```

which is equivalent to:

```scheme
(1 . (2 . (3 . nil)))
```

The AST owns its child nodes with `std::unique_ptr`.

---

## Parser

The parser uses recursive-descent parsing to construct the syntax tree.

Supported syntax includes normal lists, nested lists, dotted pairs, atoms, and quote expressions.

### Proper List

```scheme
(1 2 3)
```

### Nested List

```scheme
(1 (2 3) 4)
```

### Dotted Pair

```scheme
(1 2 . 3)
```

### Quote

```scheme
'(1 2 3)
```

is internally converted to:

```scheme
(quote (1 2 3))
```

The parser also supports expressions that span multiple input lines.

---

## Runtime Values

Evaluation uses a separate runtime `Value` representation based on `std::variant`.

The interpreter currently supports:

- Integer
- Float
- String
- Symbol
- Nil / false
- True
- Pair
- Primitive procedure
- User-defined procedure
- Error object
- Message / internal values

Pairs use `std::shared_ptr`, allowing evaluated Lisp data structures to be shared safely at runtime.

---

## Pretty Print

Evaluated lists are printed recursively with indentation.

Example input:

```scheme
'(1 (2 3) 4)
```

Example output:

```text
( 1
  ( 2
    3
  )
  4
)
```

Dotted pairs are also printed explicitly:

```scheme
'(1 2 . 3)
```

```text
( 1
  2
  .
  3
)
```

Floating-point values are displayed with three digits after the decimal point.

---

## Special Forms

The evaluator currently supports the following special forms:

| Form | Purpose |
| --- | --- |
| `quote` | Return an expression without evaluating it |
| `define` | Define a global variable or function |
| `and` | Short-circuit logical AND |
| `or` | Short-circuit logical OR |
| `if` | Conditional evaluation |
| `cond` | Multi-branch conditional evaluation |
| `begin` | Evaluate multiple expressions in sequence |
| `lambda` | Create a user-defined procedure |
| `let` | Create local bindings |
| `set!` | Change a binding |
| `clean-environment` | Clear global user-defined bindings |

### Variable Definition

```scheme
(define x 10)
(+ x 5)
```

### Function Definition

```scheme
(define (square x)
  (* x x))

(square 5)
```

### Lambda

```scheme
((lambda (x)
   (* x x))
 5)
```

### Let

```scheme
(let ((x 10)
      (y 20))
  (+ x y))
```

### Conditional

```scheme
(if (> 10 5)
    "yes"
    "no")
```

```scheme
(cond
  ((< 10 5) "small")
  ((= 10 10) "equal")
  (else "other"))
```

---

## Built-in Procedures

### List Operations

```text
cons
list
car
cdr
```

Examples:

```scheme
(cons 1 2)
(list 1 2 3)
(car '(1 2 3))
(cdr '(1 2 3))
```

### Type Predicates

```text
atom?
pair?
list?
null?
integer?
real?
number?
string?
boolean?
symbol?
```

Predicates return `#t` when true and `nil` when false.

### Arithmetic

```text
+
-
*
/
```

Arithmetic procedures require at least two arguments.

Examples:

```scheme
(+ 1 2 3)
(* 2 3 4)
(- 10 3)
(/ 20 4)
```

Operations remain integer-based when all operands are integers. If any operand is a floating-point value, the result is evaluated as a floating-point value.

### Numeric Comparison

```text
>
>=
<
<=
=
```

Example:

```scheme
(< 1 2 3)
```

### Boolean Operation

```text
not
```

In this interpreter, `nil` represents false; other values are treated as true.

### String Operations

```text
string-append
string>?
string<?
string=?
symbol->string
number->string
```

Examples:

```scheme
(string-append "Hello, " "world!")
(string=? "abc" "abc")
(number->string 123)
```

### Equality

```text
eqv?
equal?
```

`eqv?` performs identity/value-oriented comparison depending on the runtime type, while `equal?` recursively compares pair structures and string contents.

### Error Objects

```text
create-error-object
error-object?
```

### Input / Output and Evaluation

```text
read
write
display-string
newline
eval
```

### Interpreter Control

```text
verbose
verbose?
exit
clean-environment
```

`verbose` controls messages such as successful definitions and environment cleanup.

---

## Error Handling

The interpreter contains separate parser and evaluator error systems.

### Parser Errors

Examples include:

- unexpected token
- expected atom or `(`
- expected `)`
- unclosed string
- incomplete S-expression

Token row and column information is tracked so syntax errors can identify the source location.

### Evaluation Errors

Handled evaluation errors include:

- unbound symbol
- non-list expression
- incorrect number of arguments
- incorrect argument type
- attempting to apply a non-function
- no return value
- unbound parameter
- invalid test condition
- division by zero
- invalid `define` format
- invalid `cond` format
- invalid `lambda` format
- invalid `let` format
- invalid `set!` format
- invalid nesting level for `define`
- invalid nesting level for `clean-environment`
- invalid nesting level for `exit`

---

## REPL

After startup, the interpreter repeatedly reads one S-expression, parses it, evaluates it, and prints the result.

```text
Welcome to OurScheme!

> (+ 1 2)
3

> (list 1 2 3)
( 1
  2
  3
)
```

Multiple expressions may remain buffered from the same input stream and are consumed one at a time.

To leave the interpreter:

```scheme
(exit)
```

The program prints:

```text
Thanks for using OurScheme!
```

EOF is also detected and handled explicitly.

> **Note:** the current `main()` reads and discards the first input line as a test-case number before displaying the REPL welcome message. When running the program manually, enter a value such as `1` on the first line before entering Scheme expressions.

---

## Build Requirements

Because the source uses modern library features such as `<print>`, `std::println`, `std::format`, ranges, and `std::variant`, compile it with a compiler and standard library that provide the required **C++23** support.

Example with a recent GCC version:

```bash
g++ -std=c++23 -Wall -Wextra -Wshadow -Werror main.cpp -o ourscheme
```

For debugging, sanitizers can also be enabled:

```bash
g++ -std=c++23 \
    -Wall \
    -Wextra \
    -Wshadow \
    -Werror \
    -fsanitize=address,undefined \
    main.cpp \
    -o ourscheme
```

Run on Linux / macOS:

```bash
./ourscheme
```

Run on Windows PowerShell:

```powershell
.\ourscheme.exe
```

---

## Source Code Architecture

```text
main.cpp
│
├── TokenType / Token
│
├── Scanner
│   ├── GetToken()
│   ├── ClassifyToken()
│   ├── ScanString()
│   └── SkipWhitespace()
│
├── Syntax Tree
│   ├── AstNode
│   ├── AtomNode
│   └── ConsNode
│
├── Parser
│   ├── ParseOneExpression()
│   ├── ParseExpression()
│   ├── ParseQuote()
│   └── ParseList()
│
├── Runtime Values
│   ├── Value
│   ├── PairValue
│   ├── ClosureValue
│   └── Environment
│
├── Value Printer
│   └── PrintValue()
│
├── Evaluator
│   ├── Evaluate()
│   ├── EvaluateList()
│   ├── Special Forms
│   └── Built-in Procedures
│
├── Error Handling
│   ├── ParseError
│   └── EvalError
│
├── Input Handling
│   ├── ScanLine()
│   ├── ReadAndAppendLine()
│   └── RebasePendingTokens()
│
└── main()
    └── REPL
```

---

## Example Session

```text
1
Welcome to OurScheme!

> (define x 10)
x defined

> (+ x 20)
30

> (define (square x) (* x x))
square defined

> (square 5)
25

> '(1 2 . 3)
( 1
  2
  .
  3
)

> (exit)

Thanks for using OurScheme!
```

---

## Project Goal

This project is intended as a hands-on implementation of the major components of a programming-language interpreter:

- lexical analysis
- parsing
- recursive data structures
- abstract syntax trees
- expression evaluation
- environments and bindings
- Lisp pairs and lists
- function calls
- runtime type representation
- error handling
- REPL design

The implementation is intentionally built from the ground up in C++ to demonstrate how Lisp-like languages can be represented and evaluated internally.

---

## Language

- C++23

## Author

CYCU Computer Science Project
