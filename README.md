# ICC — ITU Compiler Collection

`icc` is a multi-phase compiler, written in C++17, for the **Citu (`C2`)** language.
The authoritative design document is the Phase 1 specification
[`doc/BSCS23143_Citu_Specification.md`](doc/BSCS23143_Citu_Specification.md) (v1.4).

Every compiler phase talks to the next one **only through an intermediate file on
disk** (Reader → Engine → Writer, spec §1.1). No phase hands C++ objects to another.

## Current status: Phases 2 and 3 complete

| Phase | Component | Status |
| :--- | :--- | :--- |
| 2 | Lexer → `.lex` (Lexer, LexWriter, LexReader) | Implemented and tested |
| 2 | Table-driven LL(1) parser → `.ast` (Parser, AstWriter, AstReader) | Implemented and tested |
| 3 | Symbol-table structures and `.sym` I/O (SymWriter, SymReader) | Implemented and tested |
| 3 | Two-pass semantic analyzer → `.sym`, with cross-file `.sym` imports | Implemented and tested |
| 2–3 | `icc` CLI driver (`.citu → .lex → .ast → .sym`) | Implemented and tested |
| 4–8 | Code generation, optimizers, linker / `.exe` | **Not implemented yet** |

What is implemented, per the v1.4 specification:

- **Lexer (§3.1):** 16 reserved keywords, 2 forbidden keywords (`class`, `do`), 33 terminals, and all five lexical diagnostic codes.
- **Parser (§3.2–3.3):** the 77-production grammar, driven by the 39 × 33 LL(1) table (186 populated cells, 0 conflicts). It is a stack-based table-driven parser, not recursive descent.
- **AST (§4.2):** all 23 node kinds, left-associative folding of `+ - * /`, and pre-order node IDs.
- **Semantic analysis (§3.4):**
  - **Pass 1** registers struct definitions and function headers, after importing Sections 1–2 of earlier units' `.sym` files.
  - **Pass 2** checks function bodies: scopes, function-wide unique names, strict type equality, `INT_LITERAL` adaptation, 64-bit constant folding, conditions, the `main` signature, return rules, `khali` and struct-value restrictions, and reserved names.
  - All 18 `SEMANTIC_ERROR_*` codes are produced.
  - Every AST node is annotated (§5.4).
- **File formats:** `.lex` (§5.2), `.ast` (§5.3) and `.sym` (§5.4), with full Reader validation.

## Requirements

- `g++` with C++17 support (tested with MinGW-w64 `g++ 8.1.0`, i686).
- GNU make: `make` from Git Bash/MSYS, or `mingw32-make` with a POSIX `sh` on `PATH`.

## Build

```sh
make            # builds bin/icc(.exe)
make tests      # builds all test executables into build/
```

Equivalent manual build (no make):

```sh
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude \
    src/lexer.cpp src/lex_writer.cpp src/lex_reader.cpp src/ast_schema.cpp \
    src/parser.cpp src/ast_writer.cpp src/ast_reader.cpp \
    src/sym_schema.cpp src/sym_writer.cpp src/sym_reader.cpp src/semantic_analyzer.cpp \
    src/main.cpp -o bin/icc
```

The code compiles with no warnings under `-std=c++17 -Wall -Wextra -pedantic`.

## Usage

```text
icc [options] <file.citu> [<file.citu> ...]

  -d, --out-dir <dir>     write .lex/.ast/.sym into <dir> (must exist); default: next to each source
  --stop-after <stage>    'lex', 'parse', or 'sema' (default)
  -q, --quiet             print diagnostics only
  -h, --help / --version
```

Example:

```sh
mkdir -p out
./bin/icc -d out examples/prog1_geometry.citu
# examples/prog1_geometry.citu: 122 tokens -> out/prog1_geometry.lex; 84 AST nodes -> out/prog1_geometry.ast; 9 symbols -> out/prog1_geometry.sym
```

Pipeline for each input:

1. The source is read, scanned by the **Lexer**, and written by **LexWriter** to `<stem>.lex`.
2. The **Parser** is given only the path of `<stem>.lex`. It rebuilds the tokens with **LexReader**, parses them with the LL(1) table, and **AstWriter** writes `<stem>.ast`, which is then re-read with **AstReader** as a self-check.
3. The **SemanticAnalyzer** reads:
   - `<stem>.ast` through **AstReader**;
   - `<stem>.lex` through **LexReader**, used only to recover each block's closing `khtm` line for `@SCOPE_DEF` `LINE_END`;
   - the `.sym` of every earlier file on the command line through **SymReader**.

   **SymWriter** then writes `<stem>.sym`.

**Multi-file programs.** The files on one command line form one program, compiled in order, and each file imports the structs and functions of the files before it. Providers must therefore come before consumers:

```sh
./bin/icc -d out tests/fixtures/multi_lib.citu tests/fixtures/multi_app.citu
```

