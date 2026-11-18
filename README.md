# ICC — ITU Compiler Collection

A C++ compiler project for Citu, a custom programming language developed as part of the compiler construction coursework at Information Technology University (ITU).

## Project Structure

- `doc/` — Language specification and project documentation.
- `examples/` — Sample Citu programs.
- `src/` — Compiler implementation.
- `include/` — Header files.
- `tests/` — Test cases.
- `lib/` — Reusable libraries.
- `bin/` — Generated binaries.

## Compiler Pipeline

The compiler is designed as a sequence of independent phases that communicate through intermediate files.

1. Lexical Analysis
2. Syntax Analysis
3. Semantic Analysis
4. Code Generation — Part 1
5. Code Generation — Part 2
6. Hardware-independent Optimization
7. Hardware-specific Optimization
8. Executable Generation and Linking

## Development Status

- Phase 1: Citu Language Specification
- Phase 2: Lexer and Parser — In progress
- Later phases: Planned

## Language

Citu is the custom programming language defined in the Phase 1 specification.

See `doc/Citu_Phase1_Specification.md` for its complete grammar, lexical rules, data structures, and intermediate file formats.
