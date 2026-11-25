# ICC — ITU Compiler Collection

`icc` is a multi-phase compiler, written in C++17, for the **Citu (`C2`)** language.
The authoritative design document is the Phase 1 specification
[`doc/BSCS23143_Citu_Specification.md`](doc/BSCS23143_Citu_Specification.md) (v1.4).

Every compiler phase talks to the next one **only through an intermediate file on
disk** (Reader → Engine → Writer, spec §1.1). No phase hands C++ objects to another.

## Current status: Phase 2 complete

| Phase | Component | Status |
| :--- | :--- | :--- |
| 2 | Lexer → `.lex` (Lexer, LexWriter, LexReader) | Implemented and tested |
| 2 | Table-driven LL(1) parser → `.ast` (Parser, AstWriter, AstReader) | Implemented and tested |
| 2 | `icc` CLI driver (`.citu → .lex → .ast`) | Implemented and tested |
| 3–8 | Semantic analysis, code generation, optimizers, linker | **Not implemented yet** |

The Phase 2 front end implements the exact v1.4 language:

- 16 reserved keywords, 2 forbidden keywords (`class`, `do`), 33 terminals, and all five lexical diagnostic codes (§3.1, §3.6).
- The 77-production grammar driven by the 39 × 33 LL(1) table (186 populated cells, 0 conflicts) (§3.2–3.3). The parser is a stack-based table-driven LL(1) parser, not recursive descent.
- AST construction with all 23 node kinds, left-associative folding of `+ - * /`, pre-order node IDs, and the §4.2-C line-number rules.
- The `.lex` (§5.2) and `.ast` (§5.3) formats. That covers `" | "` framing, Class A percent-escaping, and full Reader validation: format tag and version, record counts, the `@END_FILE` sentinel, token ordering, the six AST invariants, and the per-node schema.

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
    src/parser.cpp src/ast_writer.cpp src/ast_reader.cpp src/main.cpp -o bin/icc
```

The code compiles with no warnings under `-std=c++17 -Wall -Wextra -pedantic`.

## Usage

```text
icc [options] <file.citu> [<file.citu> ...]

  -d, --out-dir <dir>     write .lex/.ast into <dir> (must exist); default: next to each source
  --stop-after <stage>    'lex' (write .lex only) or 'parse' (default)
  -q, --quiet             print diagnostics only
  -h, --help / --version
```

Example:

```sh
mkdir -p out
./bin/icc -d out examples/prog1_geometry.citu
# examples/prog1_geometry.citu: 122 tokens -> out/prog1_geometry.lex; 84 AST nodes -> out/prog1_geometry.ast
```

Pipeline for each input:

1. The source is read, scanned by the **Lexer**, and written by **LexWriter** to `<stem>.lex`.
2. The **Parser** is given only the path of `<stem>.lex`. It rebuilds the tokens with **LexReader**, parses them with the LL(1) table, and **AstWriter** writes `<stem>.ast`.
3. As a self-check, the new `.ast` is read back with **AstReader**.

Stale `<stem>.lex`/`<stem>.ast` files from a previous run are removed first, so a failed run never leaves an older output behind.

Exit status: `0` success, `1` lexical/syntax/format error, `2` usage error, `3` I/O error.
Diagnostics use the specification's codes, for example:

```text
SYNTAX_ERROR in bad_syntax.citu at Line 4, Col 5: Unexpected 'int32' (int32) while parsing T'; expected one of: ...
LEXICAL_ERROR_FORBIDDEN_CONSTRUCT in bad_lexical.citu at Line 1, Col 1: Forbidden construct 'class' is not allowed in Citu
```

`-o app.exe` (multi-file linking) belongs to Phase 8 and is rejected by this build.

## Tests

```sh
make test          # unit suites: Lexer, LexWriter, LexReader, Parser/AST
make integration   # runs bin/icc on the samples and verifies the files it wrote
make check         # both
```

Run from the repository root. The unit suites read `examples/` and write scratch files under `bin/`.

| Suite | Covers |
| :--- | :--- |
| `tests/test_lexer.cpp` | keywords, forbidden words, maximal munch, literals and limits, escapes, comments, coordinates, EOF position, illegal characters, sample programs |
| `tests/test_lex_writer.cpp` | `.lex` serialization, escaping, precondition rejection, I/O failure and reuse |
| `tests/test_lex_reader.cpp` | the full `.lex` validation protocol and corruption cases |
| `tests/test_parser.cpp` | LL(1) table dimensions and every cell count, golden `.ast` files from §5.3, the sample programs, precedence and associativity, all constructs, syntax errors, AstWriter/AstReader rejection cases, instance reuse, line numbers |
| `tests/test_integration.cpp` | re-reads the CLI's real `.lex`/`.ast` output, checks §2 token/node counts, compares byte-for-byte with the spec golden files in `tests/golden/`, and cross-checks against an in-memory run |

Expected sample results (spec §2):

| Program | Tokens (incl. `$`) | AST nodes |
| :--- | ---: | ---: |
| `examples/prog1_geometry.citu` | 122 | 84 |
| `examples/prog2_payroll.citu` | 141 | 104 |
| `examples/prog3_grades.citu` | 144 | 107 |

## Repository layout

```text
include/   headers: token.h, ast.h, lexer.h, lex_writer.h, lex_reader.h,
           parser.h, ast_schema.h, ast_writer.h, ast_reader.h
src/       lexer.cpp, lex_writer.cpp, lex_reader.cpp, parser.cpp,
           ast_schema.cpp (shared §4.2 node schema), ast_writer.cpp,
           ast_reader.cpp, main.cpp (CLI)
tests/     unit suites, test_integration.cpp, fixtures/ (inputs), golden/ (spec §5.2/§5.3 files)
examples/  the three specification sample programs
doc/       Phase 1 specification (v1.4)
bin/       build output (icc); build/ holds objects and test binaries
```