- An imported struct or function appears in the importing `.sym` with `IS_EXTERNAL = 1` and its original `SOURCE_FILE`.
- An earlier file's `main` is never imported.
- If one file fails, the files after it are not compiled.

Independent programs need one `icc` invocation each.

**Outputs on failure.**
- Stale `<stem>.lex`/`<stem>.ast` from a previous run are removed before a file is compiled.
- `<stem>.sym` is written to `<stem>.sym.tmp` and renamed into place only after a successful write. A failed analysis therefore never leaves a new or partial `.sym`.
- A pre-existing `<stem>.sym` from an earlier run is never deleted. It is left untouched, and `icc` prints a note that it does not correspond to the current source.

Exit status: `0` success, `1` lexical/syntax/semantic/format error, `2` usage error, `3` I/O error.
Diagnostics use the specification's codes, for example:

```text
SEMANTIC_ERROR_UNDECLARED_IDENTIFIER in bad_semantic.citu at Line 3: undeclared identifier 'count'
SYNTAX_ERROR in bad_syntax.citu at Line 4, Col 5: Unexpected 'int32' (int32) while parsing T'; expected one of: ...
LEXICAL_ERROR_FORBIDDEN_CONSTRUCT in bad_lexical.citu at Line 1, Col 1: Forbidden construct 'class' is not allowed in Citu
```

`-o app.exe` (linking an executable) belongs to Phase 8 and is rejected by this build.

## Tests

```sh
make test          # all unit suites
make integration   # runs bin/icc on the samples and verifies the .lex/.ast/.sym it wrote
make check         # both
```

Run from the repository root. The suites read `examples/` and `tests/`. The Phase 2 and Stage 3A suites write scratch files under `bin/`, and `test_semantic` writes them under `build/test_semantic/`.

| Suite | Covers |
| :--- | :--- |
| `tests/test_lexer.cpp` | keywords, forbidden words, maximal munch, literals and limits, escapes, comments, coordinates, EOF position, illegal characters, sample programs |
| `tests/test_lex_writer.cpp` | `.lex` serialization, escaping, precondition rejection, I/O failure and reuse |
| `tests/test_lex_reader.cpp` | the full `.lex` validation protocol and corruption cases |
| `tests/test_parser.cpp` | LL(1) table dimensions and every cell count, golden `.ast` files from §5.3, the sample programs, precedence and associativity, all constructs, syntax errors, AstWriter/AstReader rejection cases, instance reuse, line numbers |
| `tests/test_sym_writer.cpp` | `.sym` serialization (golden `min_return.sym` from §5.4), layout and escaping, writer rejection of invalid tables, I/O failure and reuse |
| `tests/test_sym_reader.cpp` | the §5.4 SymReader validation protocol: header and counts, escapes, every invariant, round trip, reuse |
| `tests/test_semantic.cpp` | end-to-end golden `min_return.sym`, the sample programs, literal adaptation and constant folding, scopes and visibility, annotations for every node kind, cross-file imports, every `SEMANTIC_ERROR_*` code with its line, reuse, input integrity |
| `tests/test_integration.cpp` | re-reads the CLI's real `.lex`/`.ast`/`.sym` output, checks §2 counts and `NODE_TYPES == @TOTAL_NODES`, compares byte-for-byte with the spec golden files in `tests/golden/`, checks the two-file program, and cross-checks against independent runs |

The `integration` target also checks the CLI on failing inputs:
- lexical, syntax and semantic errors;
- that a pre-existing `.sym` is preserved;
- that a failure stops the later files of a multi-file program;
- `--stop-after parse`;
- usage errors.

Expected sample results (spec §2):

| Program | Tokens (incl. `$`) | AST nodes |
| :--- | ---: | ---: |
| `examples/prog1_geometry.citu` | 122 | 84 |
| `examples/prog2_payroll.citu` | 141 | 104 |
| `examples/prog3_grades.citu` | 144 | 107 |

## Repository layout

```text
include/   headers: token.h, ast.h, lexer.h, lex_writer.h, lex_reader.h, parser.h,
           ast_schema.h, ast_writer.h, ast_reader.h, sym.h, sym_schema.h,
           sym_writer.h, sym_reader.h, semantic_analyzer.h
src/       lexer.cpp, lex_writer.cpp, lex_reader.cpp, parser.cpp,
           ast_schema.cpp (shared §4.2 node schema), ast_writer.cpp, ast_reader.cpp,
           sym_schema.cpp (shared §5.4 rules), sym_writer.cpp, sym_reader.cpp,
           semantic_analyzer.cpp, main.cpp (CLI)
tests/     unit suites, test_integration.cpp, sym_test_support.h,
           fixtures/ (inputs), golden/ (spec §5.2-§5.4 files)
examples/  the three specification sample programs
doc/       Phase 1 specification (v1.4)
bin/       build output (icc); build/ holds objects, test binaries and test scratch output
```
