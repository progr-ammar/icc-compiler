# ITU Compiler Collection (`icc`) — Phase 1 Technical Specification

- **Course:** Compiler Construction
- **Compiler Name:** `icc` (ITU Compiler Collection)
- **Source Language:** Citu (`C2`)
- **Student ID:** `<YOUR_STUDENT_ID>`
- **Git Repository URL:** `<YOUR_GIT_REPO_URL>`
- **Submission Commit Hash:** `<YOUR_COMMIT_HASH>`
- **Document Version:** `v1.4` (Final Audited Phase 1 Specification; supersedes v1.3)

---

## Table of Contents
1. [Project Overview, Status Matrix & Compilation Pipeline](#1-project-overview-status-matrix--compilation-pipeline)
2. [Requirement 1: Three Citu Example Programs & Technical Audit](#2-requirement-1-three-citu-example-programs--technical-audit)
3. [Requirement 2: Complete Citu Language Grammar, Type System & Semantics](#3-requirement-2-complete-citu-language-grammar-type-system--semantics)
4. [Requirement 3: Compiler Data Structures & Complete AST Schema](#4-requirement-3-compiler-data-structures--complete-ast-schema)
5. [Requirement 4: Intermediate File Formats Specification](#5-requirement-4-intermediate-file-formats-specification)
6. [Requirement 5: Git Repository & Directory Structure](#6-requirement-5-git-repository--directory-structure)

---

## 1. Project Overview, Status Matrix & Compilation Pipeline

### 1.1 Architectural Mandate: Strict File-System Decoupling
The **ITU Compiler Collection (`icc`)** is a multi-phase compiler written in C++ for the **Citu (`C2`)** programming language. Every phase operates as an isolated module that communicates exclusively through human-readable intermediate files stored on disk:
1. **Reader (Deserializer):** Opens the predecessor phase's intermediate file(s), validates format headers, record counts, structural invariants, and `@END_FILE` sentinels, and reconstructs in-memory C++ data structures.
2. **Phase Engine:** Executes the phase's compilation pass in memory.
3. **Writer (Serializer):** Serializes the output data structures into a new stage-specific intermediate file on disk and closes the file stream. No phase passes C++ objects, pointers, or containers in RAM to another phase, and no phase overwrites a predecessor's output file in place.

### 1.2 Engineering Status Matrix
The table below distinguishes what is specified in this document, what was checked during the final audit, and what remains unimplemented and untested pending later project milestones. "Audit-checked" means the claim was re-derived or machine-checked from this document's own text (grammar, sample programs, example files); it does **not** mean the compiler has been implemented or tested.

| Component / Deliverable | Specified? | Audit Status | Implemented in C++? | Tested? | Scheduled Milestone |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Citu Lexical Rules (33 Terminals)** | Yes | Reviewed by inspection against the grammar and samples | Not in this project's code base (earlier assignment work was a different language subset; unverifiable here) | Not Yet Tested | Phase 1 (Nov 15) / Phase 2 (Nov 18) |
| **77-Rule CFG & 39 × 33 LL(1) Parsing Table** | Yes | FIRST/FOLLOW sets and table recomputed from the 77 productions: 186 populated cells, 0 conflicts | Not in this project's code base (earlier assignment parser covered a smaller rule subset; unverifiable here) | Not Yet Tested | Phase 1 (Nov 15) / Phase 2 (Nov 18) |
| **Three Sample Programs (`prog1`–`prog3`)** | Yes | Each program accepted by a table-driven LL(1) parse of the 77-rule grammar; token and AST-node counts recomputed (Section 2); type rules checked by hand | Yes (`.citu` source files) | Manual trace only | Phase 1 (Nov 15) |
| **C++ Data Structures & File Schemas** | Yes | Schema cross-check of every serialized field against the C++ structures; example files record-count checked | Not Yet Implemented | Not Yet Tested | Phase 1 (Nov 15), coded in Phases 2–8 |
| **Phase 2: Citu Lexer & LL(1) Parser + AST** | Yes | Design audited only | Not Yet Implemented | Not Yet Tested | Phase 2 (Due Nov 18) |
| **Phase 3: Two-Pass Semantic Analyzer (`.sym`)** | Yes | Design audited only | Not Yet Implemented | Not Yet Tested | Phase 3 (Due Nov 23) |
| **Phase 4: CodeGen 1 (1-to-1 Translation)** | Yes | Design audited only; assembly examples hand-checked, not assembled | Not Yet Implemented | Not Yet Tested | Phase 4 (Due Nov 29) |
| **Phase 5: CodeGen 2 (Refined Addressing)** | Yes | Design audited only | Not Yet Implemented | Not Yet Tested | Phase 5 (Due Dec 2) |
| **Phase 6: Optimizer 1 (Hardware-Independent)** | Yes (placement provisional, Section 1.4) | Design audited only | Not Yet Implemented | Not Yet Tested | Phase 6 (Due Dec 9) |
| **Phase 7: Optimizer 2 (x86 Hardware-Specific)** | Yes | Design audited only | Not Yet Implemented | Not Yet Tested | Phase 7 (Due Dec 9) |
| **Phase 8: Multi-File Linker & `.exe` Builder** | Yes | Design audited only; the NASM and MinGW-w64 command sequence has **not** been executed | Not Yet Implemented | Not Yet Tested | Phase 8 (Due Dec 13/20; exact date unconfirmed) |

### 1.3 Requirements, Design Decisions, and Instructor-Dependent Decisions

The three groups below are kept strictly separate. The professor-mandated list is reproduced as supplied by the author; the course documents it was derived from are **not** part of the material provided for this audit, so those items could not be independently verified and must be checked against the original assignment text before submission.

#### A. Professor-Mandated Requirements (as stated by the author; not independently verifiable here)
1. Roman-Urdu keywords: `agar` (`if`), `magar` (`else`), `jabtak` (`while`), `shro` (`{`), `khtm` (`}`), and `medalo` (assignment).
2. Mandatory `shro ... khtm` block delimiters on every scope (`struct`, functions, `agar`, `magar`, `jabtak`).
3. Compacted variable declarations at the top of a scope (`DLIST`) before executable statements (`SLIST`).
4. Strict separation of variable declaration and assignment (`int32 x medalo 5` is forbidden).
5. No `=` assignment operator; single `=` is used strictly for equality comparison.
6. Restricted primitive datatypes (`int16`, `int32`, `uint16`, `uint32`, `char`), mandatory `struct` support, functions, and `main()`.
7. Explicit prohibition of `class` and `do-while`.
8. File-system decoupling with custom human-readable intermediate file formats.
9. Two-part Code Generation (Part 1: direct 1-to-1 translation formula to assembly; Part 2: revised assembly), two-part Optimization (Part 1: hardware-independent; Part 2: hardware-dependent), and multi-file CLI linking into an executable (`icc file1.citu file2.citu -o app.exe`).

#### B. ICC Engineering Design Decisions (author's choices, fixed by this specification)
1. Functions begin with `fn`, void return types use `khali`, and return statements use `wapis` (with mandatory `wapis khali` in `khali` functions so semicolon-free returns have zero LL(1) lookahead ambiguity).
2. Semicolon-free syntax; whitespace and newlines act purely as token separators.
3. **4-Byte Stack Slot Convention (an ICC convention, not an x86 hardware requirement):** `icc` allocates a uniform 4-byte (`dword`) aligned stack slot for every local variable, parameter, struct field, and compiler temporary to keep `esp` 4-byte aligned and simplify offset arithmetic. Size-exact loads and stores (`movzx`, `movsx`, `mov byte`, `mov word`) and 16-bit sub-registers (`ax`, `cx`, `dx`) preserve exact 8-bit (`char`) and 16-bit (`int16`, `uint16`) semantics (Section 5.6-A).
4. Target: 32-bit x86, NASM syntax, Win32 COFF objects, PE32 executable (Section 5.7).

#### C. Genuinely Instructor-Dependent Decisions (unresolved; do not treat as confirmed)
| # | Decision | Status in this document |
| :--- | :--- | :--- |
| C1 | **Placement of Optimizer 1** (hardware-independent): before CodeGen 1 on a target-neutral IR (Option A) or after CodeGen 2 on assembly (Option B) | Both options fully specified; see Section 1.4 |
| C2 | Whether the course permits an external assembler and linker (`nasm` + MinGW-w64) for the final `.exe`, or expects `icc` itself to emit the PE32 image | Specified with external tools; **unconfirmed** |
| C3 | Whether 32-bit x86 / NASM / Windows is the required target (the CLI example `app.exe` implies Windows; the CPU and assembler are ICC choices) | Specified as 32-bit x86 NASM; **unconfirmed** |
| C4 | Exact due date of Phase 8 (`Dec 13` or `Dec 20`) | Left as `Dec 13/20` |

### 1.4 Provisional Compilation Pipeline & Unresolved Optimizer Placement

The classroom milestone schedule lists **CodeGen 1** (Nov 29) and **CodeGen 2** (Dec 2) chronologically before **Optimizer 1 — Hardware-Independent** (Dec 9) and **Optimizer 2 — Hardware-Specific** (Dec 9). This creates an architectural question that is **provisional pending instructor confirmation (decision C1)**.

- **Pipeline Option A: Target-Neutral IR Before CodeGen**
  - *Flow:* `.ast` + `.sym` $\rightarrow$ **IR Builder** (`.ir`) $\rightarrow$ **Optimizer 1** (`.opt1.ir`) $\rightarrow$ **CodeGen 1** (`.cg1.asm`) $\rightarrow$ **CodeGen 2** (`.cg2.asm`) $\rightarrow$ **Optimizer 2** (`.opt2.asm`) $\rightarrow$ **Linker** (`.linked.asm` $\rightarrow$ `.obj` $\rightarrow$ `.exe`).
  - *Inputs:* the IR Builder reads `.ast` and `.sym`; CodeGen 1 reads `.ir` (or `.opt1.ir` once Optimizer 1 exists) and `.sym`.
  - *Advantages:* Optimizer 1 works on quadruples that contain no x86 register or stack offset, so it is genuinely hardware-independent. Constant folding, constant propagation, algebraic simplification, and dead-store elimination are straightforward on a three-address form.
  - *Costs:* one additional file format and one additional component (the IR Builder, which has no milestone of its own in the schedule); CodeGen 1 becomes a translation of IR rather than of the AST.
- **Pipeline Option B: Strict Chronological Chaining on Assembly**
  - *Flow:* `.ast` + `.sym` $\rightarrow$ **CodeGen 1** (`.cg1.asm`) $\rightarrow$ **CodeGen 2** (`.cg2.asm`) $\rightarrow$ **Optimizer 1** (`.opt1.asm`) $\rightarrow$ **Optimizer 2** (`.opt2.asm`) $\rightarrow$ **Linker**.
  - *Inputs:* CodeGen 1 reads `.ast` and `.sym` directly (a tree walk may use an x86 evaluation stack with `push`/`pop`, because tree evaluation is naturally LIFO and arguments are naturally pushed right-to-left).
  - *Advantages:* matches the milestone dates one-to-one and needs no IR format.
  - *Costs:* `.cg2.asm` already contains x86 instructions (`mov`, `eax`, `[ebp - 4]`), so Optimizer 1 is **not truly hardware-independent**. Splitting an instruction into pipe-delimited columns does not make it target-neutral. Dataflow optimizations are still possible because Citu has no pointers or globals (every stack slot is named in `; @MEM_SYM`), but they are performed on x86 text.
- **Common to both options:** the file formats for `.ir`/`.opt1.ir` (Section 5.5) and for all assembly stages including `.opt1.asm` (Section 5.6) are specified in full, so the pipeline can be locked to either option on instructor confirmation. The worked examples in Sections 5.5 and 5.6 follow Option A.

---

## 2. Requirement 1: Three Citu Example Programs & Technical Audit

The counts quoted under each program were recomputed during the final audit by lexing each file with the Section 3.1 rules, accepting it with a table-driven LL(1) parse using the Section 3.3 table, and building the AST of Section 4.2 (a throwaway verification script; it is not part of the repository). Token counts include the final `$` token.

### 2.1 Example Program 1: `examples/prog1_geometry.citu`

```text
struct Box shro
    int32 width
    int32 height
khtm

fn int32 calcArea ( int32 w , int32 h ) shro
    int32 area
    area medalo w * h
    wapis area
khtm

fn int32 scaleBox ( int32 currentArea , int32 limit ) shro
    int32 result
    result medalo currentArea
    jabtak ( result < limit ) shro
        result medalo result + 10
    khtm
    wapis result
khtm

fn int32 main ( ) shro
    struct Box myBox
    int32 totalArea
    int32 finalScore

    myBox.width medalo 5
    myBox.height medalo 8
    totalArea medalo calcArea ( myBox.width , myBox.height )

    agar ( totalArea > 30 ) shro
        finalScore medalo scaleBox ( totalArea , 100 )
    khtm magar shro
        finalScore medalo totalArea
    khtm

    wapis finalScore
khtm
```

#### Technical Audit of Program 1
- **Features Demonstrated:** Top-level `struct` definition (`Box`), 3 `fn` definitions (`calcArea`, `scaleBox`, `main`), struct instance declaration (`struct Box myBox`), compacted top declarations (`DLIST` before `SLIST`), struct field assignment (`myBox.width medalo 5`) and expression access (`calcArea ( myBox.width , myBox.height )`), arithmetic (`*`, `+`), relational operators (`<`, `>`), `jabtak` loop, `agar ... magar` conditional, and `wapis`.
- **Verified Counts:** 37 lines, **122 tokens** (including `$`), **84 AST nodes**. Accepted by the LL(1) parse. All declarations appear at the top of their respective scopes, all operand types match `int32`, and every function ends its top-level `SLIST` with `wapis`.

---

### 2.2 Example Program 2: `examples/prog2_payroll.citu`

```text
struct Employee shro
    uint16 id
    uint32 salary
    int16 bonusTier
khtm

fn uint32 computeBonus ( uint32 base , int16 tier ) shro
    uint32 bonus
    agar ( tier = 1 ) shro
        bonus medalo base / 2
    khtm magar shro
        bonus medalo base / 4
    khtm
    wapis bonus
khtm

fn int32 main ( ) shro
    struct Employee emp
    uint32 extraPay
    int16 months

    emp.id medalo 101
    emp.salary medalo 4000
    emp.bonusTier medalo 1
    months medalo 3

    agar ( emp.id = 101 ) shro
        jabtak ( months > 0 ) shro
            agar ( emp.salary < 10000 ) shro
                uint32 newSalary
                extraPay medalo computeBonus ( emp.salary , emp.bonusTier )
                newSalary medalo emp.salary + extraPay
                emp.salary medalo newSalary
            khtm
            months medalo months - 1
        khtm
    khtm

    wapis 0
khtm
```

#### Technical Audit of Program 2
- **Features Demonstrated:** Restricted 16-bit and unsigned integer types (`uint16`, `uint32`, `int16`), single `=` equality comparison (`tier = 1`, `emp.id = 101`), 3-level nested blocks (`agar` $\rightarrow$ `jabtak` $\rightarrow$ standalone `agar` without `magar`), nested block-top declaration (`uint32 newSalary` on line 30, the first line inside the block opened on line 29, before the first `medalo`), and unsigned division by powers of two (`base / 2`, `base / 4`).
- **Verified Counts:** 40 lines, **141 tokens**, **104 AST nodes**. Accepted by the LL(1) parse.
- **Type Audit:** Zero mixing of signed/unsigned or 16-bit/32-bit types. The integer literals `101`, `4000`, `1`, `3`, `0`, `10000`, `2`, `4` each adapt to their partner type (`uint16`, `uint32`, `int16`, `int32`) and lie within its range. All local names in each function are unique (Section 3.4 rule 5).

---

### 2.3 Example Program 3: `examples/prog3_grades.citu`

```text
struct Student shro
    int32 rollNo
    int32 marks
    char grade
khtm

fn char evaluateGrade ( int32 score ) shro
    char resultGrade
    agar ( score >= 80 ) shro
        resultGrade medalo 'A'
    khtm magar shro
        resultGrade medalo 'B'
    khtm
    wapis resultGrade
khtm

fn khali auditRecord ( int32 currentMarks ) shro
    int32 margin
    agar ( currentMarks != 100 ) shro
        margin medalo 100 - currentMarks
    khtm
    wapis khali
khtm

fn int32 main ( ) shro
    struct Student st
    int32 boosted
    int32 exitCode

    st.rollNo medalo 143
    st.marks medalo 65
    boosted medalo st.marks

    jabtak ( boosted <= 75 ) shro
        boosted medalo boosted + 5
    khtm

    st.grade medalo evaluateGrade ( boosted )
    auditRecord ( boosted )

    agar ( st.grade = 'A' ) shro
        exitCode medalo st.rollNo
    khtm magar shro
        exitCode medalo 0
    khtm

    wapis exitCode
khtm
```

#### Technical Audit of Program 3
- **Features Demonstrated:** Unsigned 8-bit `char` datatype and character literals (`'A'`, `'B'`), `char` equality comparison (`st.grade = 'A'`), void function (`fn khali auditRecord`), explicit void return (`wapis khali`), standalone call statement (`auditRecord ( boosted )`), relational operators (`>=`, `<=`, `!=`, `=`), and an intentional dead store (`margin` in `auditRecord`) that is semantically valid and exercises Phase 6 Dead-Store Elimination.
- **Verified Counts:** 48 lines, **144 tokens**, **107 AST nodes**. Accepted by the LL(1) parse.

---

## 3. Requirement 2: Complete Citu Language Grammar, Type System & Semantics

### 3.1 Lexical Specification
1. **Encoding:** UTF-8 without BOM. **Columns** are 1-based byte offsets within a line (a tab counts as one column).
2. **Reserved Keywords (16):** `struct`, `fn`, `main`, `khali`, `wapis`, `shro`, `khtm`, `jabtak`, `agar`, `magar`, `medalo`, `int16`, `int32`, `uint16`, `uint32`, `char`. Because `main` is a keyword and not an `ID`, it can never be called or used as a variable name.
3. **Forbidden Keywords (2):** `class` and `do`. The Lexer scans full words (`[a-zA-Z_][a-zA-Z0-9_]*`) using Maximal Munch before checking whether the complete word equals `"class"` or `"do"`. Exact matches halt with `LEXICAL_ERROR_FORBIDDEN_CONSTRUCT`; longer identifiers such as `classRoom` or `doWork` tokenize as `ID`.
4. **Identifiers (Grammar Terminal `ID`, Category `IDENTIFIER`):** Pattern `[a-zA-Z_][a-zA-Z0-9_]*` (case-sensitive), excluding the 16 reserved and 2 forbidden keywords.
5. **Literals (Grammar Terminal `LT`, Categories `INT_LITERAL` and `CHAR_LITERAL`):**
   - **Unified Terminal `LT`:** Integer constants (`10`) and character constants (`'A'`) share Factor production Rule 69 (`F -> LT`). The grammar, the LL(1) table, and the `.lex` `TERMINAL` column use `LT`; the `.lex` `CATEGORY` column distinguishes `INT_LITERAL` from `CHAR_LITERAL` for Phase 3.
   - **Integer Literals:** Contiguous decimal digits `[0-9]+`. If immediately followed by `[a-zA-Z_.]`, the Lexer halts with `LEXICAL_ERROR_MALFORMED_NUMBER`. A value above `4,294,967,295` halts with `LEXICAL_ERROR_INTEGER_TOO_LARGE`. `-` is always tokenized separately as `'-'`.
   - **Character Literals:** Single-quoted `'c'`, where `c` is one printable ASCII character in range `32..126` excluding `'` and `\`, or one of five exact escape sequences: `'\n'` (10), `'\t'` (9), `'\0'` (0), `'\\'` (92), `'\''` (39). An empty `''`, a multi-character `'ab'`, an unclosed quote, or an invalid escape halts with `LEXICAL_ERROR_MALFORMED_CHAR`.
6. **Operators & Punctuation:**
   - **Arithmetic (4):** `+`, `-`, `*`, `/`
   - **Relational & Equality (6):** `<`, `>`, `=`, `!=`, `<=`, `>=` (disambiguated by one character of lookahead; `==` and a standalone `!` are rejected with `LEXICAL_ERROR_ILLEGAL_CHARACTER`).
   - **Punctuation (4):** `(`, `)`, `,`, `.`
   - Any other character outside a comment or character literal halts with `LEXICAL_ERROR_ILLEGAL_CHARACTER`.
7. **Whitespace, Newlines & Comments:** Spaces, `\t`, `\r`, and `\n` are insignificant separators; `\n` increments the line counter. Multiple statements on one line are valid. Single-line comments begin with `#` (outside character literals) and run to `\n` or EOF; multi-line comments do not exist.
8. **EOF Token Position:** The `$` token is positioned immediately after the final character of the file: if the file does not end with `\n`, it lies on the last line at column (last line length + 1); if the file ends with `\n`, it lies at column `1` of the following line.

---

### 3.2 Complete Context-Free Grammar (77 Productions, 39 Non-Terminals, 33 Terminals)

- **Total Productions:** **77** (`1` to `77`)
- **Total Non-Terminals:** **39** (`PROG` through `ARG_TAIL`)
- **Total Terminals:** **33** (32 language tokens + `$` EOF marker): 16 keywords, `ID`, `LT`, 4 arithmetic operators, 6 relational operators, 4 punctuation marks, plus `$`
- **LL(1) Parsing Table Size:** **39 rows × 33 columns = 1,287 cells** (**186 populated cells**, **1,101 empty error cells**, **0 conflicts**)

```text
# Group 1: Program Root & Struct Definitions (1–8)
1.  PROG        -> STRUCT_LIST FUNC_LIST
2.  STRUCT_LIST -> STRUCT_DEF STRUCT_LIST
3.  STRUCT_LIST -> ε
4.  STRUCT_DEF  -> 'struct' ID 'shro' FIELD_LIST 'khtm'
5.  FIELD_LIST  -> FIELD FIELD_TAIL
6.  FIELD_TAIL  -> FIELD FIELD_TAIL
7.  FIELD_TAIL  -> ε
8.  FIELD       -> TYPE ID

# Group 2: Function Definitions & Headers (9–16)
9.  FUNC_LIST   -> FUNC_DEF FUNC_TAIL
10. FUNC_TAIL   -> FUNC_DEF FUNC_TAIL
11. FUNC_TAIL   -> ε
12. FUNC_DEF    -> 'fn' RET_TYPE FUNC_NAME '(' PARAM_LIST ')' CST
13. RET_TYPE    -> TYPE
14. RET_TYPE    -> 'khali'
15. FUNC_NAME   -> ID
16. FUNC_NAME   -> 'main'

# Group 3: Function Parameters (17–21)
17. PARAM_LIST  -> PARAM PARAM_TAIL
18. PARAM_LIST  -> ε
19. PARAM_TAIL  -> ',' PARAM PARAM_TAIL
20. PARAM_TAIL  -> ε
21. PARAM       -> TYPE ID

# Group 4: Scopes, Compacted Top Declarations & Types (22–31)
22. CST         -> 'shro' DLIST SLIST 'khtm'
23. DLIST       -> DS DLIST
24. DLIST       -> ε
25. DS          -> TYPE ID
26. DS          -> 'struct' ID ID
27. TYPE        -> 'int16'
28. TYPE        -> 'int32'
29. TYPE        -> 'uint16'
30. TYPE        -> 'uint32'
31. TYPE        -> 'char'

# Group 5: Executable Statements & Control Flow (32–50)
32. SLIST       -> ST SLIST
33. SLIST       -> ε
34. ST          -> WS
35. ST          -> IF_STMT
36. ST          -> AS
37. ST          -> RET_STMT
38. ST          -> CST
39. WS          -> 'jabtak' '(' COND ')' CST
40. IF_STMT     -> 'agar' '(' COND ')' CST ELSE_PART
41. ELSE_PART   -> 'magar' CST
42. ELSE_PART   -> ε
43. RET_STMT    -> 'wapis' RET_VAL
44. RET_VAL     -> EX
45. RET_VAL     -> 'khali'
46. AS          -> ID AS_TAIL
47. AS_TAIL     -> VAR_TAIL 'medalo' EX
48. AS_TAIL     -> '(' ARG_LIST ')'
49. VAR_TAIL    -> '.' ID
50. VAR_TAIL    -> ε

# Group 6: Conditions & Relational Operators (51–59)
51. COND        -> EX COND_TAIL
52. COND_TAIL   -> REL_OP EX
53. COND_TAIL   -> ε
54. REL_OP      -> '<'
55. REL_OP      -> '>'
56. REL_OP      -> '='
57. REL_OP      -> '!='
58. REL_OP      -> '<='
59. REL_OP      -> '>='

# Group 7: Arithmetic Expressions, Terms, Factors & Function Calls (60–77)
60. EX          -> T EX'
61. EX'         -> '+' T EX'
62. EX'         -> '-' T EX'
63. EX'         -> ε
64. T           -> F T'
65. T'          -> '*' F T'
66. T'          -> '/' F T'
67. T'          -> ε
68. F           -> ID F_TAIL
69. F           -> LT
70. F           -> '(' EX ')'
71. F_TAIL      -> '.' ID
72. F_TAIL      -> '(' ARG_LIST ')'
73. F_TAIL      -> ε
74. ARG_LIST    -> EX ARG_TAIL
75. ARG_LIST    -> ε
76. ARG_TAIL    -> ',' EX ARG_TAIL
77. ARG_TAIL    -> ε
```

#### Precedence and Associativity
The expression grammar encodes two precedence levels. `*` and `/` (rules 64–67, non-terminal `T`) bind tighter than `+` and `-` (rules 60–63, non-terminal `EX`); parentheses (rule 70) override both. Both levels are **left-associative**: the parser folds the right-recursive `EX'` and `T'` chains into left-leaning `BIN_EXPR` trees (`a - b - c` becomes `(a - b) - c`; `a / b * c` becomes `(a / b) * c`). Relational operators are not part of `EX`: at most one relational operator appears in a condition (rule 52), so chained comparisons cannot be written. There is no unary operator.

---

### 3.3 FIRST/FOLLOW Sets & 39 × 33 LL(1) Parsing Table

The FIRST sets, FOLLOW sets, nullability, and every table entry below were **recomputed from the 77 productions** during the final audit and match this table exactly: 186 populated cells, 0 cells with more than one rule, all 77 rules used.

Let:
- `TYPE_SET` = `{ int16, int32, uint16, uint32, char }` (5 terminals)
- `ST_SET` = `{ jabtak, agar, ID, wapis, shro }` (5 terminals)
- `REL_SET` = `{ <, >, =, !=, <=, >= }` (6 terminals)
- `EX_FOLLOW` = `{ <, >, =, !=, <=, >=, ), ,, jabtak, agar, ID, wapis, shro, khtm }` (14 terminals)

In the last column, `int16`..`char` abbreviates the five `TYPE_SET` terminals and `<`..`>=` abbreviates the six `REL_SET` terminals (all take the same rule number).

| Row # | Non-Terminal | Nullable? | FIRST Set | FOLLOW Set | Populated Cells | Non-Empty LL(1) Table Entries (`Lookahead : Rule #`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `PROG` | No | `{ struct, fn }` | `{ $ }` | 2 | `struct`: **1**, `fn`: **1** |
| 2 | `STRUCT_LIST` | Yes | `{ struct, ε }` | `{ fn }` | 2 | `struct`: **2**, `fn`: **3** |
| 3 | `STRUCT_DEF` | No | `{ struct }` | `{ struct, fn }` | 1 | `struct`: **4** |
| 4 | `FIELD_LIST` | No | `TYPE_SET` | `{ khtm }` | 5 | `int16`..`char`: **5** |
| 5 | `FIELD_TAIL` | Yes | `TYPE_SET U { ε }` | `{ khtm }` | 6 | `int16`..`char`: **6**, `khtm`: **7** |
| 6 | `FIELD` | No | `TYPE_SET` | `TYPE_SET U { khtm }` | 5 | `int16`..`char`: **8** |
| 7 | `FUNC_LIST` | No | `{ fn }` | `{ $ }` | 1 | `fn`: **9** |
| 8 | `FUNC_TAIL` | Yes | `{ fn, ε }` | `{ $ }` | 2 | `fn`: **10**, `$`: **11** |
| 9 | `FUNC_DEF` | No | `{ fn }` | `{ fn, $ }` | 1 | `fn`: **12** |
| 10 | `RET_TYPE` | No | `TYPE_SET U { khali }` | `{ ID, main }` | 6 | `int16`..`char`: **13**, `khali`: **14** |
| 11 | `FUNC_NAME` | No | `{ ID, main }` | `{ ( }` | 2 | `ID`: **15**, `main`: **16** |
| 12 | `PARAM_LIST` | Yes | `TYPE_SET U { ε }` | `{ ) }` | 6 | `int16`..`char`: **17**, `)`: **18** |
| 13 | `PARAM_TAIL` | Yes | `{ ,, ε }` | `{ ) }` | 2 | `,`: **19**, `)`: **20** |
| 14 | `PARAM` | No | `TYPE_SET` | `{ ,, ) }` | 5 | `int16`..`char`: **21** |
| 15 | `CST` | No | `{ shro }` | `ST_SET U { magar, khtm, fn, $ }` | 1 | `shro`: **22** |
| 16 | `DLIST` | Yes | `TYPE_SET U { struct, ε }` | `ST_SET U { khtm }` | 12 | `int16`..`char`: **23**, `struct`: **23**, `jabtak`: **24**, `agar`: **24**, `ID`: **24**, `wapis`: **24**, `shro`: **24**, `khtm`: **24** |
| 17 | `DS` | No | `TYPE_SET U { struct }` | `TYPE_SET U { struct } U ST_SET U { khtm }` | 6 | `int16`..`char`: **25**, `struct`: **26** |
| 18 | `TYPE` | No | `TYPE_SET` | `{ ID, main }` | 5 | `int16`: **27**, `int32`: **28**, `uint16`: **29**, `uint32`: **30**, `char`: **31** |
| 19 | `SLIST` | Yes | `ST_SET U { ε }` | `{ khtm }` | 6 | `jabtak`: **32**, `agar`: **32**, `ID`: **32**, `wapis`: **32**, `shro`: **32**, `khtm`: **33** |
| 20 | `ST` | No | `ST_SET` | `ST_SET U { khtm }` | 5 | `jabtak`: **34**, `agar`: **35**, `ID`: **36**, `wapis`: **37**, `shro`: **38** |
| 21 | `WS` | No | `{ jabtak }` | `ST_SET U { khtm }` | 1 | `jabtak`: **39** |
| 22 | `IF_STMT` | No | `{ agar }` | `ST_SET U { khtm }` | 1 | `agar`: **40** |
| 23 | `ELSE_PART` | Yes | `{ magar, ε }` | `ST_SET U { khtm }` | 7 | `magar`: **41**, `jabtak`: **42**, `agar`: **42**, `ID`: **42**, `wapis`: **42**, `shro`: **42**, `khtm`: **42** |
| 24 | `RET_STMT` | No | `{ wapis }` | `ST_SET U { khtm }` | 1 | `wapis`: **43** |
| 25 | `RET_VAL` | No | `{ ID, LT, (, khali }` | `ST_SET U { khtm }` | 4 | `ID`: **44**, `LT`: **44**, `(`: **44**, `khali`: **45** |
| 26 | `AS` | No | `{ ID }` | `ST_SET U { khtm }` | 1 | `ID`: **46** |
| 27 | `AS_TAIL` | No | `{ ., medalo, ( }` | `ST_SET U { khtm }` | 3 | `.`: **47**, `medalo`: **47**, `(`: **48** |
| 28 | `VAR_TAIL` | Yes | `{ ., ε }` | `{ medalo }` | 2 | `.`: **49**, `medalo`: **50** |
| 29 | `COND` | No | `{ ID, LT, ( }` | `{ ) }` | 3 | `ID`: **51**, `LT`: **51**, `(`: **51** |
| 30 | `COND_TAIL` | Yes | `REL_SET U { ε }` | `{ ) }` | 7 | `<`..`>=`: **52**, `)`: **53** |
| 31 | `REL_OP` | No | `REL_SET` | `{ ID, LT, ( }` | 6 | `<`: **54**, `>`: **55**, `=`: **56**, `!=`: **57**, `<=`: **58**, `>=`: **59** |
| 32 | `EX` | No | `{ ID, LT, ( }` | `EX_FOLLOW` | 3 | `ID`: **60**, `LT`: **60**, `(`: **60** |
| 33 | `EX'` | Yes | `{ +, -, ε }` | `EX_FOLLOW` | 16 | `+`: **61**, `-`: **62**, `<`..`>=`: **63**, `)`: **63**, `,`: **63**, `jabtak`: **63**, `agar`: **63**, `ID`: **63**, `wapis`: **63**, `shro`: **63**, `khtm`: **63** |
| 34 | `T` | No | `{ ID, LT, ( }` | `{ +, - } U EX_FOLLOW` | 3 | `ID`: **64**, `LT`: **64**, `(`: **64** |
| 35 | `T'` | Yes | `{ *, /, ε }` | `{ +, - } U EX_FOLLOW` | 18 | `*`: **65**, `/`: **66**, `+`: **67**, `-`: **67**, `<`..`>=`: **67**, `)`: **67**, `,`: **67**, `jabtak`: **67**, `agar`: **67**, `ID`: **67**, `wapis`: **67**, `shro`: **67**, `khtm`: **67** |
| 36 | `F` | No | `{ ID, LT, ( }` | `{ *, /, +, - } U EX_FOLLOW` | 3 | `ID`: **68**, `LT`: **69**, `(`: **70** |
| 37 | `F_TAIL` | Yes | `{ ., (, ε }` | `{ *, /, +, - } U EX_FOLLOW` | 20 | `.`: **71**, `(`: **72**, `*`: **73**, `/`: **73**, `+`: **73**, `-`: **73**, `<`..`>=`: **73**, `)`: **73**, `,`: **73**, `jabtak`: **73**, `agar`: **73**, `ID`: **73**, `wapis`: **73**, `shro`: **73**, `khtm`: **73** |
| 38 | `ARG_LIST` | Yes | `{ ID, LT, (, ε }` | `{ ) }` | 4 | `ID`: **74**, `LT`: **74**, `(`: **74**, `)`: **75** |
| 39 | `ARG_TAIL` | Yes | `{ ,, ε }` | `{ ) }` | 2 | `,`: **76**, `)`: **77** |
| **Total** | **39 Rows** | **15 Nullable** | — | — | **186 Cells** | **0 Conflicts across all 1,287 cells** |

---

### 3.4 Complete Type System, Constant Evaluation & Semantic Rules

1. **Primitive Datatypes & Ranges:**
   - `int16`: Signed 16-bit two's-complement integer ($-32,768 \dots 32,767$).
   - `int32`: Signed 32-bit two's-complement integer ($-2,147,483,648 \dots 2,147,483,647$).
   - `uint16`: Unsigned 16-bit integer ($0 \dots 65,535$).
   - `uint32`: Unsigned 32-bit integer ($0 \dots 4,294,967,295$).
   - `char`: Unsigned 8-bit integer ($0 \dots 255$ storage; $0 \dots 127$ source ASCII character literals). Arithmetic (`+`, `-`, `*`, `/`) on `char` is forbidden; relational/equality comparisons (`<`, `>`, `=`, `!=`, `<=`, `>=`) between two `char` values are supported. An integer literal never adapts to `char`; only `CHAR_LITERAL` values and `char` variables have type `char`.
2. **Bottom-Up Constant Expression Evaluation & Polymorphic `INT_LITERAL` Adaptation:**
   - **Strict Variable Type Equality:** Variables, parameters, struct fields, and function return values have fixed concrete types. Implicit conversions between distinct concrete types (`int16`, `int32`, `uint16`, `uint32`, `char`, `struct S`) are forbidden (`SEMANTIC_ERROR_TYPE_MISMATCH`). Function call arguments must match parameter types exactly and the argument count must equal the parameter count.
   - **Evaluating `INT_LITERAL` Leaves & Constant Expressions:**
     1. Every `INT_LIT` AST leaf (created from a token with `TERMINAL == "LT"` and `CATEGORY == "INT_LITERAL"`) begins with internal pseudo-type `INT_LITERAL` and its value (`0 .. 4,294,967,295`).
     2. If both operands of a binary arithmetic operator (`+`, `-`, `*`, `/`) have pseudo-type `INT_LITERAL` (such as `0 - 32768` or `0 - 2147483648`), Phase 3 evaluates the sub-expression bottom-up in 64-bit signed arithmetic (division truncates toward zero) and the result is again an `INT_LITERAL` with that value. If any intermediate result leaves the 64-bit signed range, Phase 3 halts with `SEMANTIC_ERROR_CONSTANT_OVERFLOW`; division by a constant zero halts with `SEMANTIC_ERROR_DIVISION_BY_ZERO`.
     3. When an `INT_LITERAL` (a single leaf or a folded sub-expression) meets a concrete integer type $T \in \{\text{int16}, \text{int32}, \text{uint16}, \text{uint32}\}$ across a binary operator, a relational operator, a `medalo` assignment, a function argument, or a `wapis` statement, it adopts type $T$, and Phase 3 verifies that its value lies in $[\text{MIN}_T, \text{MAX}_T]$, otherwise `SEMANTIC_ERROR_LITERAL_OVERFLOW`.
     4. **Defaulting:** If an `INT_LITERAL` has no concrete partner (both sides of a relational operator are `INT_LITERAL`, as in `agar ( 1 = 1 )`, or a standalone condition such as `jabtak ( 1 )`), it defaults to `int32` and must lie in `int32` range.
     5. **Minimum Signed Integer Handling:** `0 - 32768` in an `int16` context and `0 - 2147483648` in an `int32` context succeed, because the expression folds to `-32768` (resp. `-2147483648`) before range checking. `var - 32768` with a non-constant `int16` variable `var` is rejected with `SEMANTIC_ERROR_LITERAL_OVERFLOW`, because the standalone leaf `32768` adapts directly to `int16` and exceeds `32767`. A constant such as `(0 - 32768) / (0 - 1)` folds to `+32768` and is rejected by the same range check.
3. **16-Bit & 32-Bit Runtime Overflow, Division & Constant-Folding Consistency:**
   - **Runtime Wrap-Around & Sub-Register Execution:** At runtime on 32-bit x86, `int32` and `uint32` addition, subtraction, and multiplication wrap modulo $2^{32}$ in 32-bit registers (`eax`, `ecx`, `edx`). `int16` and `uint16` arithmetic executes in 16-bit sub-registers (`ax`, `cx`, `dx`) so overflow wraps at 16-bit boundaries, immediately followed by `cwde` (sign-extending `ax` $\rightarrow$ `eax` for `int16`) or `movzx eax, ax` (zero-extending for `uint16`).
   - **Signed vs. Unsigned Division Preparation on x86:**
     - `int32`: `cdq` followed by `idiv ecx`.
     - `int16`: `cwd` followed by `idiv cx`, then `cwde`.
     - `uint32`: `xor edx, edx` followed by `div ecx`.
     - `uint16`: `xor dx, dx` followed by `div cx`, then `movzx eax, ax`.
     - A runtime divisor of zero, or `MIN / -1` for the signed types, raises a hardware `#DE` fault.
   - **Optimizer 1 Constant-Folding Consistency:** Phase 3 folds only pure `INT_LITERAL` expressions (and rejects overflow). Phase 6 (`Optimizer 1`) may additionally fold `ADD`, `SUB`, `MUL` on typed operands after constant propagation; it truncates the result to the exact width and signedness of `DATA_TYPE` so folding equals unoptimized runtime execution. It **never** folds a `DIV` whose divisor is `0` or whose operands are `MIN / -1`, and it **never deletes a `DIV` or a `CALL`/`CALL_VOID`**, even when the result is unused, so a reachable `#DE` fault or callee behaviour is preserved.
4. **Condition Semantics (`COND`):**
   - `COND -> EX REL_OP EX`: Both operands must resolve to the same primitive type (`int16`, `int32`, `uint16`, `uint32`, or `char`), yielding pseudo-type `BOOL_COND`.
   - `COND -> EX`: `EX` must resolve to an integer type (`int16`, `int32`, `uint16`, `uint32`) and tests `EX != 0`. A `char` or struct value is a semantic error (`SEMANTIC_ERROR_BAD_CONDITION_TYPE`).
5. **Entry Point (`main`), Return Guarantee, Names, Scopes & Uniqueness:**
   - **Exact `main` Signature:** `fn int32 main ( )`. Any other return type or any parameter halts with `SEMANTIC_ERROR_INVALID_MAIN_SIGNATURE`. At most one `main` per source file (`SEMANTIC_ERROR_DUPLICATE_MAIN`); exactly one `main` across the linked program (Phase 8: `LINKER_ERROR_MULTIPLE_MAIN`, `LINKER_ERROR_MISSING_MAIN`).
   - **Guaranteed Return Rule:** Every function (`TYPE` or `khali`) must end its top-level `SLIST` with a `wapis` statement matching its `RET_TYPE` (`wapis EX` or `wapis khali`), otherwise `SEMANTIC_ERROR_MISSING_RETURN`; every `wapis` in nested blocks must also match (`SEMANTIC_ERROR_RETURN_TYPE_MISMATCH`).
   - **Calls:** A `khali` function may only be called as a statement; using its call as an expression is `SEMANTIC_ERROR_VOID_VALUE_USE`. A value-returning function may be called as a statement, in which case the result is discarded (the IR still emits `CALL` into an unused temporary). Calls resolve in the function namespace and variable references in the variable namespace.
   - **Structs:** A struct-typed variable may appear only as the base of a field access (`var.field`). Using a struct variable as an operand, assignment source or target, condition, argument, or return value is `SEMANTIC_ERROR_STRUCT_VALUE_USE` (Citu has no struct copy). Field access on a non-struct variable is `SEMANTIC_ERROR_TYPE_MISMATCH`; an unknown field is `SEMANTIC_ERROR_UNKNOWN_FIELD`; an unknown struct type in `struct S v` is `SEMANTIC_ERROR_UNKNOWN_STRUCT`.
   - **Duplicates:** Duplicate struct names, duplicate field names within a struct, duplicate function names (including names imported from earlier files), and duplicate parameter names are `SEMANTIC_ERROR_DUPLICATE_DECLARATION` (or `SEMANTIC_ERROR_DUPLICATE_GLOBAL_SYMBOL` for cross-file clashes, Section 5.4).
   - **Reserved Names:** A function named `_start`, or a function whose name begins with `M` followed by digits and `_L_`, is rejected with `SEMANTIC_ERROR_RESERVED_NAME` (these collide with linker-generated symbols, Section 5.7).
   - **Scope Visibility & Function-Wide Name Uniqueness (ICC rule):**
     1. Every `FUNC_DEF` creates a root function scope (e.g., `main`), and every nested `CST` inside `agar`, `magar`, `jabtak`, or a standalone `shro ... khtm` creates a child scope (e.g., `main.agar_1`, `main.agar_1.jabtak_1`), recorded as `@SCOPE_DEF` in `.sym`.
     2. **Visibility:** A variable declared in the `DLIST` of scope `S` is visible only inside `S` and its descendant scopes; referencing it elsewhere is `SEMANTIC_ERROR_UNDECLARED_IDENTIFIER`. Calling an unknown function is `SEMANTIC_ERROR_UNDECLARED_FUNCTION`.
     3. **No-Shadowing & Function-Wide Uniqueness:** Within one function, no two parameters or local variables (across the root scope and every nested scope, whether parent-child or sibling) may share the same `identName` (`SEMANTIC_ERROR_DUPLICATE_DECLARATION`). Hence `(enclosingFunc, identName)` uniquely identifies every local symbol in `.sym`, and Phase 4 may allocate all locals of a function in one frame at entry.
   - **Two-Pass Intra-File & Ordered Cross-File Resolution:** Phase 3 runs Pass 1 (registers all `STRUCT_DEF` and `FUNC_DEF` headers of the current file plus the exports of every earlier file's `.sym` on the command line) before Pass 2 (checking function bodies). In multi-file compilation (`icc fileA.citu fileB.citu -o app.exe`), provider files must precede consumer files on the command line; mutual cross-file recursion is unsupported.

---

### 3.5 Explicit Language Restrictions Implied by the Grammar
Each restriction below is enforced by the grammar (G) or by a named semantic rule (S):
1. **No Global Variables (G):** Only `struct` definitions and `fn` definitions exist at file scope.
2. **No Arrays (`a[i]`) (G):** `[` and `]` are not Citu tokens (`LEXICAL_ERROR_ILLEGAL_CHARACTER`).
3. **No Strings (`"..."`) (G):** `"` is not a Citu token.
4. **No Floating-Point Types or Literals (G):** `3.14` is a malformed number (`LEXICAL_ERROR_MALFORMED_NUMBER`).
5. **No Unary Minus (`-x`) or Unary Not (`!x`) (G):** Negative constants are written `0 - x`.
6. **No Boolean Operators (`&&`, `||`, `!`) or Chained Comparisons (`a < b < c`) (G).**
7. **No `break` or `continue` Statements (G).**
8. **No Struct Parameters (G):** `PARAM -> TYPE ID`.
9. **No Struct Return Values (G):** `RET_TYPE -> TYPE | 'khali'`.
10. **No Nested Struct Fields (G):** `FIELD -> TYPE ID`.
11. **No Chained Field Access `a.b.c` (G):** Member access is single-level (`ID . ID`).
12. **No Struct Copy or Struct Values in Expressions (S):** Section 3.4 rule 5.
13. **No Declaration With Initializer (G):** `DS -> TYPE ID`; an assignment is a separate statement after the `DLIST`.

### 3.6 Diagnostic Codes
| Phase | Codes |
| :--- | :--- |
| Lexer | `LEXICAL_ERROR_FORBIDDEN_CONSTRUCT`, `LEXICAL_ERROR_MALFORMED_NUMBER`, `LEXICAL_ERROR_INTEGER_TOO_LARGE`, `LEXICAL_ERROR_MALFORMED_CHAR`, `LEXICAL_ERROR_ILLEGAL_CHARACTER` |
| Parser | `SYNTAX_ERROR` (no table entry for the current lookahead) |
| Semantic | `SEMANTIC_ERROR_` + `UNDECLARED_IDENTIFIER`, `UNDECLARED_FUNCTION`, `UNKNOWN_STRUCT`, `UNKNOWN_FIELD`, `DUPLICATE_DECLARATION`, `DUPLICATE_GLOBAL_SYMBOL`, `TYPE_MISMATCH`, `LITERAL_OVERFLOW`, `CONSTANT_OVERFLOW`, `DIVISION_BY_ZERO`, `STRUCT_VALUE_USE`, `BAD_CONDITION_TYPE`, `VOID_VALUE_USE`, `MISSING_RETURN`, `RETURN_TYPE_MISMATCH`, `INVALID_MAIN_SIGNATURE`, `DUPLICATE_MAIN`, `RESERVED_NAME` |
| Linker | `LINKER_ERROR_` + `MULTIPLE_MAIN`, `MISSING_MAIN`, `DUPLICATE_DEFINITION`, `UNRESOLVED_EXTERNAL`, `SIGNATURE_MISMATCH` |
| Readers | `FATAL_IO_` + `MALFORMED_ESCAPE`, `FORMAT_MISMATCH`, `COUNT_MISMATCH`, `MISSING_END_FILE`, `INVARIANT_VIOLATION` |

---

## 4. Requirement 3: Compiler Data Structures & Complete AST Schema

### 4.1 Core C++ Data Structures Across Compiler Phases

```cpp
// 1. Lexical Token (Phase 2 Lexer -> .lex -> Phase 2 Parser)
struct Token {
    int index;               // 0 .. TotalTokens-1
    int line;                // 1-based line number
    int col;                 // 1-based column number
    std::string terminal;    // LL(1) table column terminal ("struct", "fn", "ID", "LT", "$")
    std::string category;    // KEYWORD, TYPE, IDENTIFIER, INT_LITERAL, CHAR_LITERAL, ARITH_OP, REL_OP, PUNCTUATION, EOF_MARKER
    std::string lexeme;      // Raw source text (percent-escaped in .lex if containing '%', '|', '\n', '\r')
    long long parsedIntVal;  // Numeric value for INT_LITERAL / ASCII code (0..127) for CHAR_LITERAL; -1 otherwise
};

// 2. Abstract Syntax Tree Node (Phase 2 Parser -> .ast -> Phase 3 Semantic & Phase 4 CodeGen / IR Builder)
struct ASTNode {
    int nodeId;                  // Pre-order integer ID (0 .. TotalNodes-1; 0 is always root PROG)
    int parentId;                // Parent nodeId (-1 for root PROG node)
    int childIndex;              // 0-based index among parent's children (-1 for root PROG node)
    std::string nodeKind;        // One of the 23 AST node kinds defined in Section 4.2
    std::string astOp;           // Operator/type tag ("+", "-", "*", "/", "<", "=", "medalo", "int32", "@NONE")
    std::string lexeme;          // Source identifier/literal text, or "@NONE" if not applicable
    long long litVal;            // Numeric value for INT_LIT / ASCII code for CHAR_LIT; -1 otherwise
    int line;                    // 1-based source line number
    std::vector<int> childIds;   // Reconstructed in RAM by AstReader using parentId and childIndex
};

// 3. Struct & Field Metadata (Phase 3 .sym logical layout; Phase 4 stack slot layout)
struct FieldInfo {
    int fieldIndex;          // 0 .. FieldCount-1
    std::string fieldName;   // e.g., "width"
    std::string fieldType;   // "int16", "int32", "uint16", "uint32", "char"
    int byteSize = -1;       // Phase 4: True architectural width (1, 2, or 4 bytes)
    int slotOffset = -1;     // Phase 4: 4-byte aligned field offset from struct base (0, 4, 8...)
};

struct StructInfo {
    int structId;                    // 0 .. StructCount-1 (within compilation unit's .sym)
    std::string structName;          // Globally unique struct name (e.g., "Box")
    bool isExternal = false;         // True if imported from an earlier compilation unit's .sym
    std::string sourceFile;          // Defining .citu filename
    int lineDeclared;
    std::vector<FieldInfo> fields;
    int totalSlotBytes = -1;         // Phase 4: 4 * fields.size() (ICC 4-byte slot convention)
};

// 4. Function & Parameter Metadata (Phase 3 .sym signature; Phase 4 frame size)
struct ParamInfo {
    int paramIndex;          // 0 .. ParamCount-1
    std::string paramName;   // e.g., "w"
    std::string paramType;   // "int16", "int32", "uint16", "uint32", "char"
};

struct FunctionInfo {
    int funcId;                      // 0 .. FuncCount-1 (within compilation unit's .sym)
    std::string funcName;            // Globally unique function name ("calcArea", "main")
    std::string returnType;          // "int16", "int32", "uint16", "uint32", "char", "khali"
    std::vector<ParamInfo> params;
    bool isExternal = false;         // True if imported from an earlier compilation unit's .sym
    std::string sourceFile;          // Defining .citu filename
    int lineDeclared;
    int stackFrameBytes = -1;        // Phase 4: Total bytes reserved via 'sub esp, N' (locals + temporaries)
};

// 5. Scope Hierarchy, Symbol Entry & AST Node Type Annotation (Phase 3 -> .sym)
struct ScopeInfo {
    int scopeId;                     // 0 .. ScopeCount-1
    std::string scopePath;           // e.g., "main", "main.agar_1", "main.agar_1.jabtak_1"
    std::string parentScopePath;     // Enclosing scope path or "@NONE" for function root scope
    std::string enclosingFunc;       // Owning function name ("main")
    int lineStart;                   // Line number of 'shro'
    int lineEnd;                     // Line number of 'khtm'
};

struct SymbolTableEntry {
    int symId;                       // 0 .. SymbolCount-1
    std::string identName;           // Variable or parameter name ("area", "myBox")
    std::string symKind;             // "PARAM", "LOCAL_VAR", "STRUCT_VAR"
    std::string dataType;            // "int16", "int32", "uint16", "uint32", "char", "struct Box"
    std::string enclosingFunc;       // Function owning this symbol
    std::string scopePath;           // Exact ScopeInfo.scopePath where declared
    int lineDeclared;
    int byteSize = -1;               // Phase 4: 1, 2, 4, or totalSlotBytes for struct
    int ebpOffset = -1;              // Phase 4: +8, +12... for PARAM; address of first slot (negative) for LOCAL_VAR/STRUCT_VAR
};

struct NodeTypeAnnotation {
    int astNodeId;                   // Matches ASTNode.nodeId in .ast (0 .. TotalNodes-1)
    std::string evaluatedType;       // Concrete resolved type ("int32", "BOOL_COND", "khali", "struct Box", "@NONE")
    std::string refKind;             // "LOCAL_SYM", "FUNC_REF", "STRUCT_FIELD_DECL", "FIELD_ACCESS_REF", "STRUCT_TYPE", or "@NONE"
    int resolvedId;                  // symId (LOCAL_SYM / FIELD_ACCESS_REF), funcId (FUNC_REF), structId (STRUCT_TYPE / STRUCT_FIELD_DECL), or -1
    int resolvedSubId;               // fieldIndex (0..F-1) for STRUCT_FIELD_DECL and FIELD_ACCESS_REF; -1 otherwise
    bool isConstExpr;                // True if compile-time constant expression
    long long foldedConstVal;        // Folded 64-bit constant value; meaningful only when isConstExpr == true (else -1)
};

struct SymbolTable {
    std::vector<StructInfo> structs;
    std::vector<FunctionInfo> functions;
    std::vector<ScopeInfo> scopes;
    std::vector<SymbolTableEntry> symbols;
    std::vector<NodeTypeAnnotation> nodeTypes;
};

// 6. Target-Neutral IR Instruction (Option A: .ir / .opt1.ir)
struct IRInstruction {
    int irId;                // Sequential instruction index (0 .. IrCount-1)
    std::string opcode;      // One of the 23 defined IR opcodes (Section 5.5)
    std::string dataType;    // Operand/operation primitive type ("int16", "int32", "uint16", "uint32", "char", "BOOL_COND", "khali", "@NONE")
    std::string dest;        // Destination ("sym:2:area", "%t0", "lbl:L_END_1", "fn:calcArea", "field:..." never; or "@NONE")
    std::string arg1;        // First operand ("sym:0:w", "lit:10", "%t3", "fn:calcArea", "field:0:Box:0:width", or "@NONE")
    std::string arg2;        // Second operand ("sym:1:h", "field:0:Box:0:width", "argc:2", or "@NONE")
    int astNodeId;           // Originating ASTNode.nodeId (-1 if synthetic)
    std::string comment;     // Human-readable trace comment
};

// 7. Target-Specific 32-Bit x86 NASM Record (.cg1.asm, .cg2.asm, .opt1.asm, .opt2.asm, .linked.asm)
struct AsmRecord {
    int recId;               // Sequential record index (0 .. TotalRecords-1)
    std::string recKind;     // "DIR" (directive), "LBL" (standalone label), or "INS" (instruction)
    std::string nasmText;    // Exact NASM text before '; @...'
    std::string field1;      // DIR: dirKind ("SECTION","GLOBAL","EXTERN") | LBL: labelName | INS: opcode
    std::string field2;      // DIR: symName | LBL: labelKind ("FUNC_ENTRY","BRANCH_TARGET") | INS: op1
    std::string field3;      // DIR: "@NONE" | LBL: enclosingFunc | INS: op2
    std::string dataType;    // INS: operand type ("int16", "int32", "uint16", "uint32", "char", "khali", "@NONE")
    int astNodeId;           // Originating ASTNode.nodeId (-1 if synthetic)
    std::string comment;     // Human-readable comment
};

// 8. Memory-Map Metadata carried in assembly headers (Section 5.6-B)
struct MemStructInfo { std::string structName; int totalSlotBytes; std::string fieldLayout; };      // ; @MEM_STRUCT
struct MemFuncInfo   { std::string funcName;   int stackFrameBytes; };                              // ; @MEM_FUNC
struct MemSymInfo    { int symId; std::string identName, enclosingFunc; int archByteSize, ebpOffset; };  // ; @MEM_SYM
struct MemTempInfo   { std::string tempName, enclosingFunc; int archByteSize, ebpOffset; };         // ; @MEM_TEMP
struct AsmModule {
    std::string format, source;                  // ; @FORMAT tag, ; @SOURCE
    std::vector<MemStructInfo> memStructs;
    std::vector<MemFuncInfo>   memFuncs;
    std::vector<MemSymInfo>    memSyms;
    std::vector<MemTempInfo>   memTemps;
    std::vector<AsmRecord>     records;
};
```

**Cross-structure consistency checks (audit result).** Every column of every serialized record has a field in exactly one structure above: `.lex` $\leftrightarrow$ `Token`; `.ast` $\leftrightarrow$ `ASTNode` (`childIds` is rebuilt from `parentId` and `childIndex`, so the tree is fully reconstructible from the eight serialized fields); `.sym` Sections 1–5 $\leftrightarrow$ `StructInfo`/`FieldInfo`, `FunctionInfo`/`ParamInfo`, `ScopeInfo`, `SymbolTableEntry`, `NodeTypeAnnotation`; `.ir` $\leftrightarrow$ `IRInstruction`; `.asm` $\leftrightarrow$ `AsmRecord` and the memory-map structures. The Phase 4 fields (`byteSize`, `slotOffset`, `totalSlotBytes`, `stackFrameBytes`, `ebpOffset`) are **not** stored in `.sym`; they are carried by the `; @MEM_*` header lines of the `.asm` files. IDs (`structId`, `funcId`, `symId`, `scopeId`) are local to one `.sym` file; cross-file references always use the globally unique `structName` / `funcName`.

---

### 4.2 Complete AST Node Schema (All 23 `NODE_KIND` Definitions)

#### A. Grammar-Only Non-Terminals & Tokens Omitted from the AST
During Phase 2 AST construction, the following concrete parse-tree artifacts are removed after syntax validation:
1. **27 Grammar-Helper & List-Wrapper Non-Terminals Omitted:** `STRUCT_LIST`, `FIELD_LIST`, `FIELD_TAIL`, `FUNC_LIST`, `FUNC_TAIL`, `RET_TYPE`, `FUNC_NAME`, `PARAM_LIST`, `PARAM_TAIL`, `TYPE`, `ST`, `ELSE_PART`, `RET_VAL`, `AS`, `AS_TAIL`, `VAR_TAIL`, `COND`, `COND_TAIL`, `REL_OP`, `EX`, `EX'`, `T`, `T'`, `F`, `F_TAIL`, `ARG_LIST`, `ARG_TAIL`. (The remaining 12 non-terminals map to AST kinds: `PROG`, `STRUCT_DEF`, `FIELD`, `FUNC_DEF`, `PARAM`, `CST`, `DLIST`, `DS`, `SLIST`, `WS`, `IF_STMT`, `RET_STMT`.)
2. **All 15 Nullable `ε` Productions Create No Node:** empty list terminations never create AST nodes; the parent simply has `0` children of that category. The one exception is `BLOCK_CST`, which always has exactly two children, `DECL_LIST` (child `0`) and `STMT_LIST` (child `1`); these exist even when empty.
3. **Punctuation & Delimiter Tokens Omitted:** `'('`, `')'`, `','`, `'shro'`, `'khtm'`, and `'$'` never appear as nodes. Parentheses in `F -> '(' EX ')'` leave no node; grouping is expressed only by the tree shape.

#### B. Complete 23-Row `NODE_KIND` Schema Table

| # | `NODE_KIND` | Grammar Rule(s) Represented | Required Children & Exact `CHILD_INDEX` Order | `AST_OP` Value | `LEXEME` Value | `LIT_VAL` |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `PROG` | Rule 1 (`PROG`) | `0..S-1`: `STRUCT_DEF` ($S \ge 0$)<br>`S..S+F-1`: `FUNC_DEF` ($F \ge 1$) | `@NONE` | `@NONE` | `-1` |
| 2 | `STRUCT_DEF` | Rule 4 (`STRUCT_DEF`) | `0..K-1`: `FIELD_DECL` ($K \ge 1$, in declaration order) | `"struct"` | Struct name (`ID`, e.g., `"Box"`) | `-1` |
| 3 | `FIELD_DECL` | Rule 8 (`FIELD`) | None (leaf) | Field primitive type (`"int16".."char"`) | Field name (`ID`, e.g., `"width"`) | `-1` |
| 4 | `FUNC_DEF` | Rule 12 (`FUNC_DEF`) | `0..P-1`: `PARAM_DECL` ($P \ge 0$)<br>`P` (last child): `BLOCK_CST` (function body) | Return type (`"int16".."char"` or `"khali"`) | Function name (`ID` or `"main"`) | `-1` |
| 5 | `PARAM_DECL` | Rule 21 (`PARAM`) | None (leaf) | Parameter primitive type (`"int16".."char"`) | Parameter name (`ID`, e.g., `"w"`) | `-1` |
| 6 | `BLOCK_CST` | Rule 22 (`CST`) | `0`: `DECL_LIST`<br>`1`: `STMT_LIST` (always exactly 2 children) | `@NONE` | `@NONE` | `-1` |
| 7 | `DECL_LIST` | Rules 23–24 (`DLIST`) | `0..D-1`: `VAR_DECL` or `STRUCT_VAR_DECL` ($D \ge 0$, declaration order) | `@NONE` | `@NONE` | `-1` |
| 8 | `STMT_LIST` | Rules 32–33 (`SLIST`) | `0..M-1`: statement nodes (`ASSIGN_STMT`, `WHILE_STMT`, `IF_STMT`, `RETURN_STMT`, `CALL_STMT`, `BLOCK_CST`; $M \ge 0$) | `@NONE` | `@NONE` | `-1` |
| 9 | `VAR_DECL` | Rule 25 (`DS -> TYPE ID`) | None (leaf) | Variable primitive type (`"int16".."char"`) | Variable name (`ID`, e.g., `"area"`) | `-1` |
| 10 | `STRUCT_VAR_DECL` | Rule 26 (`DS -> 'struct' ID ID`) | None (leaf) | Struct type name (`ID`, e.g., `"Box"`) | Variable name (`ID`, e.g., `"myBox"`) | `-1` |
| 11 | `ASSIGN_STMT` | Rules 46–47 (`AS`) | `0`: target lvalue (`VAR_REF` or `FIELD_ACCESS`)<br>`1`: value expression (`<EXPR>`) | `"medalo"` | `"medalo"` | `-1` |
| 12 | `WHILE_STMT` | Rule 39 (`WS`) | `0`: condition (`REL_EXPR` or `<EXPR>`)<br>`1`: loop body (`BLOCK_CST`) | `"jabtak"` | `"jabtak"` | `-1` |
| 13 | `IF_STMT` | Rules 40–42 (`IF_STMT`) | `0`: condition (`REL_EXPR` or `<EXPR>`)<br>`1`: then-branch (`BLOCK_CST`)<br>`2` (present iff Rule 41 `'magar'` matched): else-branch (`BLOCK_CST`) | `"agar"` | `"agar"` | `-1` |
| 14 | `RETURN_STMT` | Rules 43–45 (`RET_STMT`) | `0`: payload (`<EXPR>` for Rule 44, or `VOID_KHALI` for Rule 45; always 1 child) | `"wapis"` | `"wapis"` | `-1` |
| 15 | `CALL_STMT` | Rules 46, 48 (call as statement) | `0..A-1`: argument expressions (`<EXPR>`, $A \ge 0$, left-to-right) | `"call"` | Callee function name (`ID`) | `-1` |
| 16 | `REL_EXPR` | Rules 51–52 (`EX REL_OP EX`) | `0`: left operand (`<EXPR>`)<br>`1`: right operand (`<EXPR>`) | Relational operator (`"<"`, `">"`, `"="`, `"!="`, `"<="`, `">="`) | Same as `AST_OP` | `-1` |
| 17 | `BIN_EXPR` | Rules 60–67 (`EX`, `T` folded left-associatively) | `0`: left operand (`<EXPR>`)<br>`1`: right operand (`<EXPR>`) | Arithmetic operator (`"+"`, `"-"`, `"*"`, `"/"`) | Same as `AST_OP` | `-1` |
| 18 | `FUNC_CALL_EXPR` | Rules 68, 72 (call in expression) | `0..A-1`: argument expressions (`<EXPR>`, $A \ge 0$, left-to-right) | `"call"` | Callee function name (`ID`) | `-1` |
| 19 | `FIELD_ACCESS` | Rules 49, 71 (`ID '.' ID`) | `0`: struct variable (`VAR_REF`, exactly 1 child) | `"."` | Accessed field name (`ID`) | `-1` |
| 20 | `VAR_REF` | Rules 46, 68, 73 (`ID`) | None (leaf) | `@NONE` | Variable identifier (`ID`) | `-1` |
| 21 | `INT_LIT` | Rule 69 (`LT`, `INT_LITERAL`) | None (leaf) | `@NONE` | Raw digit string (e.g., `"10"`) | Parsed integer ($0 \dots 4294967295$) |
| 22 | `CHAR_LIT` | Rule 69 (`LT`, `CHAR_LITERAL`) | None (leaf) | `@NONE` | Raw quoted literal (e.g., `"'A'"`) | Resolved ASCII code ($0 \dots 127$) |
| 23 | `VOID_KHALI` | Rule 45 (`RET_VAL -> 'khali'`) | None (leaf) | `"khali"` | `"khali"` | `-1` |

*(`<EXPR>` denotes any expression node kind: `BIN_EXPR`, `FUNC_CALL_EXPR`, `FIELD_ACCESS`, `VAR_REF`, `INT_LIT`, or `CHAR_LIT`.)*

#### C. `LINE_NUM` Rule for AST Nodes
A node's `LINE_NUM` is the line of its defining token (`struct`, `fn`, the type keyword or identifier of a declaration, the operator of `BIN_EXPR`/`REL_EXPR`, the identifier of a reference or call, the literal token, `jabtak`, `agar`, `wapis`, or `khali`). `PROG` takes the line of the first token of the file; `BLOCK_CST` takes the line of its `shro`; `DECL_LIST` and `STMT_LIST` take the line of their first child, or, when empty, the line of the enclosing `shro`.

---

## 5. Requirement 4: Intermediate File Formats Specification

### 5.1 Universal File Encoding, Delimiter Framing, Field Classes & Escaping Rules

1. **Encoding & Line Endings:** All intermediate files use **UTF-8 without BOM** with `\n` line endings. Readers strip a single trailing `\r` if present before parsing a line.
2. **Exact Delimiter Framing (no destructive trimming):**
   - **Writer Rule:** Writers join the fields of a record with the exact 3-character string **`" | "`** (space, pipe, space), with no leading space before Field 0 and no trailing space before `\n`.
   - **Reader Rule:** Readers split a line on the raw pipe `|`. For each slice they remove **at most one trailing space** (all fields except the last) and **at most one leading space** (all fields except the first). Readers never perform arbitrary `.trim()` on field contents, so spaces inside free-text fields are preserved byte-for-byte.
3. **Field Classes:**
   - **Class A: Free-Text Escaped Fields** (`@SOURCE` value, `SOURCE_FILE`, `LEXEME`, `COMMENT`): arbitrary printable text. Writers encode in a **single left-to-right pass**: `%` $\rightarrow$ `%25`, `|` $\rightarrow$ `%7C`, `\n` (`0x0A`) $\rightarrow$ `%0A`, `\r` (`0x0D`) $\rightarrow$ `%0D`. Readers decode in a single left-to-right pass after removing the delimiter-padding space; a `%` that is not followed by exactly `25`, `7C`, `0A`, or `0D` (uppercase hex) halts with `FATAL_IO_MALFORMED_ESCAPE`. The scheme is unambiguous and reversible because `%` itself is always encoded and no decoded output is re-scanned. A backslash sequence inside a Citu character literal (`'\n'`) is two ordinary characters and needs no escaping. **Only Class A fields are escaped or decoded; every other class is written and read raw.**
   - **Class B: Structural / Identifier / IR Operand Fields** (`TERMINAL`, `CATEGORY`, `NODE_KIND`, `AST_OP`, `STRUCT_NAME`, `FIELD_NAME`, `FIELD_TYPE`, `FUNC_NAME`, `PARAM_NAME`, `PARAM_TYPE`, `IDENT_NAME`, `SYM_KIND`, `DATA_TYPE`, `RETURN_TYPE`, `ENCLOSING_FUNC`, `SCOPE_PATH`, `PARENT_SCOPE_PATH`, `EVALUATED_TYPE`, `REF_KIND`, `OPCODE` of `.ir`, `DEST`, `ARG1`, `ARG2`): must match the regular expression `^[A-Za-z0-9_.:+%\[\] -]+$` (note that the hyphen is the last class member and is literal; raw `|` and `,` are never allowed), or equal `@NONE`. Exceptions: `TERMINAL` and `AST_OP` may additionally be one of the operator/punctuation tokens `$`, `<`, `>`, `=`, `!=`, `<=`, `>=`, `*`, `/`, `(`, `)`, `,`.
   - **Class C: Memory-Layout Descriptor** (the `FIELD_LAYOUT` field of `; @MEM_STRUCT`): `^[A-Za-z0-9_:,+-]+$` or `@NONE`, permitting `,` and `:` for comma-separated `FIELD_NAME:SLOT_OFFSET:ARCH_BYTE_SIZE` tuples.
   - **Class D: Assembly Name & Operand Fields** (`.asm` fields `DIR_KIND`, `SYM_NAME`, `LABEL_NAME`, `LABEL_KIND`, `ENCLOSING_FUNC`, `OPCODE`, `OP1`, `OP2`, and `TEMP_NAME`): `^[A-Za-z0-9_.@%+\[\] -]+$` or `@NONE` (the `@` admits `_ExitProcess@4`; the `%` admits temporary names such as `%t0`).
   - **Class N: Numeric Fields** (every `*_ID`, `*_INDEX`, `LINE_*`, `COL_NUM`, count, `LIT_VAL`, `PARSED_INT_VAL`, `FOLDED_CONST_VAL`, `IS_*` flag): `^-?[0-9]+$`; signed offsets (`SLOT_OFFSET` excepted) such as `EBP_OFFSET` are written with an explicit sign, `^[+-][0-9]+$`.
4. **Missing-Value Sentinels:** an empty or not-applicable string field is **`@NONE`**; an empty or not-applicable numeric field is **`-1`**. A Class A field whose text is empty is written `@NONE`.
5. **Line Categories & Header Count Accounting:**
   - **Uncounted:** empty lines; lines starting with `#` (`.lex`, `.ast`, `.sym`, `.ir`) or `; #` (`.asm`); and the metadata lines `@FORMAT`, `@SOURCE`, `@TOTAL_...`, `@COUNTS`, `; @MEM_STRUCT`, `; @MEM_FUNC`, `; @MEM_SYM`, `; @MEM_TEMP`, and `@END_FILE` / `; @END_FILE`.
   - **Counted payload records:** every other record, counted toward its header total (`@TOTAL_TOKENS`, `@TOTAL_NODES`, `@COUNTS`, `@TOTAL_IR_INSTRS`, `@TOTAL_RECORDS`).
6. **Header/Footer Contract:** every file starts with `@FORMAT | <TAG> | v1.0` (`; @FORMAT | <TAG> | v1.0` in `.asm` files), then `@SOURCE`, then its count line(s); every file ends with `@END_FILE` (`; @END_FILE`). Readers verify the tag, the version, every count, and the sentinel before accepting the file. Valid tags: `ICC_LEX`, `ICC_AST`, `ICC_SYM`, `ICC_IR`, `ICC_OPT1_IR`, `ICC_CG1_ASM`, `ICC_CG2_ASM`, `ICC_OPT1_ASM`, `ICC_OPT2_ASM`, `ICC_LINKED_ASM`.

---

### 5.2 Format 1: `<prog>.lex` (Lexer Output)

- **Written By:** Phase 2 Lexer (`LexWriter`). **Read By:** Phase 2 Parser (`LexReader`).
- **Counted Record Schema (7 fields, `TOKEN_INDEX` `0 .. N-1`):**
  `TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL`
- **`LexReader` Validation Protocol:**
  1. `@TOTAL_TOKENS` ($N$) must be $\ge 1$, and `TOKEN_INDEX` must run contiguously from `0` to `N - 1`.
  2. **`(LINE_NUM, COL_NUM)` ordering:** every record has `LINE_NUM >= 1` and `COL_NUM >= 1`; for consecutive records $i-1$, $i$: `LINE_NUM[i] >= LINE_NUM[i-1]`, and if the lines are equal then `COL_NUM[i] > COL_NUM[i-1]`.
  3. **Unique final EOF token:** record `N - 1` has `TERMINAL == "$"`, `CATEGORY == "EOF_MARKER"`, `LEXEME == "$"`, `PARSED_INT_VAL == -1`; no earlier record has `TERMINAL == "$"` or `CATEGORY == "EOF_MARKER"`.
  4. **Literal consistency:** if `TERMINAL == "LT"` then `CATEGORY` is `INT_LITERAL` ($0 \le \text{PARSED\_INT\_VAL} \le 4294967295$) or `CHAR_LITERAL` ($0 \le \text{PARSED\_INT\_VAL} \le 127$); for every other terminal `PARSED_INT_VAL == -1`.
- **Complete `.lex` for `min_return.citu`.** Source file `min_return.citu` (exactly these three lines):
  ```text
  fn int32 main ( ) shro
      wapis 0
  khtm
  ```
  Column ruler for reference (not part of the file): `fn` starts at column 1, `int32` at 4, `main` at 10, `(` at 15, `)` at 17, `shro` at 19; `wapis` at column 5 and `0` at column 11 on line 2; `khtm` at column 1 on line 3, and the EOF token at line 3, column 5 (the file has no trailing newline).
  ```text
  @FORMAT | ICC_LEX | v1.0
  @SOURCE | min_return.citu
  @TOTAL_TOKENS | 10
  # TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL
  0 | 1 | 1 | fn | KEYWORD | fn | -1
  1 | 1 | 4 | int32 | TYPE | int32 | -1
  2 | 1 | 10 | main | KEYWORD | main | -1
  3 | 1 | 15 | ( | PUNCTUATION | ( | -1
  4 | 1 | 17 | ) | PUNCTUATION | ) | -1
  5 | 1 | 19 | shro | KEYWORD | shro | -1
  6 | 2 | 5 | wapis | KEYWORD | wapis | -1
  7 | 2 | 11 | LT | INT_LITERAL | 0 | 0
  8 | 3 | 1 | khtm | KEYWORD | khtm | -1
  9 | 3 | 5 | $ | EOF_MARKER | $ | -1
  @END_FILE
  ```

---

### 5.3 Format 2: `<prog>.ast` (Parser Output: Abstract Syntax Tree)

- **Written By:** Phase 2 Parser (`AstWriter`). **Read By:** Phase 3 Semantic Analyzer and Phase 4 (IR Builder under Option A, CodeGen 1 under Option B) via `AstReader`.
- **Counted Record Schema (8 fields, pre-order `0 .. N-1`):**
  `NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM`
- **`AstReader` Validation Protocol (6 invariants):**
  1. **Exact count:** exactly $N$ counted records matching `@TOTAL_NODES | <N>` ($N \ge 1$), then `@END_FILE`.
  2. **Contiguous unique IDs:** `NODE_ID` unique and contiguous from `0` to `N - 1`.
  3. **Unique root:** `NODE_ID == 0` is the only node with `PARENT_ID == -1` and `CHILD_INDEX == -1`, and has `NODE_KIND == "PROG"`.
  4. **Parent before child:** every `1 <= NODE_ID < N` has `0 <= PARENT_ID < NODE_ID`.
  5. **Ordered children and schema:** for every parent $p$, its children in ascending `NODE_ID` have `CHILD_INDEX` equal to `0, 1, ..., K - 1` (`CHILD_INDEX == tree[p].childIds.size()` before insertion), and the final child count and child `NODE_KIND`s match the Section 4.2 schema for `tree[p].nodeKind`.
  6. **Reachability:** a depth-first traversal from node `0` over `childIds` visits all $N$ nodes.
- **Complete `.ast` for `min_return.citu` (all 7 nodes):**
  ```text
  @FORMAT | ICC_AST | v1.0
  @SOURCE | min_return.citu
  @TOTAL_NODES | 7
  # NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM
  0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1
  1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1
  2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1
  3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1
  4 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 2
  5 | 4 | 0 | RETURN_STMT | wapis | wapis | -1 | 2
  6 | 5 | 0 | INT_LIT | @NONE | 0 | 0 | 2
  @END_FILE
  ```
- **Complete `.ast` for `computeHalf.citu` (all 14 nodes).** Source file `computeHalf.citu`; this module is used again in Sections 5.5 and 5.6:
  ```text
  fn uint32 computeHalf ( uint32 base ) shro
      uint32 res
      res medalo base / 2
      wapis res
  khtm
  ```
  ```text
  @FORMAT | ICC_AST | v1.0
  @SOURCE | computeHalf.citu
  @TOTAL_NODES | 14
  # NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM
  0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1
  1 | 0 | 0 | FUNC_DEF | uint32 | computeHalf | -1 | 1
  2 | 1 | 0 | PARAM_DECL | uint32 | base | -1 | 1
  3 | 1 | 1 | BLOCK_CST | @NONE | @NONE | -1 | 1
  4 | 3 | 0 | DECL_LIST | @NONE | @NONE | -1 | 2
  5 | 4 | 0 | VAR_DECL | uint32 | res | -1 | 2
  6 | 3 | 1 | STMT_LIST | @NONE | @NONE | -1 | 3
  7 | 6 | 0 | ASSIGN_STMT | medalo | medalo | -1 | 3
  8 | 7 | 0 | VAR_REF | @NONE | res | -1 | 3
  9 | 7 | 1 | BIN_EXPR | / | / | -1 | 3
  10 | 9 | 0 | VAR_REF | @NONE | base | -1 | 3
  11 | 9 | 1 | INT_LIT | @NONE | 2 | 2 | 3
  12 | 6 | 1 | RETURN_STMT | wapis | wapis | -1 | 4
  13 | 12 | 0 | VAR_REF | @NONE | res | -1 | 4
  @END_FILE
  ```

---

### 5.4 Format 3: `<prog>.sym` (Semantic Analyzer Output & Cross-File Import Semantics)

- **Written By:** Phase 3 Semantic Analyzer (`SymWriter`).
- **Read By:** Phase 3 (for each later file, Sections 1 and 2 of every earlier `.sym`), Phase 4 (`SymReader`, all 5 sections), and Phase 8 Linker (Sections 1 and 2).
- **Header & Section Schemas:**
  ```text
  @FORMAT | ICC_SYM | v1.0
  @SOURCE | <source_filename>
  @COUNTS | STRUCTS=<S> | FUNCS=<F> | SCOPES=<C> | SYMBOLS=<L> | NODE_TYPES=<T>
  ```
  1. **Section 1: Struct Blueprints** (`STRUCTS=<S>` counts `@STRUCT_DEF` rows `0 .. S-1`):
     `@STRUCT_DEF | STRUCT_ID | STRUCT_NAME | FIELD_COUNT | IS_EXTERNAL | SOURCE_FILE | LINE_DECLARED`
     `@STRUCT_FIELD | STRUCT_NAME | FIELD_INDEX | FIELD_NAME | FIELD_TYPE`
  2. **Section 2: Function Signatures** (`FUNCS=<F>` counts `@FUNC_DEF` rows `0 .. F-1`):
     `@FUNC_DEF | FUNC_ID | FUNC_NAME | RETURN_TYPE | PARAM_COUNT | IS_EXTERNAL | SOURCE_FILE | LINE_DECLARED`
     `@FUNC_PARAM | FUNC_NAME | PARAM_INDEX | PARAM_NAME | PARAM_TYPE`
  3. **Section 3: Lexical Scope Hierarchy** (`SCOPES=<C>` counts `@SCOPE_DEF` rows `0 .. C-1`):
     `@SCOPE_DEF | SCOPE_ID | SCOPE_PATH | PARENT_SCOPE_PATH | ENCLOSING_FUNC | LINE_START | LINE_END`
  4. **Section 4: Function-Scoped Symbols** (`SYMBOLS=<L>` counts `@SYMBOL` rows `0 .. L-1`):
     `@SYMBOL | SYM_ID | IDENT_NAME | SYM_KIND | DATA_TYPE | ENCLOSING_FUNC | SCOPE_PATH | LINE_DECLARED`
  5. **Section 5: AST Node Annotations** (`NODE_TYPES=<T>` counts `@NODE_TYPE` rows `0 .. T-1`):
     `@NODE_TYPE | AST_NODE_ID | EVALUATED_TYPE | REF_KIND | RESOLVED_ID | RESOLVED_SUB_ID | IS_CONST_EXPR | FOLDED_CONST_VAL`
  The `@STRUCT_FIELD` and `@FUNC_PARAM` rows are **not** counted separately; they belong to the `@STRUCT_DEF` / `@FUNC_DEF` row of the same name and number exactly `FIELD_COUNT` / `PARAM_COUNT` rows in index order. `@FUNC_PARAM` rows are listed for `IS_EXTERNAL = 1` functions as well.
- **Annotation conventions for `@NODE_TYPE`:** expression nodes carry their evaluated type; `ASSIGN_STMT`, `RETURN_STMT` and `CALL_STMT` carry the type of the assigned, returned or callee-returned value (`khali` for `wapis khali` and `khali` calls); `WHILE_STMT`, `IF_STMT`, structural list/block/`PROG` nodes carry `@NONE`; `REL_EXPR` carries `BOOL_COND`; `FUNC_DEF` carries its return type with `FUNC_REF`; declaration nodes carry their declared type with `LOCAL_SYM` (`STRUCT_FIELD_DECL` for `FIELD_DECL`, `STRUCT_TYPE` for `STRUCT_DEF`). `FOLDED_CONST_VAL` is meaningful only when `IS_CONST_EXPR == 1` (otherwise it is `-1` and must be ignored), which is how a legitimate folded value of `-1` stays distinguishable.
- **`SymReader` Validation Protocol:**
  1. Counts match `@COUNTS` and the file ends with `@END_FILE`; IDs in each section are contiguous from `0`.
  2. Every `@STRUCT_FIELD` names a defined struct, with indices `0 .. FIELD_COUNT-1`; every `@FUNC_PARAM` names a defined function, with indices `0 .. PARAM_COUNT-1`.
  3. `IS_EXTERNAL` is `0` or `1`; `SOURCE_FILE` equals `@SOURCE` exactly when `IS_EXTERNAL == 0`.
  4. Each scope's `PARENT_SCOPE_PATH` is `@NONE` (function root) or an existing `SCOPE_PATH` of the same function; each symbol's `SCOPE_PATH` and `ENCLOSING_FUNC` exist, and `(ENCLOSING_FUNC, IDENT_NAME)` is unique.
  5. `NODE_TYPES` equals the `@TOTAL_NODES` of the corresponding `.ast`, with `AST_NODE_ID` running `0 .. T-1`; every `RESOLVED_ID` is a valid ID of the table named by `REF_KIND`.

#### Cross-File `.sym` Import, Deduplication & Linking Semantics
1. **Importing earlier compilation units' signatures:** When `icc fileA.citu fileB.citu -o app.exe` compiles `fileB.citu`, Phase 3 first reads Sections 1 and 2 of `fileA.sym` (excluding `main`). Each imported struct and function enters `fileB`'s in-memory `SymbolTable` with `IS_EXTERNAL = 1` and its original `SOURCE_FILE`. With more files, every earlier file's `.sym` is read in command-line order.
2. **Representation in `fileB.sym`:** All structs and functions of `fileB`'s symbol table, imported (`IS_EXTERNAL = 1`) and local (`IS_EXTERNAL = 0`), are written to Sections 1 and 2 and **are included** in `STRUCTS` and `FUNCS`, with contiguous file-local IDs.
3. **Transitive imports vs. duplicate definitions:** If a name is loaded more than once from prior `.sym` files (for example `fileA`'s function appears in `fileA.sym` as `IS_EXTERNAL = 0` and in `fileB.sym` as `IS_EXTERNAL = 1`), the repeated load is ignored when signature and defining `SOURCE_FILE` match. If the current file **defines** a struct or function whose name already exists in the loaded table, Phase 3 halts with `SEMANTIC_ERROR_DUPLICATE_GLOBAL_SYMBOL`.
4. **How Phase 8 distinguishes definitions from imports:** `IS_EXTERNAL == 0` records are concrete definitions; two of them for one name across all linked `.sym` files give `LINKER_ERROR_DUPLICATE_DEFINITION`. Every `IS_EXTERNAL == 1` record must match one `IS_EXTERNAL == 0` definition with identical return type and parameter (or field) list (`LINKER_ERROR_UNRESOLVED_EXTERNAL` / `LINKER_ERROR_SIGNATURE_MISMATCH`). Because `main` is never imported, two `main` definitions in different files are caught here as `LINKER_ERROR_MULTIPLE_MAIN`.
5. **File-local ID validity across linked files:** Within each file, `@NODE_TYPE`, `.ir` and `.asm` metadata use that file's local `STRUCT_ID`, `FUNC_ID` and `SYM_ID`. Phase 4 resolves all struct and variable references into concrete `[ebp +/- offset]` operands and emits calls, exports and externs by the globally unique `FUNC_NAME`. Phase 8 therefore links by `FUNC_NAME` and by module-prefixed branch labels, so local integer IDs of separate `.sym` files never conflict.

- **Complete `.sym` for `min_return.citu`:**
  ```text
  @FORMAT | ICC_SYM | v1.0
  @SOURCE | min_return.citu
  @COUNTS | STRUCTS=0 | FUNCS=1 | SCOPES=1 | SYMBOLS=0 | NODE_TYPES=7
  # --- SECTION 1: STRUCT BLUEPRINTS (0 Structs) ---
  # --- SECTION 2: FUNCTION SIGNATURES (1 Function) ---
  @FUNC_DEF | 0 | main | int32 | 0 | 0 | min_return.citu | 1
  # --- SECTION 3: LEXICAL SCOPE HIERARCHY (1 Scope) ---
  @SCOPE_DEF | 0 | main | @NONE | main | 1 | 3
  # --- SECTION 4: FUNCTION-SCOPED SYMBOLS (0 Symbols) ---
  # --- SECTION 5: AST NODE SEMANTIC ANNOTATIONS (All 7 Nodes 0..6) ---
  @NODE_TYPE | 0 | @NONE | @NONE | -1 | -1 | 0 | -1
  @NODE_TYPE | 1 | int32 | FUNC_REF | 0 | -1 | 0 | -1
  @NODE_TYPE | 2 | @NONE | @NONE | -1 | -1 | 0 | -1
  @NODE_TYPE | 3 | @NONE | @NONE | -1 | -1 | 0 | -1
  @NODE_TYPE | 4 | @NONE | @NONE | -1 | -1 | 0 | -1
  @NODE_TYPE | 5 | int32 | @NONE | -1 | -1 | 0 | -1
  @NODE_TYPE | 6 | int32 | @NONE | -1 | -1 | 1 | 0
  @END_FILE
  ```

---

### 5.5 Format 4: `<prog>.ir` and `<prog>.opt1.ir` (Target-Neutral IR; Option A)

- **Written By:** IR Builder (`IrWriter`, tag `ICC_IR`) and Phase 6 Optimizer 1 (`IrWriter`, tag `ICC_OPT1_IR`). **Read By:** Phase 6 Optimizer 1 and Phase 4 CodeGen 1 (`IrReader`).
- **Header, Counted Record Schema & Footer** (`<TAG>` is `ICC_IR` for `.ir` and `ICC_OPT1_IR` for `.opt1.ir`):
  ```text
  @FORMAT | <TAG> | v1.0
  @SOURCE | <source_filename>
  @TOTAL_IR_INSTRS | <N>
  # IR_ID | OPCODE | DATA_TYPE | DEST | ARG1 | ARG2 | AST_NODE_ID | COMMENT
  ...
  @END_FILE
  ```
- **Operand conventions:** program variables and parameters `sym:<SYM_ID>:<identName>`; temporaries `%t0`, `%t1`, ... (4-byte values normalized to the width/signedness of their type); constants `lit:<value>` (decimal, possibly negative after optimizer folding; a character literal is its ASCII code); struct field references `field:<STRUCT_ID>:<structName>:<FIELD_INDEX>:<fieldName>`; functions `fn:<name>`; labels `lbl:<name>`; argument ordinals `argc:<n>`. No operand names a register, a stack offset, or a byte size, which is what makes the IR target-neutral.
- **Complete 23-Opcode Table:**

| # | `OPCODE` | `DATA_TYPE` | `DEST` | `ARG1` | `ARG2` | Exact Operational & Type Semantics |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `FUNC_BEGIN` | Function return type (`int16..char` or `khali`) | `fn:<name>` | `@NONE` | `@NONE` | Opens function `<name>`; resets the function-local temporary counter to `%t0`. |
| 2 | `FUNC_END` | Function return type | `fn:<name>` | `@NONE` | `@NONE` | Closes function `<name>`. |
| 3 | `ASSIGN` | `int16..char` | `sym:<id>:<v>` or `%tK` | `sym:..`, `%tJ`, or `lit:..` | `@NONE` | Copies `ARG1` into `DEST`. |
| 4–7 | `ADD`, `SUB`, `MUL`, `DIV` | `int16`, `int32`, `uint16`, `uint32` | `%tK` (result type = `DATA_TYPE`) | Left operand (`DATA_TYPE`) | Right operand (`DATA_TYPE`) | Computes `ARG1 op ARG2` with the width and signedness of `DATA_TYPE` and stores it in `DEST`; `DIV` truncates toward zero and may fault (`#DE`). |
| 8–13 | `CMP_LT`, `CMP_GT`, `CMP_EQ`, `CMP_NE`, `CMP_LE`, `CMP_GE` | Common primitive type of `ARG1` and `ARG2` (`int16..char`) | `%tK` (result type `BOOL_COND`) | Left operand (`DATA_TYPE`) | Right operand (`DATA_TYPE`) | Compares using the signedness/width of `DATA_TYPE`; writes `1` or `0` to `DEST`. |
| 14 | `LABEL` | `@NONE` | `lbl:<name>` | `@NONE` | `@NONE` | Defines branch target `<name>`. |
| 15 | `JMP` | `@NONE` | `lbl:<name>` | `@NONE` | `@NONE` | Unconditional jump within the same function. |
| 16 | `JMP_FALSE` | `BOOL_COND` or `int16..uint32` | `lbl:<name>` | Condition (`%tK`, `sym:..`, or `lit:..`) | `@NONE` | Jumps to `<name>` if `ARG1 == 0`. |
| 17 | `FIELD_LOAD` | Field primitive type | `%tK` | `sym:<id>:<structVar>` | `field:<sId>:<s>:<fIdx>:<f>` | Reads field `ARG2` of struct variable `ARG1` into `DEST`. |
| 18 | `FIELD_STORE` | Field primitive type | `sym:<id>:<structVar>` | `field:<sId>:<s>:<fIdx>:<f>` | Source operand (`DATA_TYPE`) | Stores `ARG2` into field `ARG1` of struct variable `DEST`. |
| 19 | `PARAM` | Parameter primitive type | `@NONE` | Value operand (`DATA_TYPE`) | `argc:<i>` | Stages argument `i` (0-based) for the immediately following `CALL`/`CALL_VOID`. |
| 20 | `CALL` | Callee return type (`int16..char`) | `%tK` | `fn:<name>` | `argc:<P>` | Calls value-returning `<name>` with the `P` preceding `PARAM`s; the result goes to `DEST`. |
| 21 | `CALL_VOID` | `khali` | `@NONE` | `fn:<name>` | `argc:<P>` | Calls void procedure `<name>` with the `P` preceding `PARAM`s. |
| 22 | `RET` | Function return type (`int16..char`) | `@NONE` | Return operand | `@NONE` | Exits the function returning `ARG1`. |
| 23 | `RET_VOID` | `khali` | `@NONE` | `@NONE` | `@NONE` | Exits a `khali` function. |

#### Lowering Conventions (IR Builder)
- **Expressions** are evaluated left-to-right into fresh temporaries; leaves (`sym:`, `lit:`) are used directly as operands. A call used as an expression becomes `CALL` into a fresh temporary; a call statement of a value-returning function emits `CALL` into an unused temporary; a call statement of a `khali` function emits `CALL_VOID`.
- **`agar C shro T khtm`** lowers to: evaluate `C`; `JMP_FALSE lbl:L_ENDIF_n`; `T`; `LABEL lbl:L_ENDIF_n`. With `magar E`: `JMP_FALSE lbl:L_ELSE_n`; `T`; `JMP lbl:L_ENDIF_n`; `LABEL lbl:L_ELSE_n`; `E`; `LABEL lbl:L_ENDIF_n`.
- **`jabtak C shro B khtm`** lowers to: `LABEL lbl:L_WHILE_n`; evaluate `C`; `JMP_FALSE lbl:L_ENDWHILE_n`; `B`; `JMP lbl:L_WHILE_n`; `LABEL lbl:L_ENDWHILE_n`. (`n` is a per-file counter.)
- **`COND -> EX`** passes the integer value directly to `JMP_FALSE` (`DATA_TYPE` = its integer type); **`COND -> EX REL_OP EX`** goes through a `CMP_*` into a `BOOL_COND` temporary.
- **Calls with `P` arguments:** every argument expression is evaluated (left-to-right, innermost calls first) **before** any `PARAM` of this call is emitted; then `P` contiguous `PARAM` instructions `argc:0 .. argc:P-1` are emitted, then the `CALL`/`CALL_VOID`.

#### IR Structural & `IrReader` Validation Rules
1. **Sequential `IR_ID`s** run `0 .. @TOTAL_IR_INSTRS - 1`; each function is one `FUNC_BEGIN ... FUNC_END` block, and no instruction lies outside such a block.
2. **Single-assignment temporaries:** numbering restarts at `%t0` in each function and increases by one per new temporary; each `%tK` is the `DEST` of exactly one instruction, which precedes every use in `ARG1`/`ARG2`. (This is a property of IR as built; Optimizer 1 may delete temporaries but never redefines one.)
3. **Labels:** each `LABEL` name is unique in the file; every `JMP`/`JMP_FALSE` target is a `LABEL` inside the same function.
4. **Calls and `PARAM` grouping:** every `CALL`/`CALL_VOID` with `ARG2 == argc:<P>` is immediately preceded by exactly `P` contiguous `PARAM` instructions with `ARG2` equal to `argc:0, argc:1, ..., argc:<P-1>` in that order, with no other instruction between them; `PARAM` appears nowhere else. `ARG1 = fn:<name>` must name a function in `.sym` Section 2 whose parameter count equals `P`, and whose `RETURN_TYPE` is `khali` exactly when the opcode is `CALL_VOID`.
5. **Return consistency:** `RET` appears only in non-`khali` functions with `DATA_TYPE` equal to the function's return type; `RET_VOID` only in `khali` functions; the last instruction before `FUNC_END` is `RET` or `RET_VOID`.
6. **Operand validity:** every `sym:<id>:<name>` agrees with `.sym` Section 4 (`id` and `name` of the same symbol, in the same function); every `field:` operand agrees with Section 1; every `AST_NODE_ID` is `-1` or a node of the `.ast`.

#### Optimizer 1 (Phase 6, Option A): Permitted Transformations
Constant folding and propagation (with the width/sign-exact truncation of Section 3.4 rule 3); algebraic identities (`x + 0`, `x - 0`, `x * 1`, `x / 1`, `x * 0`); folding `JMP_FALSE` on a constant condition; unreachable-code elimination; dead-store and dead-temporary elimination of pure instructions. Pure means `ASSIGN`, `ADD`, `SUB`, `MUL`, `CMP_*`, `FIELD_LOAD`, `FIELD_STORE` to a never-read local. `DIV`, `CALL` and `CALL_VOID` are never deleted or folded unsafely (Section 3.4 rule 3). Optimizer 1 must leave `PARAM` grouping and label uniqueness valid.

- **Complete `computeHalf.ir`** (source and `.ast` in Section 5.3; `sym:0` is `base`, `sym:1` is `res`):
  ```text
  @FORMAT | ICC_IR | v1.0
  @SOURCE | computeHalf.citu
  @TOTAL_IR_INSTRS | 5
  # IR_ID | OPCODE | DATA_TYPE | DEST | ARG1 | ARG2 | AST_NODE_ID | COMMENT
  0 | FUNC_BEGIN | uint32 | fn:computeHalf | @NONE | @NONE | 1 | fn uint32 computeHalf
  1 | DIV | uint32 | %t0 | sym:0:base | lit:2 | 9 | base / 2
  2 | ASSIGN | uint32 | sym:1:res | %t0 | @NONE | 7 | res medalo base / 2
  3 | RET | uint32 | @NONE | sym:1:res | @NONE | 12 | wapis res
  4 | FUNC_END | uint32 | fn:computeHalf | @NONE | @NONE | 1 | end fn computeHalf
  @END_FILE
  ```
- **Complete `audit_module.ir` and `audit_module.opt1.ir`.** Source file `audit_module.citu` (a library module without `main`; its AST has 21 nodes: `FUNC_DEF` 1, `IF_STMT` 7, `REL_EXPR` 8, `ASSIGN_STMT` 14, `BIN_EXPR` 16, `RETURN_STMT` 19):
  ```text
  fn khali auditRecord ( int32 currentMarks ) shro
      int32 margin
      agar ( currentMarks != 100 ) shro
          margin medalo 100 - currentMarks
      khtm
      wapis khali
  khtm
  ```
  ```text
  @FORMAT | ICC_IR | v1.0
  @SOURCE | audit_module.citu
  @TOTAL_IR_INSTRS | 8
  # IR_ID | OPCODE | DATA_TYPE | DEST | ARG1 | ARG2 | AST_NODE_ID | COMMENT
  0 | FUNC_BEGIN | khali | fn:auditRecord | @NONE | @NONE | 1 | fn khali auditRecord
  1 | CMP_NE | int32 | %t0 | sym:0:currentMarks | lit:100 | 8 | currentMarks != 100 (operands int32, result BOOL_COND)
  2 | JMP_FALSE | BOOL_COND | lbl:L_ENDIF_1 | %t0 | @NONE | 7 | jump if %t0 is false
  3 | SUB | int32 | %t1 | lit:100 | sym:0:currentMarks | 16 | 100 - currentMarks
  4 | ASSIGN | int32 | sym:1:margin | %t1 | @NONE | 14 | dead store to margin
  5 | LABEL | @NONE | lbl:L_ENDIF_1 | @NONE | @NONE | 7 | end agar block
  6 | RET_VOID | khali | @NONE | @NONE | @NONE | 19 | wapis khali
  7 | FUNC_END | khali | fn:auditRecord | @NONE | @NONE | 1 | end fn auditRecord
  @END_FILE
  ```
  After Optimizer 1: `margin` is written (instruction 4) and never read, so the dead `ASSIGN` and its pure temporary computation `SUB` are removed; `%t0` then only feeds a branch whose both outcomes are empty, so the pure `CMP_NE`, the `JMP_FALSE` and the `LABEL` are removed too. The result is semantically identical (no observable effect was removed):
  ```text
  @FORMAT | ICC_OPT1_IR | v1.0
  @SOURCE | audit_module.citu
  @TOTAL_IR_INSTRS | 3
  # IR_ID | OPCODE | DATA_TYPE | DEST | ARG1 | ARG2 | AST_NODE_ID | COMMENT
  0 | FUNC_BEGIN | khali | fn:auditRecord | @NONE | @NONE | 1 | fn khali auditRecord
  1 | RET_VOID | khali | @NONE | @NONE | @NONE | 19 | wapis khali
  2 | FUNC_END | khali | fn:auditRecord | @NONE | @NONE | 1 | end fn auditRecord
  @END_FILE
  ```

---

### 5.6 Formats 5–7: 32-Bit x86 NASM Assembly Files & `AsmReader` Validation

Assembly stage files and their tags: `.cg1.asm` (`ICC_CG1_ASM`), `.cg2.asm` (`ICC_CG2_ASM`), `.opt1.asm` (`ICC_OPT1_ASM`, **Option B only**), `.opt2.asm` (`ICC_OPT2_ASM`), `.linked.asm` (`ICC_LINKED_ASM`, Section 5.7). All use the same record and header schemas below. Under Option B, `.opt1.asm` is produced by Optimizer 1 from `.cg2.asm` and consumed by Optimizer 2; its transformations are limited to those that do not rely on x86 specifics (redundant store/load forwarding, dead-store elimination of `[ebp - k]` slots that are never read according to `; @MEM_SYM`, constant propagation through `mov` immediates, folding of constant arithmetic with the width/sign rules of Section 3.4). No example file is shown because it has the format of `.cg2.asm` and equals it when no such opportunity exists.

#### A. ICC 32-Bit x86 Calling Convention (ABI), Register Classes & Type Handling
1. **Caller-saved scratch registers (`EAX`, `ECX`, `EDX`):** `EAX` (`AX`, `AL`) is the accumulator and the **function return value register**; `ECX` (`CX`, `CL`) is the second operand and divisor; `EDX` (`DX`) is the high half for division (`cdq`, `cwd`, `xor edx, edx`). **Return values are normalized to 32 bits in `EAX`:** `int16` is sign-extended, `uint16` and `char` are zero-extended.
2. **Callee-saved registers (`EBX`, `ESI`, `EDI`):** CodeGen 1 and 2 never use them. If Phase 7 promotes a value into one, the function must `push` it in the prologue (after `sub esp, N`) and restore it in the epilogue before `mov esp, ebp` is replaced accordingly. `ESP` and `EBP` are reserved for frame management and are never scratch or promotion registers.
3. **Arguments & stack cleanup (cdecl-style):** arguments are pushed right-to-left, each as a 4-byte normalized value; parameter $i$ (0-based) is at `[ebp + 8 + 4*i]`; the **caller** removes the arguments after the call with `add esp, 4*P` (omitted when $P = 0$). (The Win32 API call in `_start` is `stdcall`: its callee removes its own argument, Section 5.7.)
4. **Frame layout:** prologue `push ebp` / `mov ebp, esp` / `sub esp, N` (omitted when `N = 0`); epilogue `mov esp, ebp` / `pop ebp` / `ret`. `N` is the `STACK_FRAME_BYTES` of `; @MEM_FUNC`. All locals of a function (across every nested scope, which is possible because names are function-wide unique) and, in `.cg1.asm`, all IR temporaries get dedicated 4-byte slots below `ebp`: symbols in `SYM_ID` order, then temporaries in `%t` order. A struct variable occupies `totalSlotBytes` contiguous bytes; its `EBP_OFFSET` is the address of field 0, and field $f$ lives at `EBP_OFFSET + slotOffset_f`.
5. **Loads and stores by type** (slot or field address written `[m]`): `int32`/`uint32` `mov eax, dword [m]` / `mov dword [m], eax`; `int16` `movsx eax, word [m]` / `mov word [m], ax`; `uint16` `movzx eax, word [m]` / `mov word [m], ax`; `char` `movzx eax, byte [m]` / `mov byte [m], al`. Temporaries are always stored and loaded as full `dword`s.
6. **Arithmetic & comparison by type:** 32-bit types use `add`/`sub`/`imul eax, ecx` and the division sequences of Section 3.4 rule 3; 16-bit types use `ax`/`cx` forms (`add ax, cx`, `sub ax, cx`, `imul ax, cx`) followed by `cwde` (`int16`) or `movzx eax, ax` (`uint16`). Comparisons first load both operands into `eax`/`ecx` (already 32-bit normalized), then `cmp eax, ecx`, then `setcc al` / `movzx eax, al`: signed types (`int16`, `int32`) use `setl`/`setg`/`setle`/`setge`, unsigned types and `char` use `setb`/`seta`/`setbe`/`setae`, equality uses `sete`/`setne`. `JMP_FALSE` is `cmp eax, 0` / `je <label>`.

#### B. Counted Record Schemas and Memory-Map Header Lines
- **Uncounted header lines (schemas):**
  `; @FORMAT | <TAG> | v1.0` · `; @SOURCE | <source>` · `; @TOTAL_RECORDS | <M>`
  `; @MEM_STRUCT | STRUCT_NAME | TOTAL_SLOT_BYTES | FIELD_LAYOUT` (field layout is Class C, e.g. `width:0:4,height:4:4`)
  `; @MEM_FUNC | FUNC_NAME | STACK_FRAME_BYTES`
  `; @MEM_SYM | SYM_ID | IDENT_NAME | ENCLOSING_FUNC | ARCH_BYTE_SIZE | EBP_OFFSET`
  `; @MEM_TEMP | TEMP_NAME | ENCLOSING_FUNC | ARCH_BYTE_SIZE | EBP_OFFSET` (present only while IR temporaries have stack slots, i.e. in `.cg1.asm`)
  `; @END_FILE`
- **Counted payload lines (`REC_ID` `0 .. M-1`):**
  1. `<nasm_directive> ; @DIR | REC_ID | DIR_KIND | SYM_NAME | COMMENT` (5 pipe-separated fields)
  2. `<label_name>: ; @LBL | REC_ID | LABEL_NAME | LABEL_KIND | ENCLOSING_FUNC | AST_NODE_ID | COMMENT` (7 fields)
  3. `<nasm_instruction> ; @INS | REC_ID | OPCODE | OP1 | OP2 | DATA_TYPE | AST_NODE_ID | COMMENT` (8 fields)

  In records, `DATA_TYPE` is the operand type of an instruction (`@NONE` when meaningless; for `call` it is the callee's return type).

#### C. `AsmReader` Validation Protocol
1. **Counts and IDs:** `REC_ID` runs contiguously from `0` to `@TOTAL_RECORDS - 1`; the file ends with `; @END_FILE`; the format tag is valid.
2. **Record shapes:** each counted line has exactly the field count of its kind (`@DIR` 5, `@LBL` 7, `@INS` 8).
3. **`DIR_KIND`:** `SECTION` (`SYM_NAME == ".text"`), `GLOBAL` (a defined function label or `_start`), or `EXTERN` (an external function or `_ExitProcess@4`).
4. **`AST_NODE_ID`:** `-1` (synthetic records such as `_start`) or a non-negative node ID. For stage files of one module (`.cg1`, `.cg2`, `.opt1`, `.opt2`) it must be a node of that module's `.ast`; in `.linked.asm`, where IDs of several modules coexist, only the range `>= -1` is checked.
5. **Labels and targets:** `LABEL_NAME` is unique in the file; `LABEL_KIND` is `FUNC_ENTRY` or `BRANCH_TARGET`. Every conditional or unconditional jump (`je`, `jne`, `jl`, `jg`, `jle`, `jge`, `jb`, `ja`, `jbe`, `jae`, `jmp`) has an `OP1` naming a `BRANCH_TARGET` label with the same `ENCLOSING_FUNC`. Every `call` has an `OP1` naming a `FUNC_ENTRY` label in the file or a symbol declared by an `EXTERN` directive.
6. **NASM-text consistency:** the Reader rebuilds the canonical text from the metadata (`section <SYM_NAME>`, `global <SYM_NAME>`, `extern <SYM_NAME>`; `<LABEL_NAME>:`; `<OPCODE>`, `<OPCODE> <OP1>`, or `<OPCODE> <OP1>, <OP2>` for instructions, omitting operands equal to `@NONE`) and requires it to equal the left-hand NASM text after trimming trailing spaces (the column padding before `; @` is not significant).
7. **Memory-map validation:** `STACK_FRAME_BYTES` is a non-negative multiple of `4`. For `; @MEM_SYM`/`; @MEM_TEMP`: `ARCH_BYTE_SIZE` $\in \{1, 2, 4\}$ (a positive multiple of `4` for struct variables, equal to the struct's `TOTAL_SLOT_BYTES`), and `EBP_OFFSET` is a multiple of `4`: $\ge +8$ for parameters; for locals and temporaries $\le -4$, $\ge -\text{STACK\_FRAME\_BYTES}$, and `EBP_OFFSET + slot size` $\le 0$. No two slots of one function overlap.

#### D. CodeGen 1 Translation Templates (Option A: one IR instruction, one fixed template)
`FUNC_BEGIN`: `global`, entry label, prologue. `ASSIGN`: load `ARG1` into `eax` (by type), store to `DEST`. `ADD/SUB/MUL`: load `ARG1`$\rightarrow$`eax`, `ARG2`$\rightarrow$`ecx`, operate (Section 5.6-A6), store the result to the temporary slot of `DEST`. `DIV`: load as above, extend and divide (Section 3.4 rule 3), store. `CMP_*`: load, `cmp eax, ecx`, `setcc al`, `movzx eax, al`, store. `JMP_FALSE`: load, `cmp eax, 0`, `je`. `FIELD_LOAD`/`FIELD_STORE`: address `[ebp + EBP_OFFSET + slotOffset]`, loaded or stored by field type. `PARAM...CALL`: CodeGen 1 reads the contiguous `PARAM` group, then for $i = P-1$ down to $0$ loads operand $i$ into `eax` and `push eax`; then `call <fn>`; then `add esp, 4*P`; for `CALL` it stores `eax` into the temporary slot of `DEST`. `RET`: load into `eax`, epilogue. `RET_VOID`: epilogue. A literal operand is loaded with `mov eax, <value>`. (Under Option B, CodeGen 1 may instead walk the AST with an evaluation stack, because tree evaluation is LIFO; the file format is identical.)

#### E. Phase 5 and Phase 7 Rules
- **CodeGen 2** replaces the temporary-slot round trips of CodeGen 1 by keeping a value in its register when its single consumer is the next instruction, removes the corresponding `; @MEM_TEMP` lines and `sub esp` bytes, and rewrites `; @MEM_FUNC` accordingly.
- **Optimizer 2** (x86-specific): unsigned multiplication or division by $2^k$ $\rightarrow$ `shl`/`shr` by $k$ (`uint32` on `eax`, `uint16` on `ax`); signed multiplication by $2^k$ $\rightarrow$ `shl`; `mov reg, 0` $\rightarrow$ `xor reg, reg` (only where flags are not live); forwarding of a 32-bit `mov [m], r` immediately followed by `mov r, [m]` (the reload is deleted; for `char`/16-bit slots the reload is kept because it re-normalizes the value); loop-counter register promotion (callee-saved registers must be saved and restored). **Signed division by a power of two is never replaced by `sar`**, because arithmetic shift rounds toward negative infinity whereas `idiv` truncates toward zero.

#### F. Complete `computeHalf` Assembly Chain (Option A; source and IR above)
`computeHalf.citu` has `base` at `[ebp + 8]`, local `res` at `[ebp - 4]`; in `.cg1.asm` the temporary `%t0` has the extra slot `[ebp - 8]`, so `N = 8`.

- **1. `computeHalf.cg1.asm` (17 counted records `0..16`):**
  ```nasm
  ; @FORMAT | ICC_CG1_ASM | v1.0
  ; @SOURCE | computeHalf.citu
  ; @TOTAL_RECORDS | 17
  ; @MEM_FUNC | computeHalf | 8
  ; @MEM_SYM | 0 | base | computeHalf | 4 | +8
  ; @MEM_SYM | 1 | res | computeHalf | 4 | -4
  ; @MEM_TEMP | %t0 | computeHalf | 4 | -8
  section .text                  ; @DIR | 0 | SECTION | .text | code section
  global computeHalf             ; @DIR | 1 | GLOBAL | computeHalf | export symbol
  computeHalf:                   ; @LBL | 2 | computeHalf | FUNC_ENTRY | computeHalf | 1 | entry label
  push ebp                       ; @INS | 3 | push | ebp | @NONE | @NONE | 1 | save ebp
  mov ebp, esp                   ; @INS | 4 | mov | ebp | esp | @NONE | 1 | set frame pointer
  sub esp, 8                     ; @INS | 5 | sub | esp | 8 | @NONE | 1 | allocate res and %t0
  mov eax, dword [ebp + 8]       ; @INS | 6 | mov | eax | dword [ebp + 8] | uint32 | 9 | load base
  mov ecx, 2                     ; @INS | 7 | mov | ecx | 2 | uint32 | 9 | load divisor 2
  xor edx, edx                   ; @INS | 8 | xor | edx | edx | uint32 | 9 | zero upper 32 bits
  div ecx                        ; @INS | 9 | div | ecx | @NONE | uint32 | 9 | unsigned divide edx:eax by ecx
  mov dword [ebp - 8], eax       ; @INS | 10 | mov | dword [ebp - 8] | eax | uint32 | 9 | store quotient in %t0
  mov eax, dword [ebp - 8]       ; @INS | 11 | mov | eax | dword [ebp - 8] | uint32 | 7 | load %t0
  mov dword [ebp - 4], eax       ; @INS | 12 | mov | dword [ebp - 4] | eax | uint32 | 7 | store into res
  mov eax, dword [ebp - 4]       ; @INS | 13 | mov | eax | dword [ebp - 4] | uint32 | 12 | load return value res
  mov esp, ebp                   ; @INS | 14 | mov | esp | ebp | @NONE | 12 | restore esp
  pop ebp                        ; @INS | 15 | pop | ebp | @NONE | @NONE | 12 | restore ebp
  ret                            ; @INS | 16 | ret | @NONE | @NONE | uint32 | 12 | return to caller
  ; @END_FILE
  ```

- **2. `computeHalf.cg2.asm` (15 counted records `0..14`; the `%t0` slot is gone):**
  ```nasm
  ; @FORMAT | ICC_CG2_ASM | v1.0
  ; @SOURCE | computeHalf.citu
  ; @TOTAL_RECORDS | 15
  ; @MEM_FUNC | computeHalf | 4
  ; @MEM_SYM | 0 | base | computeHalf | 4 | +8
  ; @MEM_SYM | 1 | res | computeHalf | 4 | -4
  section .text                  ; @DIR | 0 | SECTION | .text | code section
  global computeHalf             ; @DIR | 1 | GLOBAL | computeHalf | export symbol
  computeHalf:                   ; @LBL | 2 | computeHalf | FUNC_ENTRY | computeHalf | 1 | entry label
  push ebp                       ; @INS | 3 | push | ebp | @NONE | @NONE | 1 | save ebp
  mov ebp, esp                   ; @INS | 4 | mov | ebp | esp | @NONE | 1 | set frame pointer
  sub esp, 4                     ; @INS | 5 | sub | esp | 4 | @NONE | 1 | allocate res
  mov eax, dword [ebp + 8]       ; @INS | 6 | mov | eax | dword [ebp + 8] | uint32 | 9 | load base
  mov ecx, 2                     ; @INS | 7 | mov | ecx | 2 | uint32 | 9 | load divisor 2
  xor edx, edx                   ; @INS | 8 | xor | edx | edx | uint32 | 9 | zero upper 32 bits
  div ecx                        ; @INS | 9 | div | ecx | @NONE | uint32 | 9 | unsigned divide edx:eax by ecx
  mov dword [ebp - 4], eax       ; @INS | 10 | mov | dword [ebp - 4] | eax | uint32 | 7 | store quotient into res
  mov eax, dword [ebp - 4]       ; @INS | 11 | mov | eax | dword [ebp - 4] | uint32 | 12 | load return value res
  mov esp, ebp                   ; @INS | 12 | mov | esp | ebp | @NONE | 12 | restore esp
  pop ebp                        ; @INS | 13 | pop | ebp | @NONE | @NONE | 12 | restore ebp
  ret                            ; @INS | 14 | ret | @NONE | @NONE | uint32 | 12 | return to caller
  ; @END_FILE
  ```

- **3. `computeHalf.opt2.asm` (12 counted records `0..11`; `shr` replaces the unsigned division and the redundant reload is forwarded):**
  ```nasm
  ; @FORMAT | ICC_OPT2_ASM | v1.0
  ; @SOURCE | computeHalf.citu
  ; @TOTAL_RECORDS | 12
  ; @MEM_FUNC | computeHalf | 4
  ; @MEM_SYM | 0 | base | computeHalf | 4 | +8
  ; @MEM_SYM | 1 | res | computeHalf | 4 | -4
  section .text                  ; @DIR | 0 | SECTION | .text | code section
  global computeHalf             ; @DIR | 1 | GLOBAL | computeHalf | export symbol
  computeHalf:                   ; @LBL | 2 | computeHalf | FUNC_ENTRY | computeHalf | 1 | entry label
  push ebp                       ; @INS | 3 | push | ebp | @NONE | @NONE | 1 | save ebp
  mov ebp, esp                   ; @INS | 4 | mov | ebp | esp | @NONE | 1 | set frame pointer
  sub esp, 4                     ; @INS | 5 | sub | esp | 4 | @NONE | 1 | allocate res
  mov eax, dword [ebp + 8]       ; @INS | 6 | mov | eax | dword [ebp + 8] | uint32 | 9 | load base
  shr eax, 1                     ; @INS | 7 | shr | eax | 1 | uint32 | 9 | unsigned divide by 2 via shift
  mov dword [ebp - 4], eax       ; @INS | 8 | mov | dword [ebp - 4] | eax | uint32 | 7 | store into res; eax already holds the return value
  mov esp, ebp                   ; @INS | 9 | mov | esp | ebp | @NONE | 12 | restore esp
  pop ebp                        ; @INS | 10 | pop | ebp | @NONE | @NONE | 12 | restore ebp
  ret                            ; @INS | 11 | ret | @NONE | @NONE | uint32 | 12 | return to caller
  ; @END_FILE
  ```

---

### 5.7 Format 8 & Phase 8: Multi-File Merging (`.linked.asm`), Win32 Object (`.obj`), PE32 Executable (`.exe`)

#### A. Phase 8 Artifacts
1. **`<out>.linked.asm`:** a human-readable merged NASM source in the `ICC_LINKED_ASM v1.0` structured format (not machine code). Its `@SOURCE` value is the comma-separated list of the input `.citu` file names in command-line order.
2. **`<out>.obj`:** a Win32 COFF relocatable object produced by `nasm -f win32 <out>.linked.asm -o <out>.obj`.
3. **`<out>.exe`:** the Windows PE32 executable produced by a Windows-targeting linker, importing `_ExitProcess@4` from `kernel32.dll`. Whether an external assembler and linker may be used is instructor-dependent decision C2 (Section 1.3).

#### B. Deterministic Merging Rules (`<prog_0>.opt2.asm ... <prog_{K-1}>.opt2.asm` $\rightarrow$ `<out>.linked.asm`)
For $K$ input modules $M_0, \dots, M_{K-1}$ in command-line order:
1. **Cross-file verification:** from all `.sym` files, check that exactly one `IS_EXTERNAL == 0` definition of `main` with signature `fn int32 main ( )` exists (`LINKER_ERROR_MISSING_MAIN`, `LINKER_ERROR_MULTIPLE_MAIN`), that no function or struct has two `IS_EXTERNAL == 0` definitions (`LINKER_ERROR_DUPLICATE_DEFINITION`), and that each `IS_EXTERNAL == 1` import resolves to a definition with identical signature (`LINKER_ERROR_UNRESOLVED_EXTERNAL`, `LINKER_ERROR_SIGNATURE_MISMATCH`).
2. **Memory-map consolidation:** deduplicate `; @MEM_STRUCT` by `STRUCT_NAME`; copy all `; @MEM_FUNC` lines; copy all `; @MEM_SYM` lines, renumbering `SYM_ID` sequentially from `0` while keeping `ENCLOSING_FUNC` and `EBP_OFFSET`.
3. **Directive consolidation:** emit one `section .text` as record `0`, then `extern _ExitProcess@4` and `global _start`; keep `GLOBAL` directives for every defined Citu function; **drop** the `EXTERN` directives of Citu functions that are defined in another input module (they are now defined in the merged file).
4. **Labels:** `FUNC_ENTRY` labels keep their exact function name so `call <name>` resolves across modules. Every `BRANCH_TARGET` label of module $M_m$ is prefixed with `M<m>_` (`L_ENDIF_1` $\rightarrow$ `M0_L_ENDIF_1`) in its `; @LBL` record **and** in every jump of $M_m$ that references it, in both the metadata columns and the NASM text. (User function names cannot collide with these prefixes or with `_start`: Section 3.4 rule 5.)
5. **Process entry and termination (`_start`):** the Linker appends four records at the end:
   1. `_start:` (`LABEL_KIND = FUNC_ENTRY`, `ENCLOSING_FUNC = _start`, `AST_NODE_ID = -1`)
   2. `call main` (the 32-bit result is returned in `eax`)
   3. `push eax` (the `UINT uExitCode` argument)
   4. `call _ExitProcess@4` (Win32 `stdcall`: the callee removes its own argument, so no `add esp` follows, and the call never returns)
6. **Renumbering:** all records are renumbered contiguously from `0`, and `@TOTAL_RECORDS` is written, so the linked file is itself a valid structured assembly file passing `AsmReader`.

#### C. Complete `min_return.linked.asm` (15 counted records `0..14`)
Built from `min_return.citu` (Section 5.2); `main` has no locals, so its frame size is `0` and no `sub esp` is emitted.
```nasm
; @FORMAT | ICC_LINKED_ASM | v1.0
; @SOURCE | min_return.citu
; @TOTAL_RECORDS | 15
; @MEM_FUNC | main | 0
section .text                  ; @DIR | 0 | SECTION | .text | merged code section
extern _ExitProcess@4          ; @DIR | 1 | EXTERN | _ExitProcess@4 | Win32 kernel32 process exit API
global _start                  ; @DIR | 2 | GLOBAL | _start | PE32 entry point symbol
global main                    ; @DIR | 3 | GLOBAL | main | Citu main function
main:                          ; @LBL | 4 | main | FUNC_ENTRY | main | 1 | main entry point
push ebp                       ; @INS | 5 | push | ebp | @NONE | @NONE | 1 | save ebp
mov ebp, esp                   ; @INS | 6 | mov | ebp | esp | @NONE | 1 | set frame pointer
mov eax, 0                     ; @INS | 7 | mov | eax | 0 | int32 | 5 | return literal 0
mov esp, ebp                   ; @INS | 8 | mov | esp | ebp | @NONE | 5 | restore esp
pop ebp                        ; @INS | 9 | pop | ebp | @NONE | @NONE | 5 | restore ebp
ret                            ; @INS | 10 | ret | @NONE | @NONE | int32 | 5 | return to _start
_start:                        ; @LBL | 11 | _start | FUNC_ENTRY | _start | -1 | Windows PE32 entry point
call main                      ; @INS | 12 | call | main | @NONE | int32 | -1 | invoke Citu main
push eax                       ; @INS | 13 | push | eax | @NONE | int32 | -1 | pass eax as uExitCode
call _ExitProcess@4            ; @INS | 14 | call | _ExitProcess@4 | @NONE | khali | -1 | terminate the Win32 process
; @END_FILE
```

#### D. Windows Toolchain Commands (status: not executed during this audit)
A Windows PE32 executable needs a **Windows-targeting 32-bit toolchain**; a Linux `gcc -m32` produces ELF, not PE32. With NASM and MinGW-w64 installed:
```powershell
# Step 1: assemble the merged source into a 32-bit Windows COFF object
nasm -f win32 app.linked.asm -o app.obj

# Step 2: link against kernel32 with the entry point _start (no C runtime)
i686-w64-mingw32-ld -mi386pe app.obj -e _start -lkernel32 -o app.exe
# Alternative via the GCC driver without the C start-up files:
# i686-w64-mingw32-gcc -m32 -nostdlib -Wl,-e,_start app.obj -lkernel32 -o app.exe
```
If the linker cannot find `libkernel32.a`, add `-L<mingw-w64 i686 lib directory>`. The sequence follows the documented NASM `win32` format and MinGW-w64 import-library behaviour, but because the toolchain was not available during this audit, it must be run once on the submission machine before it is described as working. The exit code of `app.exe` is the value returned by `main`.

---

## 6. Requirement 5: Git Repository & Directory Structure

### 6.1 Repository & Submission Metadata
- **Student ID:** `<YOUR_STUDENT_ID>`
- **Git Repository URL:** `<YOUR_GIT_REPO_URL>`
- **Submission Commit Hash:** `<YOUR_COMMIT_HASH>`
- **Required Collaborators:**
  - `umarsuleman@gmail.com` (Instructor)
  - `mscs24052@itu.edu.pk` (Teaching Assistant)

### 6.2 Planned Repository Layout
The layout below is the intended structure; the committed repository must match it (or this section must be edited to match the repository) before submission.
```text
icc-compiler/
├── .gitignore                          # Excludes compiled binaries (*.exe, *.obj, *.o) from version control
├── Makefile                            # Build configuration for compiling icc modules with g++
├── README.md                           # Project overview, build instructions, and CLI usage guide
├── doc/
│   └── Citu_Phase1_Specification.md    # Complete Phase 1 Technical Specification
├── examples/
│   ├── prog1_geometry.citu             # Sample Program 1 (Structs, functions, loops, if-else)
│   ├── prog2_payroll.citu              # Sample Program 2 (16/32-bit types, single '=', nested scopes)
│   └── prog3_grades.citu               # Sample Program 3 (Unsigned char, khali procedures, relational ops)
├── tests/                              # Positive and negative test cases for lexical, syntax, and semantic checks
├── include/                            # C++ Header files (.h) for Token, ASTNode, SymbolTable, IR, AsmRecord
├── src/                                # C++ Source files (.cpp) for each compiler phase
├── lib/                                # Reader/Writer serializer modules
└── bin/                                # Compiled binaries (icc.exe)
```
