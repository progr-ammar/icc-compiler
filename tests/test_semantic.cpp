// Phase 3 Stage 3B: SemanticAnalyzer test suite (Specification v1.4, Sections 3.4, 3.6 & 5.4).
//
// Every case runs the file-decoupled pipeline on disk:
//   source -> Lexer -> LexWriter -> .lex -> Parser::parseFromLexFile -> AstWriter -> .ast
//   -> SemanticAnalyzer::analyze(.ast, .lex, imported .sym files) -> writeSym -> .sym
// Scratch files live in build/test_semantic/ (run from the repository root).

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define ICC_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define ICC_MKDIR(path) mkdir(path, 0755)
#endif

#include "ast.h"
#include "ast_reader.h"
#include "ast_writer.h"
#include "lex_writer.h"
#include "lexer.h"
#include "parser.h"
#include "semantic_analyzer.h"
#include "sym.h"
#include "sym_reader.h"
#include "sym_test_support.h"

using namespace symtest;

namespace {

const std::string kDir = "build/test_semantic";

struct Unit {
    bool frontEndOk = false;
    bool ok = false;
    std::string code;
    std::string message;
    int line = 0;
    std::string symPath;
    std::vector<ASTNode> ast;
    SymbolTable table;
};

std::string pathOf(const std::string& stem, const std::string& ext) {
    return kDir + "/" + stem + ext;
}

// Runs the full pipeline for `source` named "<stem>.citu"; writes <stem>.sym on success.
Unit compileUnit(TestContext& ctx, const std::string& source, const std::string& stem,
                 const std::vector<std::string>& imports = {}) {
    Unit u;
    const std::string lexPath = pathOf(stem, ".lex");
    const std::string astPath = pathOf(stem, ".ast");
    u.symPath = pathOf(stem, ".sym");
    std::remove(lexPath.c_str());
    std::remove(astPath.c_str());
    std::remove(u.symPath.c_str());

    const std::string name = stem + ".citu";
    Lexer lexer(source, name);
    if (!lexer.scanTokens()) {
        ctx.fail(stem + ": fixture must lex: " + lexer.getErrorMessage());
        return u;
    }
    LexWriter lw;
    Parser parser;
    AstWriter aw;
    if (!lw.write(lexPath, name, lexer.getTokens()) || !parser.parseFromLexFile(lexPath) ||
        !aw.write(astPath, parser.getSourceFilename(), parser.getAstNodes())) {
        ctx.fail(stem + ": fixture must parse: " + parser.getErrorMessage());
        return u;
    }
    AstReader ar;
    ctx.checkTrue(ar.read(astPath), stem + ": .ast readable");
    u.ast = ar.getAstNodes();
    u.frontEndOk = true;

    SemanticAnalyzer sa;
    u.ok = sa.analyze(astPath, lexPath, imports);
    u.code = sa.getErrorCode();
    u.message = sa.getErrorMessage();
    u.line = sa.getErrorLine();
    if (u.ok) {
        u.table = sa.getSymbolTable();
        ctx.checkTrue(sa.writeSym(u.symPath), stem + ": writeSym (" + sa.getErrorMessage() + ")");
    } else {
        ctx.checkTrue(sa.getSymbolTable().nodeTypes.empty() && sa.getSymbolTable().functions.empty(),
                      stem + ": failed analysis clears the symbol table");
    }
    return u;
}

Unit expectOk(TestContext& ctx, const std::string& source, const std::string& stem,
              const std::vector<std::string>& imports = {}) {
    Unit u = compileUnit(ctx, source, stem, imports);
    if (u.frontEndOk) {
        ctx.checkTrue(u.ok, stem + ": analysis must succeed (" + u.message + ")");
    }
    return u;
}

void expectError(TestContext& ctx, const std::string& source, const std::string& code, int line,
                 const std::string& stem, const std::vector<std::string>& imports = {}) {
    const Unit u = compileUnit(ctx, source, stem, imports);
    if (!u.frontEndOk) {
        return;
    }
    ctx.checkTrue(!u.ok, stem + ": analysis must fail with " + code);
    ctx.checkEqual(u.code, code, stem + ": error code (" + u.message + ")");
    ctx.checkEqual(u.line, line, stem + ": error line (" + u.message + ")");
    ctx.checkTrue(u.message.find(code) != std::string::npos, stem + ": message names the code");
    ctx.checkTrue(!fileExists(u.symPath), stem + ": no .sym written for a failed analysis");
}

// n-th node of a kind (pre-order), optionally with a given lexeme; -1 if absent
int findNode(const Unit& u, const std::string& kind, std::size_t nth = 0, const std::string& lexeme = "") {
    std::size_t seen = 0;
    for (const ASTNode& n : u.ast) {
        if (n.nodeKind == kind && (lexeme.empty() || n.lexeme == lexeme)) {
            if (seen++ == nth) {
                return n.nodeId;
            }
        }
    }
    return -1;
}

void checkAnn(TestContext& ctx, const Unit& u, int nodeId, const std::string& type, const std::string& ref,
              int rid, int sub, bool isConst, long long folded, const std::string& label) {
    if (nodeId < 0 || static_cast<std::size_t>(nodeId) >= u.table.nodeTypes.size()) {
        ctx.fail(label + ": node not found / no annotation");
        return;
    }
    const NodeTypeAnnotation& a = u.table.nodeTypes[static_cast<std::size_t>(nodeId)];
    ctx.checkEqual(a.evaluatedType, type, label + ": EVALUATED_TYPE");
    ctx.checkEqual(a.refKind, ref, label + ": REF_KIND");
    ctx.checkEqual(a.resolvedId, rid, label + ": RESOLVED_ID");
    ctx.checkEqual(a.resolvedSubId, sub, label + ": RESOLVED_SUB_ID");
    ctx.checkEqual(a.isConstExpr, isConst, label + ": IS_CONST_EXPR");
    ctx.checkEqual(a.foldedConstVal, folded, label + ": FOLDED_CONST_VAL");
}

std::string readSource(TestContext& ctx, const std::string& path) {
    std::string s;
    ctx.checkTrue(loadFileBinary(path, s), "read " + path + " (run from repo root)");
    return s;
}

// Wraps statements into "fn int32 main ( ) shro <decls/stmts> wapis 0 khtm" on line 1
std::string mainWith(const std::string& body) {
    return "fn int32 main ( ) shro\n" + body + "wapis 0\nkhtm\n";
}

// ----------------------------------------------------------------------------
// S1: end-to-end golden min_return.sym (Section 5.4) and computeHalf symbol IDs (Section 5.5)
// ----------------------------------------------------------------------------
void testS1_GoldenMinReturn(TestContext& ctx) {
    const Unit u = expectOk(ctx, readSource(ctx, "tests/fixtures/min_return.citu"), "min_return");
    std::string produced;
    std::string golden;
    ctx.checkTrue(loadFileBinary(u.symPath, produced), "S1: min_return.sym written");
    ctx.checkTrue(loadFileBinary("tests/golden/min_return.sym", golden), "S1: golden readable");
    ctx.checkEqual(produced, golden, "S1: min_return.sym byte-for-byte equal to Spec v1.4 Sec 5.4");

    // Section 5.5 computeHalf.ir names sym:0:base and sym:1:res
    const Unit h = expectOk(ctx, readSource(ctx, "tests/fixtures/computeHalf.citu"), "computeHalf");
    if (h.table.symbols.size() == 2) {
        ctx.checkEqual(h.table.symbols[0].identName, std::string("base"), "S1: sym 0 is base");
        ctx.checkEqual(h.table.symbols[0].symKind, std::string("PARAM"), "S1: base is a PARAM");
        ctx.checkEqual(h.table.symbols[1].identName, std::string("res"), "S1: sym 1 is res");
        ctx.checkEqual(h.table.symbols[1].symKind, std::string("LOCAL_VAR"), "S1: res is a LOCAL_VAR");
    } else {
        ctx.fail("S1: computeHalf must have 2 symbols");
    }
    // Section 5.3 computeHalf.ast: node 9 is 'base / 2', node 11 the literal 2
    checkAnn(ctx, h, 9, "uint32", "@NONE", -1, -1, false, -1, "S1 computeHalf node 9 (base / 2)");
    checkAnn(ctx, h, 11, "uint32", "@NONE", -1, -1, true, 2, "S1 computeHalf node 11 (2 adapts to uint32)");
    checkAnn(ctx, h, 10, "uint32", "LOCAL_SYM", 0, -1, false, -1, "S1 computeHalf node 10 (base)");
    checkAnn(ctx, h, 8, "uint32", "LOCAL_SYM", 1, -1, false, -1, "S1 computeHalf node 8 (res)");
}

// ----------------------------------------------------------------------------
// S2: the three specification sample programs
// ----------------------------------------------------------------------------
void testS2_SamplePrograms(TestContext& ctx) {
    struct Expect { const char* path; const char* stem; std::size_t structs, funcs, scopes, syms, nodes; };
    const Expect samples[3] = {
        {"examples/prog1_geometry.citu", "prog1_geometry", 1, 3, 6, 9, 84},
        {"examples/prog2_payroll.citu", "prog2_payroll", 1, 2, 7, 7, 104},
        {"examples/prog3_grades.citu", "prog3_grades", 1, 3, 9, 7, 107}
    };
    std::vector<Unit> units;
    for (const Expect& e : samples) {
        const Unit u = expectOk(ctx, readSource(ctx, e.path), e.stem);
        ctx.checkEqual(u.table.structs.size(), e.structs, std::string(e.stem) + ": structs");
        ctx.checkEqual(u.table.functions.size(), e.funcs, std::string(e.stem) + ": functions");
        ctx.checkEqual(u.table.scopes.size(), e.scopes, std::string(e.stem) + ": scopes");
        ctx.checkEqual(u.table.symbols.size(), e.syms, std::string(e.stem) + ": symbols");
        ctx.checkEqual(u.table.nodeTypes.size(), e.nodes, std::string(e.stem) + ": NODE_TYPES == @TOTAL_NODES");
        SymReader r;
        ctx.checkTrue(r.read(u.symPath, static_cast<int>(e.nodes)),
                      std::string(e.stem) + ": .sym passes SymReader with @TOTAL_NODES (" + r.getErrorMessage() + ")");
        units.push_back(u);
    }
    if (units.size() != 3) {
        return;
    }
    const Unit& p1 = units[0];
    const Unit& p2 = units[1];
    const Unit& p3 = units[2];

    // prog1: call resolves to calcArea (funcId 0); field access myBox.width -> FIELD_ACCESS_REF
    checkAnn(ctx, p1, findNode(p1, "FUNC_CALL_EXPR", 0, "calcArea"), "int32", "FUNC_REF", 0, -1, false, -1, "S2 prog1 calcArea()");
    checkAnn(ctx, p1, findNode(p1, "FIELD_ACCESS", 0, "width"), "int32", "FIELD_ACCESS_REF", 6, 0, false, -1, "S2 prog1 myBox.width");
    checkAnn(ctx, p1, findNode(p1, "STRUCT_VAR_DECL"), "struct Box", "LOCAL_SYM", 6, -1, false, -1, "S2 prog1 struct Box myBox");

    // prog2: INT_LITERAL adapts to its partner type (Section 2.2 type audit)
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 0, "101"), "uint16", "@NONE", -1, -1, true, 101, "S2 prog2 emp.id medalo 101");
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 1, "101"), "uint16", "@NONE", -1, -1, true, 101, "S2 prog2 emp.id = 101");
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 0, "4000"), "uint32", "@NONE", -1, -1, true, 4000, "S2 prog2 4000");
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 0, "1"), "int16", "@NONE", -1, -1, true, 1, "S2 prog2 tier = 1");
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 0, "2"), "uint32", "@NONE", -1, -1, true, 2, "S2 prog2 base / 2");
    checkAnn(ctx, p2, findNode(p2, "INT_LIT", 0, "10000"), "uint32", "@NONE", -1, -1, true, 10000, "S2 prog2 10000");
    if (p2.table.symbols.size() == 7) {
        ctx.checkEqual(p2.table.symbols[6].scopePath, std::string("main.agar_1.jabtak_1.agar_1"),
                       "S2 prog2: newSalary declared in the innermost nested block");
    }

    // prog3: char literals, khali call statement, standalone call result type
    checkAnn(ctx, p3, findNode(p3, "CHAR_LIT", 0), "char", "@NONE", -1, -1, true, 65, "S2 prog3 'A'");
    checkAnn(ctx, p3, findNode(p3, "CALL_STMT", 0, "auditRecord"), "khali", "FUNC_REF", 1, -1, false, -1, "S2 prog3 auditRecord ( boosted )");
    checkAnn(ctx, p3, findNode(p3, "VOID_KHALI"), "khali", "@NONE", -1, -1, false, -1, "S2 prog3 wapis khali payload");
    checkAnn(ctx, p3, findNode(p3, "FUNC_DEF", 1), "khali", "FUNC_REF", 1, -1, false, -1, "S2 prog3 fn khali auditRecord");
}

// ----------------------------------------------------------------------------
// S3: INT_LITERAL adaptation and 64-bit constant folding (Section 3.4 Rule 2)
// ----------------------------------------------------------------------------
void testS3_LiteralsAndFolding(TestContext& ctx) {
    // Minimum signed values fold before the range check (Rule 2.5)
    {
        const Unit u = expectOk(ctx, mainWith("int16 x\nint32 y\nx medalo 0 - 32768\ny medalo 0 - 2147483648\n"), "s3_min");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 0), "int16", "@NONE", -1, -1, true, -32768, "S3 0 - 32768 folds to int16 -32768");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 1), "int16", "@NONE", -1, -1, true, 32768, "S3 leaf 32768 inside the folded expression");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 1), "int32", "@NONE", -1, -1, true, -2147483648LL, "S3 0 - 2147483648 folds to int32 min");
    }
    // Folded -1 is distinguishable from 'not constant' through IS_CONST_EXPR
    {
        const Unit u = expectOk(ctx, mainWith("int32 x\nx medalo 0 - 1\nx medalo ( 0 - 7 ) / 2\n"), "s3_neg");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 0), "int32", "@NONE", -1, -1, true, -1, "S3 0 - 1 folds to -1");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 0, "/"), "int32", "@NONE", -1, -1, true, -3, "S3 (0 - 7) / 2 truncates toward zero");
    }
    // A variable operand makes the expression non-constant; its literal partner adapts alone
    {
        const Unit u = expectOk(ctx, mainWith("uint16 x\nx medalo x + 5\nx medalo x / 0\n"), "s3_mixed");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 0), "uint16", "@NONE", -1, -1, false, -1, "S3 x + 5 is not constant");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 0, "5"), "uint16", "@NONE", -1, -1, true, 5, "S3 5 adapts to uint16");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 0, "0"), "uint16", "@NONE", -1, -1, true, 0,
                 "S3 x / 0 is accepted (only constant division is checked; runtime #DE)");
    }
    // Boundaries of every integer type
    expectOk(ctx, mainWith("int16 a\nuint16 b\nint32 c\nuint32 d\na medalo 32767\nb medalo 65535\n"
                           "c medalo 2147483647\nd medalo 4294967295\na medalo 0 - 32768\n"), "s3_bounds");
    expectError(ctx, mainWith("int16 a\na medalo 32768\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 3, "s3_int16_over");
    expectError(ctx, mainWith("uint16 a\na medalo 65536\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 3, "s3_uint16_over");
    expectError(ctx, mainWith("int32 a\na medalo 2147483648\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 3, "s3_int32_over");
    expectError(ctx, mainWith("uint32 a\na medalo 0 - 1\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 3, "s3_uint32_negative");
    expectError(ctx, mainWith("int16 a\na medalo ( 0 - 32768 ) / ( 0 - 1 )\n"),
                "SEMANTIC_ERROR_LITERAL_OVERFLOW", 3, "s3_minus_min_over_minus_one");
    expectError(ctx, mainWith("int16 a\nint16 b\nb medalo a - 32768\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 4, "s3_var_minus_32768");
    expectError(ctx, mainWith("int32 a\na medalo 4294967295 * 4294967295 * 4294967295\n"),
                "SEMANTIC_ERROR_CONSTANT_OVERFLOW", 3, "s3_constant_overflow");
    expectError(ctx, mainWith("int32 a\na medalo 7 / ( 3 - 3 )\n"), "SEMANTIC_ERROR_DIVISION_BY_ZERO", 3, "s3_div_zero");

    // Defaulting to int32 when no concrete partner exists (Rule 2.4)
    {
        const Unit u = expectOk(ctx, mainWith("agar ( 1 = 1 ) shro khtm\njabtak ( 0 ) shro khtm\n"), "s3_default");
        checkAnn(ctx, u, findNode(u, "REL_EXPR"), "BOOL_COND", "@NONE", -1, -1, false, -1, "S3 1 = 1 is BOOL_COND");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 0), "int32", "@NONE", -1, -1, true, 1, "S3 1 defaults to int32");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 2), "int32", "@NONE", -1, -1, true, 0, "S3 jabtak ( 0 ) defaults to int32");
    }
    expectError(ctx, mainWith("agar ( 3000000000 = 1 ) shro khtm\n"), "SEMANTIC_ERROR_LITERAL_OVERFLOW", 2, "s3_default_range");

    // Literals adapt across arguments and wapis; never to char
    {
        const std::string src =
            "fn uint16 f ( uint16 p ) shro\n"   // 1
            "wapis 2 * 3\n"                     // 2
            "khtm\n"                            // 3
            "fn int32 main ( ) shro\n"          // 4
            "uint16 r\n"                        // 5
            "r medalo f ( 65535 )\n"            // 6
            "wapis 0\n"                         // 7
            "khtm\n";
        const Unit u = expectOk(ctx, src, "s3_args");
        checkAnn(ctx, u, findNode(u, "BIN_EXPR", 0), "uint16", "@NONE", -1, -1, true, 6, "S3 wapis 2 * 3 adapts to uint16");
        checkAnn(ctx, u, findNode(u, "INT_LIT", 0, "65535"), "uint16", "@NONE", -1, -1, true, 65535, "S3 argument adapts to uint16");
    }
    expectError(ctx, "fn uint16 f ( uint16 p ) shro\nwapis p\nkhtm\nfn int32 main ( ) shro\nuint16 r\nr medalo f ( 70000 )\nwapis 0\nkhtm\n",
                "SEMANTIC_ERROR_LITERAL_OVERFLOW", 6, "s3_arg_overflow");
    expectError(ctx, "fn int16 f ( ) shro\nwapis 70000\nkhtm\nfn int32 main ( ) shro\nwapis 0\nkhtm\n",
                "SEMANTIC_ERROR_LITERAL_OVERFLOW", 2, "s3_return_overflow");
    expectError(ctx, mainWith("char c\nc medalo 65\n"), "SEMANTIC_ERROR_TYPE_MISMATCH", 3, "s3_literal_never_char");
}

// ----------------------------------------------------------------------------
// S4: scope hierarchy, scope paths, visibility and function-wide uniqueness
// ----------------------------------------------------------------------------
void testS4_Scopes(TestContext& ctx) {
    const std::string src =
        "fn int32 main ( ) shro\n"                       // 1
        "    int32 a\n"                                  // 2
        "    agar ( a ) shro\n"                          // 3
        "        int32 b\n"                              // 4
        "        jabtak ( b ) shro\n"                    // 5
        "            int32 c\n"                          // 6
        "        khtm\n"                                 // 7
        "    khtm magar shro\n"                          // 8
        "        int32 d\n"                              // 9
        "    khtm\n"                                     // 10
        "    shro\n"                                     // 11
        "        int32 e\n"                              // 12
        "        shro\n"                                 // 13
        "            int32 f\n"                          // 14
        "            f medalo e + a\n"                   // 15
        "        khtm\n"                                 // 16
        "    khtm\n"                                     // 17
        "    agar ( a ) shro khtm\n"                     // 18
        "    agar ( a ) shro khtm magar shro khtm\n"     // 19
        "    jabtak ( a ) shro khtm\n"                   // 20
        "    wapis a\n"                                  // 21
        "khtm\n";                                        // 22
    const Unit u = expectOk(ctx, src, "s4_scopes");
    struct S { const char* path; const char* parent; int ls; int le; };
    const S expected[] = {
        {"main", "@NONE", 1, 22},
        {"main.agar_1", "main", 3, 8},
        {"main.agar_1.jabtak_1", "main.agar_1", 5, 7},
        {"main.magar_1", "main", 8, 10},
        {"main.shro_1", "main", 11, 17},
        {"main.shro_1.shro_1", "main.shro_1", 13, 16},
        {"main.agar_2", "main", 18, 18},
        {"main.agar_3", "main", 19, 19},
        {"main.magar_2", "main", 19, 19},
        {"main.jabtak_1", "main", 20, 20}
    };
    const std::size_t n = sizeof(expected) / sizeof(expected[0]);
    ctx.checkEqual(u.table.scopes.size(), n, "S4: scope count");
    for (std::size_t i = 0; i < n && i < u.table.scopes.size(); ++i) {
        const ScopeInfo& sc = u.table.scopes[i];
        const std::string p = "S4 scope[" + std::to_string(i) + "]";
        ctx.checkEqual(sc.scopeId, static_cast<int>(i), p + ".scopeId");
        ctx.checkEqual(sc.scopePath, std::string(expected[i].path), p + ".scopePath");
        ctx.checkEqual(sc.parentScopePath, std::string(expected[i].parent), p + ".parentScopePath");
        ctx.checkEqual(sc.enclosingFunc, std::string("main"), p + ".enclosingFunc");
        ctx.checkEqual(sc.lineStart, expected[i].ls, p + ".lineStart ('shro' line)");
        ctx.checkEqual(sc.lineEnd, expected[i].le, p + ".lineEnd ('khtm' line)");
    }
    const char* symScopes[6] = {"main", "main.agar_1", "main.agar_1.jabtak_1", "main.magar_1", "main.shro_1", "main.shro_1.shro_1"};
    ctx.checkEqual(u.table.symbols.size(), static_cast<std::size_t>(6), "S4: symbol count");
    for (std::size_t i = 0; i < 6 && i < u.table.symbols.size(); ++i) {
        ctx.checkEqual(u.table.symbols[i].scopePath, std::string(symScopes[i]), "S4 symbol " + u.table.symbols[i].identName + " scope");
    }

    // Visibility (Section 3.4 Rule 5 scope 2)
    expectError(ctx, mainWith("int32 a\nagar ( a ) shro\nint32 b\nkhtm magar shro\nb medalo 1\nkhtm\n"),
                "SEMANTIC_ERROR_UNDECLARED_IDENTIFIER", 6, "s4_sibling_not_visible");
    expectError(ctx, mainWith("int32 a\nshro\nint32 e\nkhtm\na medalo e\n"),
                "SEMANTIC_ERROR_UNDECLARED_IDENTIFIER", 6, "s4_after_block_not_visible");
    // No shadowing; function-wide uniqueness also across siblings and parameters (scope 3)
    expectError(ctx, mainWith("int32 a\nshro\nint32 a\nkhtm\n"), "SEMANTIC_ERROR_DUPLICATE_DECLARATION", 4, "s4_shadowing");
    expectError(ctx, mainWith("shro\nint32 t\nkhtm\nshro\nint32 t\nkhtm\n"), "SEMANTIC_ERROR_DUPLICATE_DECLARATION", 6, "s4_sibling_duplicate");
    expectError(ctx, "fn int32 f ( int32 p ) shro\nint32 p\nwapis p\nkhtm\nfn int32 main ( ) shro\nwapis 0\nkhtm\n",
                "SEMANTIC_ERROR_DUPLICATE_DECLARATION", 2, "s4_param_vs_local");
    // The same name in two different functions is allowed
    expectOk(ctx, "fn int32 f ( int32 v ) shro\nwapis v\nkhtm\nfn int32 main ( ) shro\nint32 v\nv medalo f ( 1 )\nwapis v\nkhtm\n",
             "s4_same_name_other_function");
}

// ----------------------------------------------------------------------------
// S5: annotation conventions of Section 5.4 for every node kind
// ----------------------------------------------------------------------------
void testS5_Annotations(TestContext& ctx) {
    const std::string src =
        "struct P shro\n"                          // 1
        "    int32 x\n"                            // 2
        "    char tag\n"                           // 3
        "khtm\n"                                   // 4
        "fn khali log ( int32 v ) shro\n"          // 5
        "    wapis khali\n"                        // 6
        "khtm\n"                                   // 7
        "fn int32 twice ( int32 v ) shro\n"        // 8
        "    wapis v * 2\n"                        // 9
        "khtm\n"                                   // 10
        "fn int32 main ( ) shro\n"                 // 11
        "    struct P p\n"                         // 12
        "    int32 n\n"                            // 13
        "    p.x medalo twice ( 3 )\n"             // 14
        "    p.tag medalo 'Z'\n"                   // 15
        "    log ( p.x )\n"                        // 16
        "    twice ( n )\n"                        // 17
        "    agar ( p.tag = 'Z' ) shro\n"          // 18
        "        n medalo p.x\n"                   // 19
        "    khtm\n"                               // 20
        "    jabtak ( n ) shro\n"                  // 21
        "        n medalo n - 1\n"                 // 22
        "    khtm\n"                               // 23
        "    wapis n\n"                            // 24
        "khtm\n";                                  // 25
    const Unit u = expectOk(ctx, src, "s5_annotations");
    // Symbols: 0 v(log) 1 v(twice) 2 p 3 n ; functions: 0 log 1 twice 2 main
    checkAnn(ctx, u, 0, "@NONE", "@NONE", -1, -1, false, -1, "S5 PROG");
    checkAnn(ctx, u, findNode(u, "STRUCT_DEF"), "struct P", "STRUCT_TYPE", 0, -1, false, -1, "S5 STRUCT_DEF");
    checkAnn(ctx, u, findNode(u, "FIELD_DECL", 1), "char", "STRUCT_FIELD_DECL", 0, 1, false, -1, "S5 FIELD_DECL tag");
    checkAnn(ctx, u, findNode(u, "FUNC_DEF", 2), "int32", "FUNC_REF", 2, -1, false, -1, "S5 FUNC_DEF main");
    checkAnn(ctx, u, findNode(u, "PARAM_DECL", 1), "int32", "LOCAL_SYM", 1, -1, false, -1, "S5 PARAM_DECL twice.v");
    checkAnn(ctx, u, findNode(u, "STRUCT_VAR_DECL"), "struct P", "LOCAL_SYM", 2, -1, false, -1, "S5 STRUCT_VAR_DECL p");
    checkAnn(ctx, u, findNode(u, "VAR_DECL"), "int32", "LOCAL_SYM", 3, -1, false, -1, "S5 VAR_DECL n");
    checkAnn(ctx, u, findNode(u, "BLOCK_CST"), "@NONE", "@NONE", -1, -1, false, -1, "S5 BLOCK_CST");
    checkAnn(ctx, u, findNode(u, "DECL_LIST"), "@NONE", "@NONE", -1, -1, false, -1, "S5 DECL_LIST");
    checkAnn(ctx, u, findNode(u, "STMT_LIST"), "@NONE", "@NONE", -1, -1, false, -1, "S5 STMT_LIST");
    checkAnn(ctx, u, findNode(u, "RETURN_STMT", 0), "khali", "@NONE", -1, -1, false, -1, "S5 wapis khali");
    checkAnn(ctx, u, findNode(u, "VOID_KHALI"), "khali", "@NONE", -1, -1, false, -1, "S5 VOID_KHALI");
    checkAnn(ctx, u, findNode(u, "RETURN_STMT", 1), "int32", "@NONE", -1, -1, false, -1, "S5 wapis v * 2");
    checkAnn(ctx, u, findNode(u, "ASSIGN_STMT", 0), "int32", "@NONE", -1, -1, false, -1, "S5 p.x medalo ...");
    checkAnn(ctx, u, findNode(u, "ASSIGN_STMT", 1), "char", "@NONE", -1, -1, false, -1, "S5 p.tag medalo 'Z'");
    checkAnn(ctx, u, findNode(u, "FIELD_ACCESS", 0, "x"), "int32", "FIELD_ACCESS_REF", 2, 0, false, -1, "S5 p.x");
    checkAnn(ctx, u, findNode(u, "FIELD_ACCESS", 0, "tag"), "char", "FIELD_ACCESS_REF", 2, 1, false, -1, "S5 p.tag");
    checkAnn(ctx, u, findNode(u, "VAR_REF", 0, "p"), "struct P", "LOCAL_SYM", 2, -1, false, -1, "S5 field-access base p");
    checkAnn(ctx, u, findNode(u, "FUNC_CALL_EXPR", 0, "twice"), "int32", "FUNC_REF", 1, -1, false, -1, "S5 twice ( 3 ) expression");
    checkAnn(ctx, u, findNode(u, "CALL_STMT", 0, "log"), "khali", "FUNC_REF", 0, -1, false, -1, "S5 log ( p.x ) statement");
    checkAnn(ctx, u, findNode(u, "CALL_STMT", 0, "twice"), "int32", "FUNC_REF", 1, -1, false, -1,
             "S5 value call as statement (result discarded)");
    checkAnn(ctx, u, findNode(u, "CHAR_LIT", 0), "char", "@NONE", -1, -1, true, 90, "S5 CHAR_LIT 'Z'");
    checkAnn(ctx, u, findNode(u, "IF_STMT"), "@NONE", "@NONE", -1, -1, false, -1, "S5 IF_STMT");
    checkAnn(ctx, u, findNode(u, "WHILE_STMT"), "@NONE", "@NONE", -1, -1, false, -1, "S5 WHILE_STMT");
    checkAnn(ctx, u, findNode(u, "REL_EXPR"), "BOOL_COND", "@NONE", -1, -1, false, -1, "S5 REL_EXPR char = char");
    checkAnn(ctx, u, findNode(u, "VAR_REF", 0, "n"), "int32", "LOCAL_SYM", 3, -1, false, -1, "S5 VAR_REF n");
    checkAnn(ctx, u, findNode(u, "BIN_EXPR", 1), "int32", "@NONE", -1, -1, false, -1, "S5 n - 1");
    ctx.checkEqual(u.table.nodeTypes.size(), u.ast.size(), "S5: one annotation per AST node");
}

// ----------------------------------------------------------------------------
// S6: cross-file .sym imports (Section 5.4 import rules)
// ----------------------------------------------------------------------------
void testS6_CrossFileImports(TestContext& ctx) {
    const std::string lib =
        "struct Pt shro\n    int32 x\n    int32 y\nkhtm\n"
        "fn int32 add ( int32 a , int32 b ) shro\n    wapis a + b\nkhtm\n"
        "fn int32 main ( ) shro\n    wapis 0\nkhtm\n";
    const Unit l = expectOk(ctx, lib, "s6_lib");
    const std::string app =
        "fn int32 main ( ) shro\n"     // 1
        "    struct Pt p\n"            // 2
        "    p.x medalo add ( 1 , 2 )\n"
        "    wapis p.x\n"
        "khtm\n";
    const Unit a = expectOk(ctx, app, "s6_app", {l.symPath});
    if (a.ok) {
        ctx.checkEqual(a.table.structs.size(), static_cast<std::size_t>(1), "S6: one imported struct");
        ctx.checkEqual(a.table.functions.size(), static_cast<std::size_t>(2), "S6: add imported, lib's main skipped");
        if (a.table.structs.size() == 1 && a.table.functions.size() == 2) {
            ctx.checkTrue(a.table.structs[0].isExternal, "S6: Pt IS_EXTERNAL = 1");
            ctx.checkEqual(a.table.structs[0].sourceFile, std::string("s6_lib.citu"), "S6: Pt keeps its defining SOURCE_FILE");
            ctx.checkEqual(a.table.functions[0].funcName, std::string("add"), "S6: imports come first (FUNC_ID 0)");
            ctx.checkTrue(a.table.functions[0].isExternal, "S6: add IS_EXTERNAL = 1");
            ctx.checkEqual(a.table.functions[0].params.size(), static_cast<std::size_t>(2), "S6: @FUNC_PARAM rows of an import");
            ctx.checkEqual(a.table.functions[1].funcName, std::string("main"), "S6: local main");
            ctx.checkTrue(!a.table.functions[1].isExternal, "S6: local main IS_EXTERNAL = 0");
        }
        checkAnn(ctx, a, findNode(a, "FUNC_CALL_EXPR", 0, "add"), "int32", "FUNC_REF", 0, -1, false, -1, "S6 call to imported add");
        checkAnn(ctx, a, findNode(a, "STRUCT_VAR_DECL"), "struct Pt", "LOCAL_SYM", 0, -1, false, -1, "S6 var of imported struct");
        std::string text;
        ctx.checkTrue(loadFileBinary(a.symPath, text), "S6: app .sym written");
        ctx.checkTrue(text.find("@FUNC_DEF | 0 | add | int32 | 2 | 1 | s6_lib.citu | 5\n") != std::string::npos,
                      "S6: imported function serialized with IS_EXTERNAL 1 and its original SOURCE_FILE/line");
    }

    // A repeated (transitive) load of the same definition is ignored
    const Unit b = expectOk(ctx, "fn int32 main ( ) shro\n    wapis add ( 3 , 4 )\nkhtm\n", "s6_app2", {l.symPath, a.symPath});
    ctx.checkEqual(b.table.functions.size(), static_cast<std::size_t>(2), "S6: add loaded once from two .sym files");
    ctx.checkEqual(b.table.structs.size(), static_cast<std::size_t>(1), "S6: Pt loaded once from two .sym files");

    // Defining an already-imported name
    expectError(ctx, "fn int32 add ( int32 a , int32 b ) shro\n    wapis a\nkhtm\n",
                "SEMANTIC_ERROR_DUPLICATE_GLOBAL_SYMBOL", 1, "s6_redefine_function", {l.symPath});
    expectError(ctx, "struct Pt shro\n    int32 x\nkhtm\nfn int32 main ( ) shro\n    wapis 0\nkhtm\n",
                "SEMANTIC_ERROR_DUPLICATE_GLOBAL_SYMBOL", 1, "s6_redefine_struct", {l.symPath});
    // Two earlier units define the same name differently
    const Unit l2 = expectOk(ctx, "fn int16 add ( int16 a ) shro\n    wapis a\nkhtm\n", "s6_lib2");
    expectError(ctx, "fn int32 main ( ) shro\n    wapis 0\nkhtm\n",
                "SEMANTIC_ERROR_DUPLICATE_GLOBAL_SYMBOL", 0, "s6_conflicting_imports", {l.symPath, l2.symPath});
    // Imported signatures are type-checked like local ones
    expectError(ctx, "fn int32 main ( ) shro\n    char c\n    c medalo 'a'\n    wapis add ( c , 1 )\nkhtm\n",
                "SEMANTIC_ERROR_TYPE_MISMATCH", 4, "s6_imported_arg_type", {l.symPath});
    expectError(ctx, "fn int32 main ( ) shro\n    struct Pt p\n    p.z medalo 1\n    wapis 0\nkhtm\n",
                "SEMANTIC_ERROR_UNKNOWN_FIELD", 3, "s6_imported_struct_field", {l.symPath});
    // A call to a function no earlier unit exports
    expectError(ctx, "fn int32 main ( ) shro\n    wapis sub ( 1 )\nkhtm\n",
                "SEMANTIC_ERROR_UNDECLARED_FUNCTION", 2, "s6_not_imported", {l.symPath});

    // Unreadable / corrupted imports propagate the reader's diagnostics
    {
        const Unit m = compileUnit(ctx, "fn int32 main ( ) shro\n    wapis 0\nkhtm\n", "s6_missing_import",
                                   {kDir + "/does_not_exist.sym"});
        ctx.checkTrue(!m.ok, "S6: missing import fails");
        ctx.checkEqual(m.code, std::string(""), "S6: missing import is an OS error (empty code)");
        ctx.checkTrue(m.message.find("OS I/O error") != std::string::npos, "S6: OS error message");
        const std::string bad = kDir + "/s6_corrupt.sym";
        ctx.checkTrue(writeRawFileBinary(bad, "@FORMAT | ICC_SYM | v1.0\n@SOURCE | x.citu\n"), "S6: write corrupt .sym");
        const Unit c = compileUnit(ctx, "fn int32 main ( ) shro\n    wapis 0\nkhtm\n", "s6_corrupt_import", {bad});
        ctx.checkTrue(!c.ok, "S6: corrupted import fails");
        ctx.checkEqual(c.code, std::string("FATAL_IO_FORMAT_MISMATCH"), "S6: reader code propagated");
    }
}

// ----------------------------------------------------------------------------
// S7: every SEMANTIC_ERROR_* code of Section 3.6 (code + line)
// ----------------------------------------------------------------------------
void testS7_AllDiagnostics(TestContext& ctx) {
    const std::string E = "SEMANTIC_ERROR_";
    const std::string khaliF = "fn khali k ( ) shro\nwapis khali\nkhtm\n";               // lines 1-3
    const std::string structS = "struct S shro\nint32 f\nkhtm\n";                        // lines 1-3

    expectError(ctx, mainWith("int32 a\na medalo zz\n"), E + "UNDECLARED_IDENTIFIER", 3, "s7_undeclared_identifier");
    expectError(ctx, mainWith("zz medalo 1\n"), E + "UNDECLARED_IDENTIFIER", 2, "s7_undeclared_target");
    expectError(ctx, mainWith("int32 a\na medalo nope ( )\n"), E + "UNDECLARED_FUNCTION", 3, "s7_undeclared_function");
    expectError(ctx, mainWith("nope ( 1 )\n"), E + "UNDECLARED_FUNCTION", 2, "s7_undeclared_call_stmt");
    expectError(ctx, mainWith("struct Q q\n"), E + "UNKNOWN_STRUCT", 2, "s7_unknown_struct");
    expectError(ctx, structS + mainWith("struct S s\ns.g medalo 1\n"), E + "UNKNOWN_FIELD", 6, "s7_unknown_field");

    expectError(ctx, structS + "struct S shro\nint32 g\nkhtm\n" + mainWith(""), E + "DUPLICATE_DECLARATION", 4, "s7_dup_struct");
    expectError(ctx, "struct S shro\nint32 f\nchar f\nkhtm\n" + mainWith(""), E + "DUPLICATE_DECLARATION", 3, "s7_dup_field");
    expectError(ctx, khaliF + khaliF + mainWith(""), E + "DUPLICATE_DECLARATION", 4, "s7_dup_function");
    expectError(ctx, "fn int32 f ( int32 a , int16 a ) shro\nwapis 0\nkhtm\n" + mainWith(""),
                E + "DUPLICATE_DECLARATION", 1, "s7_dup_param");
    expectError(ctx, mainWith("int32 a\nint16 a\n"), E + "DUPLICATE_DECLARATION", 3, "s7_dup_local");

    expectError(ctx, mainWith("int16 a\nint32 b\nb medalo a\n"), E + "TYPE_MISMATCH", 4, "s7_assign_type");
    expectError(ctx, mainWith("int16 a\nint32 b\nb medalo b + a\n"), E + "TYPE_MISMATCH", 4, "s7_binary_types");
    expectError(ctx, mainWith("uint32 a\nint32 b\nagar ( a < b ) shro khtm\n"), E + "TYPE_MISMATCH", 4, "s7_relational_types");
    expectError(ctx, mainWith("char c\nint32 b\nb medalo c + c\n"), E + "TYPE_MISMATCH", 4, "s7_char_arithmetic");
    expectError(ctx, mainWith("char c\nagar ( c = 65 ) shro khtm\n"), E + "TYPE_MISMATCH", 3, "s7_char_vs_int_literal");
    expectError(ctx, mainWith("int32 a\na.f medalo 1\n"), E + "TYPE_MISMATCH", 3, "s7_field_on_non_struct");
    expectError(ctx, "fn int32 f ( int32 p ) shro\nwapis p\nkhtm\n" + mainWith("int32 a\na medalo f ( 1 , 2 )\n"),
                E + "TYPE_MISMATCH", 6, "s7_argument_count");
    expectError(ctx, "fn int32 f ( int32 p ) shro\nwapis p\nkhtm\n" + mainWith("int16 a\nf ( a )\n"),
                E + "TYPE_MISMATCH", 6, "s7_argument_type");

    expectError(ctx, mainWith("int16 a\na medalo 40000\n"), E + "LITERAL_OVERFLOW", 3, "s7_literal_overflow");
    expectError(ctx, mainWith("int32 a\na medalo 4294967295 * 4294967295 * 4294967295\n"), E + "CONSTANT_OVERFLOW", 3,
                "s7_constant_overflow");
    expectError(ctx, mainWith("int32 a\na medalo 1 / 0\n"), E + "DIVISION_BY_ZERO", 3, "s7_division_by_zero");

    expectError(ctx, structS + mainWith("struct S s\nint32 a\na medalo s\n"), E + "STRUCT_VALUE_USE", 7, "s7_struct_as_source");
    expectError(ctx, structS + mainWith("struct S s\nstruct S t\ns medalo t\n"), E + "STRUCT_VALUE_USE", 7, "s7_struct_as_target");
    expectError(ctx, structS + mainWith("struct S s\nint32 a\na medalo s + 1\n"), E + "STRUCT_VALUE_USE", 7, "s7_struct_operand");
    expectError(ctx, structS + "fn int32 f ( int32 p ) shro\nwapis p\nkhtm\n" + mainWith("struct S s\nf ( s )\n"),
                E + "STRUCT_VALUE_USE", 9, "s7_struct_argument");
    expectError(ctx, structS + "fn int32 f ( ) shro\nstruct S s\nwapis s\nkhtm\n" + mainWith(""),
                E + "STRUCT_VALUE_USE", 6, "s7_struct_return");
    expectError(ctx, structS + mainWith("struct S s\nagar ( s ) shro khtm\n"), E + "STRUCT_VALUE_USE", 6, "s7_struct_condition");

    expectError(ctx, mainWith("char c\nc medalo 'x'\njabtak ( c ) shro khtm\n"), E + "BAD_CONDITION_TYPE", 4, "s7_char_condition");
    expectError(ctx, khaliF + mainWith("int32 a\na medalo k ( )\n"), E + "VOID_VALUE_USE", 6, "s7_void_value");
    expectError(ctx, khaliF + mainWith("agar ( k ( ) ) shro khtm\n"), E + "VOID_VALUE_USE", 5, "s7_void_condition");

    expectError(ctx, "fn int32 f ( ) shro\nint32 a\na medalo 1\nkhtm\n" + mainWith(""), E + "MISSING_RETURN", 4, "s7_no_return");
    expectError(ctx, "fn khali f ( ) shro\nkhtm\n" + mainWith(""), E + "MISSING_RETURN", 2, "s7_empty_body");
    expectError(ctx, "fn int32 f ( ) shro\nagar ( 1 ) shro\nwapis 1\nkhtm\nkhtm\n" + mainWith(""),
                E + "MISSING_RETURN", 5, "s7_return_only_nested");
    expectError(ctx, "fn int32 f ( ) shro\nint32 a\nwapis a\na medalo 1\nkhtm\n" + mainWith(""),
                E + "MISSING_RETURN", 5, "s7_statement_after_return");

    expectError(ctx, "fn int32 f ( ) shro\nwapis khali\nkhtm\n" + mainWith(""), E + "RETURN_TYPE_MISMATCH", 2, "s7_khali_in_int");
    expectError(ctx, "fn khali f ( ) shro\nwapis 0\nkhtm\n" + mainWith(""), E + "RETURN_TYPE_MISMATCH", 2, "s7_value_in_khali");
    expectError(ctx, "fn int32 f ( ) shro\nint16 a\nagar ( 1 ) shro\nwapis a\nkhtm\nwapis 0\nkhtm\n" + mainWith(""),
                E + "RETURN_TYPE_MISMATCH", 4, "s7_nested_wrong_type");
    expectError(ctx, "fn char f ( ) shro\nwapis 65\nkhtm\n" + mainWith(""), E + "RETURN_TYPE_MISMATCH", 2, "s7_literal_to_char_return");

    expectError(ctx, "fn khali main ( ) shro\nwapis khali\nkhtm\n", E + "INVALID_MAIN_SIGNATURE", 1, "s7_main_khali");
    expectError(ctx, "fn int32 main ( int32 a ) shro\nwapis a\nkhtm\n", E + "INVALID_MAIN_SIGNATURE", 1, "s7_main_param");
    expectError(ctx, "fn int16 main ( ) shro\nwapis 0\nkhtm\n", E + "INVALID_MAIN_SIGNATURE", 1, "s7_main_int16");
    expectError(ctx, mainWith("") + mainWith(""), E + "DUPLICATE_MAIN", 4, "s7_duplicate_main");
    expectError(ctx, "fn int32 _start ( ) shro\nwapis 0\nkhtm\n", E + "RESERVED_NAME", 1, "s7_reserved_start");
    expectError(ctx, "fn int32 M12_L_loop ( ) shro\nwapis 0\nkhtm\n", E + "RESERVED_NAME", 1, "s7_reserved_label");
    expectOk(ctx, "fn int32 M_L_x ( ) shro\nwapis 0\nkhtm\nfn int32 Mx1_L_ ( ) shro\nwapis 0\nkhtm\n" + mainWith(""),
             "s7_not_reserved");

    // DUPLICATE_GLOBAL_SYMBOL is exercised with imports in S6; library units without main are valid
    expectOk(ctx, "fn int32 helper ( ) shro\nwapis 0\nkhtm\n", "s7_library_without_main");
}

// ----------------------------------------------------------------------------
// S8: instance reuse after success and failure
// ----------------------------------------------------------------------------
void testS8_Reuse(TestContext& ctx) {
    const Unit good = expectOk(ctx, readSource(ctx, "tests/fixtures/min_return.citu"), "s8_good");
    const Unit bad = compileUnit(ctx, mainWith("zz medalo 1\n"), "s8_bad");
    ctx.checkTrue(!bad.ok, "S8: bad fixture fails");

    SemanticAnalyzer sa;
    ctx.checkTrue(!sa.writeSym(pathOf("s8_none", ".sym")), "S8: writeSym before analyze fails");
    ctx.checkTrue(!fileExists(pathOf("s8_none", ".sym")), "S8: no file written");

    ctx.checkTrue(sa.analyze(pathOf("s8_good", ".ast"), pathOf("s8_good", ".lex")), "S8: good analysis");
    ctx.checkEqual(sa.getSymbolTable().nodeTypes.size(), static_cast<std::size_t>(7), "S8: 7 annotations");
    ctx.checkTrue(!sa.analyze(pathOf("s8_bad", ".ast"), pathOf("s8_bad", ".lex")), "S8: bad analysis on same instance");
    ctx.checkEqual(sa.getErrorCode(), std::string("SEMANTIC_ERROR_UNDECLARED_IDENTIFIER"), "S8: bad code");
    ctx.checkTrue(sa.getSymbolTable().functions.empty() && sa.getSymbolTable().nodeTypes.empty(), "S8: table cleared");
    ctx.checkEqual(sa.getSourceFilename(), std::string(""), "S8: source cleared");
    ctx.checkTrue(!sa.writeSym(pathOf("s8_after_fail", ".sym")), "S8: writeSym after a failed analysis fails");
    ctx.checkTrue(!fileExists(pathOf("s8_after_fail", ".sym")), "S8: no file after failed analysis");

    ctx.checkTrue(sa.analyze(pathOf("s8_good", ".ast"), pathOf("s8_good", ".lex")), "S8: recovery analysis");
    ctx.checkTrue(!sa.hasError(), "S8: hasError reset");
    ctx.checkEqual(sa.getErrorCode(), std::string(""), "S8: code reset");
    ctx.checkEqual(sa.getErrorLine(), 0, "S8: line reset");
    ctx.checkEqual(sa.getSourceFilename(), std::string("s8_good.citu"), "S8: source restored");
    const std::string out = pathOf("s8_again", ".sym");
    ctx.checkTrue(sa.writeSym(out), "S8: writeSym after recovery");
    std::string a;
    std::string b;
    ctx.checkTrue(loadFileBinary(out, a) && loadFileBinary(good.symPath, b), "S8: both .sym readable");
    ctx.checkEqual(a, b, "S8: re-analysis output identical to a fresh analyzer's");

    // Two analyses on one instance with different import lists do not leak imports
    ctx.checkTrue(sa.analyze(pathOf("s8_good", ".ast"), pathOf("s8_good", ".lex")), "S8: no imports");
    ctx.checkEqual(sa.getSymbolTable().functions.size(), static_cast<std::size_t>(1), "S8: only local functions");
}

// ----------------------------------------------------------------------------
// S9: input integrity (.ast / .lex pairing, unreadable inputs)
// ----------------------------------------------------------------------------
void testS9_InputIntegrity(TestContext& ctx) {
    expectOk(ctx, readSource(ctx, "tests/fixtures/min_return.citu"), "s9_a");
    expectOk(ctx, mainWith("agar ( 1 ) shro khtm\n"), "s9_b");

    SemanticAnalyzer sa;
    ctx.checkTrue(!sa.analyze(pathOf("s9_a", ".ast"), pathOf("s9_b", ".lex")), "S9: mismatched .lex rejected");
    ctx.checkEqual(sa.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), "S9: mismatched pair code");

    ctx.checkTrue(!sa.analyze(pathOf("s9_missing", ".ast"), pathOf("s9_a", ".lex")), "S9: missing .ast");
    ctx.checkEqual(sa.getErrorCode(), std::string(""), "S9: missing .ast is an OS error");

    const std::string corrupt = pathOf("s9_corrupt", ".ast");
    ctx.checkTrue(writeRawFileBinary(corrupt, "@FORMAT | ICC_AST | v1.0\n@SOURCE | s9_a.citu\n@TOTAL_NODES | 7\n@END_FILE\n"),
                  "S9: write corrupt .ast");
    ctx.checkTrue(!sa.analyze(corrupt, pathOf("s9_a", ".lex")), "S9: corrupt .ast");
    ctx.checkEqual(sa.getErrorCode(), std::string("FATAL_IO_COUNT_MISMATCH"), "S9: AstReader code propagated");

    ctx.checkTrue(!sa.analyze(pathOf("s9_a", ".ast"), pathOf("s9_missing", ".lex")), "S9: missing .lex");
    ctx.checkTrue(sa.analyze(pathOf("s9_a", ".ast"), pathOf("s9_a", ".lex")), "S9: correct pair still works");
}

} // namespace

int main() {
    ICC_MKDIR("build");
    ICC_MKDIR(kDir.c_str());
    return runSuite("ICC Phase 3 (Stage 3B) Semantic Analyzer Test Suite", {
        {"S1_Golden_Min_Return_Sym_End_To_End", testS1_GoldenMinReturn},
        {"S2_Specification_Sample_Programs", testS2_SamplePrograms},
        {"S3_Literal_Adaptation_And_Constant_Folding", testS3_LiteralsAndFolding},
        {"S4_Scopes_Visibility_And_Uniqueness", testS4_Scopes},
        {"S5_Annotation_Conventions", testS5_Annotations},
        {"S6_Cross_File_Sym_Imports", testS6_CrossFileImports},
        {"S7_Every_Semantic_Diagnostic", testS7_AllDiagnostics},
        {"S8_Instance_Reuse", testS8_Reuse},
        {"S9_Input_Integrity", testS9_InputIntegrity}
    });
}
