#include <cstdio>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast.h"
#include "ast_reader.h"
#include "ast_writer.h"
#include "lex_writer.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"

namespace {

struct TestContext {
    bool passed = true;
    std::vector<std::string> messages;

    void fail(const std::string& msg) {
        passed = false;
        messages.push_back(msg);
    }

    void checkTrue(bool condition, const std::string& desc) {
        if (!condition) {
            fail("Expected true: " + desc);
        }
    }

    template <typename T>
    void checkEqual(const T& actual, const T& expected, const std::string& desc) {
        if (!(actual == expected)) {
            std::ostringstream oss;
            oss << desc << "\n         expected: [" << expected << "]"
                << "\n         actual  : [" << actual << "]";
            fail(oss.str());
        }
    }
};

bool loadFileBinary(const std::string& path, std::string& outContent) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outContent = ss.str();
    return true;
}

bool writeRawFileBinary(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return out.good();
}

void checkAstVectorsEqual(TestContext& ctx,
                          const std::vector<ASTNode>& a,
                          const std::vector<ASTNode>& b,
                          const std::string& label) {
    ctx.checkEqual(a.size(), b.size(), label + ": AST node count");
    const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::string p = label + " node[" + std::to_string(i) + "]";
        ctx.checkEqual(a[i].nodeId, b[i].nodeId, p + ".nodeId");
        ctx.checkEqual(a[i].parentId, b[i].parentId, p + ".parentId");
        ctx.checkEqual(a[i].childIndex, b[i].childIndex, p + ".childIndex");
        ctx.checkEqual(a[i].nodeKind, b[i].nodeKind, p + ".nodeKind");
        ctx.checkEqual(a[i].astOp, b[i].astOp, p + ".astOp");
        ctx.checkEqual(a[i].lexeme, b[i].lexeme, p + ".lexeme");
        ctx.checkEqual(a[i].litVal, b[i].litVal, p + ".litVal");
        ctx.checkEqual(a[i].line, b[i].line, p + ".line");
        ctx.checkEqual(a[i].childIds.size(), b[i].childIds.size(), p + ".childIds.size");
        const std::size_t cCount = (a[i].childIds.size() < b[i].childIds.size())
                                       ? a[i].childIds.size()
                                       : b[i].childIds.size();
        for (std::size_t k = 0; k < cCount; ++k) {
            ctx.checkEqual(a[i].childIds[k], b[i].childIds[k],
                           p + ".childIds[" + std::to_string(k) + "]");
        }
    }
}

// Helper running the complete file-decoupled Phase 2 pipeline:
// source -> Lexer -> LexWriter -> .lex -> Parser::parseFromLexFile -> AstWriter -> .ast -> AstReader
bool runFullFilePipeline(TestContext& ctx,
                         const std::string& source,
                         const std::string& sourceName,
                         const std::string& stem,
                         std::vector<ASTNode>& outAst,
                         std::string& outAstRawBytes) {
    const std::string lexPath = "bin/test_" + stem + ".lex";
    const std::string astPath = "bin/test_" + stem + ".ast";
    std::remove(lexPath.c_str());
    std::remove(astPath.c_str());

    Lexer lexer(source, sourceName);
    if (!lexer.scanTokens()) {
        ctx.fail(stem + ": Lexer failed: " + lexer.getErrorMessage());
        return false;
    }

    LexWriter lexWriter;
    if (!lexWriter.write(lexPath, sourceName, lexer.getTokens())) {
        ctx.fail(stem + ": LexWriter failed: " + lexWriter.getErrorMessage());
        std::remove(lexPath.c_str());
        return false;
    }

    Parser parser;
    if (!parser.parseFromLexFile(lexPath)) {
        ctx.fail(stem + ": Parser failed: " + parser.getErrorMessage());
        std::remove(lexPath.c_str());
        return false;
    }

    AstWriter astWriter;
    if (!astWriter.write(astPath, parser.getSourceFilename(), parser.getAstNodes())) {
        ctx.fail(stem + ": AstWriter failed: " + astWriter.getErrorMessage());
        std::remove(lexPath.c_str());
        std::remove(astPath.c_str());
        return false;
    }

    ctx.checkTrue(loadFileBinary(astPath, outAstRawBytes), stem + ": read raw .ast bytes");

    AstReader astReader;
    if (!astReader.read(astPath)) {
        ctx.fail(stem + ": AstReader failed: " + astReader.getErrorMessage());
        std::remove(lexPath.c_str());
        std::remove(astPath.c_str());
        return false;
    }

    ctx.checkEqual(astReader.getSourceFilename(), sourceName, stem + ": round-trip sourceFilename");
    checkAstVectorsEqual(ctx, astReader.getAstNodes(), parser.getAstNodes(), stem + "_roundtrip");
    outAst = astReader.getAstNodes();

    std::remove(lexPath.c_str());
    std::remove(astPath.c_str());
    return true;
}

void expectSyntaxError(TestContext& ctx,
                       const std::string& source,
                       int expectedLine,
                       const std::string& label) {
    const std::string lexPath = "bin/test_synerr_" + label + ".lex";
    std::remove(lexPath.c_str());

    Lexer lexer(source, label + ".citu");
    ctx.checkTrue(lexer.scanTokens(), label + ": expected valid lexing before syntax error (got: " +
                                      lexer.getErrorMessage() + ")");
    if (!lexer.scanTokens()) {
        return;
    }

    LexWriter writer;
    ctx.checkTrue(writer.write(lexPath, label + ".citu", lexer.getTokens()), label + ": write .lex");

    Parser parser;
    const bool ok = parser.parseFromLexFile(lexPath);
    ctx.checkTrue(!ok, label + ": Parser must reject invalid syntax");
    ctx.checkTrue(parser.hasError(), label + ": hasError() must be true");
    ctx.checkEqual(parser.getErrorCode(), std::string("SYNTAX_ERROR"), label + ": errorCode == SYNTAX_ERROR");
    if (expectedLine > 0) {
        ctx.checkEqual(parser.getErrorLine(), expectedLine, label + ": errorLine");
    }
    ctx.checkTrue(parser.getErrorCol() >= 1, label + ": errorCol >= 1");
    ctx.checkTrue(parser.getAstNodes().empty(), label + ": failed parse must clear AST nodes");
    ctx.checkTrue(!parser.getErrorMessage().empty(), label + ": non-empty errorMessage");

    std::remove(lexPath.c_str());
}

// ============================================================================
// P1: LL(1) Table Dimensions & Golden Spec v1.4 Section 5.3 AST Benchmarks
// ============================================================================

void testP1_TableDimensionsAndGoldenAsts(TestContext& ctx) {
    // Verify exact Section 3.2 & 3.3 table dimensions
    ctx.checkEqual(Parser::getProductionCount(), static_cast<std::size_t>(77), "77 productions");
    ctx.checkEqual(Parser::getNonTerminalCount(), static_cast<std::size_t>(39), "39 non-terminals");
    ctx.checkEqual(Parser::getTerminalCount(), static_cast<std::size_t>(33), "33 terminals");
    ctx.checkEqual(Parser::getPopulatedTableCellCount(), static_cast<std::size_t>(186), "186 populated LL(1) cells");

    // Golden benchmark 1: min_return.citu (Spec v1.4 Section 5.3, 7 nodes)
    const std::string minReturnSrc = "fn int32 main ( ) shro\n    wapis 0\nkhtm";
    std::vector<ASTNode> minAst;
    std::string minRaw;
    if (runFullFilePipeline(ctx, minReturnSrc, "min_return.citu", "p1_min_return", minAst, minRaw)) {
        const std::string expectedMinAst =
            "@FORMAT | ICC_AST | v1.0\n"
            "@SOURCE | min_return.citu\n"
            "@TOTAL_NODES | 7\n"
            "# NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM\n"
            "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
            "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\n"
            "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
            "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1\n"
            "4 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 2\n"
            "5 | 4 | 0 | RETURN_STMT | wapis | wapis | -1 | 2\n"
            "6 | 5 | 0 | INT_LIT | @NONE | 0 | 0 | 2\n"
            "@END_FILE\n";
        ctx.checkEqual(minRaw, expectedMinAst, "P1: min_return.ast exact byte-for-byte match with v1.4 Sec 5.3");
    }

    // Golden benchmark 2: computeHalf.citu (Spec v1.4 Section 5.3, 14 nodes)
    const std::string computeHalfSrc =
        "fn uint32 computeHalf ( uint32 base ) shro\n"
        "    uint32 res\n"
        "    res medalo base / 2\n"
        "    wapis res\n"
        "khtm";
    std::vector<ASTNode> halfAst;
    std::string halfRaw;
    if (runFullFilePipeline(ctx, computeHalfSrc, "computeHalf.citu", "p1_computeHalf", halfAst, halfRaw)) {
        const std::string expectedHalfAst =
            "@FORMAT | ICC_AST | v1.0\n"
            "@SOURCE | computeHalf.citu\n"
            "@TOTAL_NODES | 14\n"
            "# NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM\n"
            "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
            "1 | 0 | 0 | FUNC_DEF | uint32 | computeHalf | -1 | 1\n"
            "2 | 1 | 0 | PARAM_DECL | uint32 | base | -1 | 1\n"
            "3 | 1 | 1 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
            "4 | 3 | 0 | DECL_LIST | @NONE | @NONE | -1 | 2\n"
            "5 | 4 | 0 | VAR_DECL | uint32 | res | -1 | 2\n"
            "6 | 3 | 1 | STMT_LIST | @NONE | @NONE | -1 | 3\n"
            "7 | 6 | 0 | ASSIGN_STMT | medalo | medalo | -1 | 3\n"
            "8 | 7 | 0 | VAR_REF | @NONE | res | -1 | 3\n"
            "9 | 7 | 1 | BIN_EXPR | / | / | -1 | 3\n"
            "10 | 9 | 0 | VAR_REF | @NONE | base | -1 | 3\n"
            "11 | 9 | 1 | INT_LIT | @NONE | 2 | 2 | 3\n"
            "12 | 6 | 1 | RETURN_STMT | wapis | wapis | -1 | 4\n"
            "13 | 12 | 0 | VAR_REF | @NONE | res | -1 | 4\n"
            "@END_FILE\n";
        ctx.checkEqual(halfRaw, expectedHalfAst, "P1: computeHalf.ast exact byte-for-byte match with v1.4 Sec 5.3");
    }

    // Golden benchmark 3: audit_module.citu (Spec v1.4 Section 5.5, 21 nodes with exact IDs)
    const std::string auditSrc =
        "fn khali auditRecord ( int32 currentMarks ) shro\n"
        "    int32 margin\n"
        "    agar ( currentMarks != 100 ) shro\n"
        "        margin medalo 100 - currentMarks\n"
        "    khtm\n"
        "    wapis khali\n"
        "khtm";
    std::vector<ASTNode> auditAst;
    std::string auditRaw;
    if (runFullFilePipeline(ctx, auditSrc, "audit_module.citu", "p1_audit", auditAst, auditRaw)) {
        ctx.checkEqual(auditAst.size(), static_cast<std::size_t>(21), "P1: audit_module 21 AST nodes");
        if (auditAst.size() == 21) {
            ctx.checkEqual(auditAst[1].nodeKind, std::string("FUNC_DEF"), "audit node 1 FUNC_DEF");
            ctx.checkEqual(auditAst[7].nodeKind, std::string("IF_STMT"), "audit node 7 IF_STMT");
            ctx.checkEqual(auditAst[8].nodeKind, std::string("REL_EXPR"), "audit node 8 REL_EXPR");
            ctx.checkEqual(auditAst[14].nodeKind, std::string("ASSIGN_STMT"), "audit node 14 ASSIGN_STMT");
            ctx.checkEqual(auditAst[16].nodeKind, std::string("BIN_EXPR"), "audit node 16 BIN_EXPR");
            ctx.checkEqual(auditAst[19].nodeKind, std::string("RETURN_STMT"), "audit node 19 RETURN_STMT");
            ctx.checkEqual(auditAst[20].nodeKind, std::string("VOID_KHALI"), "audit node 20 VOID_KHALI");
        }
    }
}

// ============================================================================
// P2: All Three Approved Sample Programs (Full File-Based Pipeline)
// ============================================================================

void verifyApprovedSamplePipeline(TestContext& ctx,
                                  const std::string& cituPath,
                                  const std::string& stem,
                                  std::size_t expectedTokenCount,
                                  std::size_t expectedAstNodeCount,
                                  const std::string& expectedStructName) {
    std::string source;
    if (!loadFileBinary(cituPath, source)) {
        ctx.fail(stem + ": could not open '" + cituPath + "' (run from repository root)");
        return;
    }

    Lexer countLexer(source, cituPath);
    ctx.checkTrue(countLexer.scanTokens(), stem + ": sample lexes cleanly");
    ctx.checkEqual(countLexer.getTokens().size(), expectedTokenCount,
                   stem + ": exact token count (including $) from Spec v1.4 Sec 2");

    std::vector<ASTNode> ast;
    std::string rawAst;
    if (runFullFilePipeline(ctx, source, cituPath, stem, ast, rawAst)) {
        ctx.checkEqual(rawAst.find('\r'), std::string::npos, stem + ": .ast must use LF only");
        ctx.checkEqual(ast.size(), expectedAstNodeCount, stem + ": exact AST node count from Spec v1.4 Sec 2");
        if (ast.size() >= 2) {
            ctx.checkEqual(ast[0].nodeKind, std::string("PROG"), stem + ": root PROG");
            ctx.checkEqual(ast[1].nodeKind, std::string("STRUCT_DEF"), stem + ": child 0 STRUCT_DEF");
            ctx.checkEqual(ast[1].lexeme, expectedStructName, stem + ": struct name");
        }
    }
}

void testP2_ApprovedSamplePrograms(TestContext& ctx) {
    verifyApprovedSamplePipeline(ctx, "examples/prog1_geometry.citu", "p2_prog1", 122, 84,  "Box");
    verifyApprovedSamplePipeline(ctx, "examples/prog2_payroll.citu",  "p2_prog2", 141, 104, "Employee");
    verifyApprovedSamplePipeline(ctx, "examples/prog3_grades.citu",   "p2_prog3", 144, 107, "Student");
}

// ============================================================================
// P3: Operator Precedence and Left-Associative AST Folding
// ============================================================================

// Bounds-checked navigation helpers. P3 previously indexed fixed node IDs
// (e.g. ast[7].childIds[1]) that pointed at a zero-child VAR_REF leaf, which
// read past the end of childIds and crashed the suite.
int childAt(const std::vector<ASTNode>& ast, int nodeId, std::size_t idx) {
    if (nodeId < 0 || static_cast<std::size_t>(nodeId) >= ast.size()) {
        return -1;
    }
    const std::vector<int>& kids = ast[static_cast<std::size_t>(nodeId)].childIds;
    if (idx >= kids.size()) {
        return -1;
    }
    const int c = kids[idx];
    return (c >= 0 && static_cast<std::size_t>(c) < ast.size()) ? c : -1;
}

std::string kindOf(const std::vector<ASTNode>& ast, int id) {
    return (id < 0) ? std::string("<missing>") : ast[static_cast<std::size_t>(id)].nodeKind;
}

std::string opOf(const std::vector<ASTNode>& ast, int id) {
    return (id < 0) ? std::string("<missing>") : ast[static_cast<std::size_t>(id)].astOp;
}

std::string lexOf(const std::vector<ASTNode>& ast, int id) {
    return (id < 0) ? std::string("<missing>") : ast[static_cast<std::size_t>(id)].lexeme;
}

// Returns the value-expression (child 1) of the k-th ASSIGN_STMT in pre-order, or -1.
int nthAssignValue(const std::vector<ASTNode>& ast, std::size_t k) {
    std::size_t seen = 0;
    for (const ASTNode& n : ast) {
        if (n.nodeKind == "ASSIGN_STMT") {
            if (seen == k) {
                return childAt(ast, n.nodeId, 1);
            }
            ++seen;
        }
    }
    return -1;
}

void testP3_PrecedenceAndAssociativity(TestContext& ctx) {
    std::vector<ASTNode> ast;
    std::string raw;

    // 1. Left associativity of subtraction: a - b - c => (a - b) - c
    const std::string subSrc =
        "fn int32 main ( ) shro\n"
        "    int32 r\n"
        "    r medalo a - b - c\n"
        "    wapis r\n"
        "khtm";
    if (runFullFilePipeline(ctx, subSrc, "sub_assoc.citu", "p3_sub", ast, raw)) {
        // Exact pre-order layout: 6 ASSIGN_STMT, 7 VAR_REF r, 8 BIN '-', 9 BIN '-', 10 a, 11 b, 12 c
        ctx.checkEqual(ast.size(), static_cast<std::size_t>(15), "a - b - c: total AST nodes");
        ctx.checkEqual(kindOf(ast, 6), std::string("ASSIGN_STMT"), "node 6 is ASSIGN_STMT");
        const int outer = nthAssignValue(ast, 0);
        ctx.checkEqual(outer, 8, "outer BIN_EXPR is node 8");
        ctx.checkEqual(kindOf(ast, outer), std::string("BIN_EXPR"), "outer node is BIN_EXPR");
        ctx.checkEqual(opOf(ast, outer), std::string("-"), "outer op is '-'");
        const int leftInner = childAt(ast, outer, 0);
        const int rightLeaf = childAt(ast, outer, 1);
        ctx.checkEqual(kindOf(ast, leftInner), std::string("BIN_EXPR"), "(a - b) is left child");
        ctx.checkEqual(opOf(ast, leftInner), std::string("-"), "inner op is '-'");
        ctx.checkEqual(lexOf(ast, childAt(ast, leftInner, 0)), std::string("a"), "leftmost is 'a'");
        ctx.checkEqual(lexOf(ast, childAt(ast, leftInner, 1)), std::string("b"), "middle is 'b'");
        ctx.checkEqual(kindOf(ast, rightLeaf), std::string("VAR_REF"), "right child is a leaf");
        ctx.checkEqual(lexOf(ast, rightLeaf), std::string("c"), "rightmost is 'c'");
    }

    // 2. Left associativity of / and *: a / b * c => (a / b) * c
    const std::string mulDivSrc =
        "fn int32 main ( ) shro\n"
        "    int32 r\n"
        "    r medalo a / b * c\n"
        "    wapis r\n"
        "khtm";
    if (runFullFilePipeline(ctx, mulDivSrc, "muldiv_assoc.citu", "p3_muldiv", ast, raw)) {
        const int outer = nthAssignValue(ast, 0);
        ctx.checkEqual(opOf(ast, outer), std::string("*"), "outer op of a / b * c is '*'");
        const int inner = childAt(ast, outer, 0);
        ctx.checkEqual(opOf(ast, inner), std::string("/"), "inner left op is '/'");
        ctx.checkEqual(lexOf(ast, childAt(ast, inner, 0)), std::string("a"), "inner left is 'a'");
        ctx.checkEqual(lexOf(ast, childAt(ast, inner, 1)), std::string("b"), "inner right is 'b'");
        ctx.checkEqual(lexOf(ast, childAt(ast, outer, 1)), std::string("c"), "right child is 'c'");
    }

    // 3. Precedence & parentheses: a + b * c vs (a + b) * c vs a - (b - c)
    const std::string precSrc =
        "fn int32 main ( ) shro\n"
        "    int32 r\n"
        "    r medalo a + b * c\n"
        "    r medalo ( a + b ) * c\n"
        "    r medalo a - ( b - c )\n"
        "    wapis r\n"
        "khtm";
    if (runFullFilePipeline(ctx, precSrc, "prec.citu", "p3_prec", ast, raw)) {
        const int expr1 = nthAssignValue(ast, 0);
        const int expr2 = nthAssignValue(ast, 1);
        const int expr3 = nthAssignValue(ast, 2);

        // a + b * c => '+' at root, '*' on right
        ctx.checkEqual(opOf(ast, expr1), std::string("+"), "a + b * c root is '+'");
        ctx.checkEqual(lexOf(ast, childAt(ast, expr1, 0)), std::string("a"), "a + b * c left is 'a'");
        ctx.checkEqual(opOf(ast, childAt(ast, expr1, 1)), std::string("*"), "a + b * c right is '*'");

        // (a + b) * c => '*' at root, '+' on left
        ctx.checkEqual(opOf(ast, expr2), std::string("*"), "(a + b) * c root is '*'");
        ctx.checkEqual(opOf(ast, childAt(ast, expr2, 0)), std::string("+"), "(a + b) * c left is '+'");
        ctx.checkEqual(lexOf(ast, childAt(ast, expr2, 1)), std::string("c"), "(a + b) * c right is 'c'");

        // a - ( b - c ) => '-' at root, parenthesised '-' on the right
        ctx.checkEqual(opOf(ast, expr3), std::string("-"), "a - (b - c) root is '-'");
        ctx.checkEqual(lexOf(ast, childAt(ast, expr3, 0)), std::string("a"), "a - (b - c) left is 'a'");
        ctx.checkEqual(opOf(ast, childAt(ast, expr3, 1)), std::string("-"), "a - (b - c) right is '-'");

        // Parentheses never produce AST nodes
        for (const ASTNode& n : ast) {
            ctx.checkTrue(n.lexeme != "(" && n.lexeme != ")", "no parenthesis nodes in AST");
        }
    }
}

// ============================================================================
// P4: Every Major Grammar Construct, Nested Blocks, Optional Magar & Zero-Arg Calls
// ============================================================================

void testP4_AllConstructsAndAlternatives(TestContext& ctx) {
    const std::string src =
        "struct A shro int16 x uint16 y khtm\n"
        "struct B shro int32 p uint32 q char ch khtm\n"
        "fn khali noop ( ) shro\n"
        "    wapis khali\n"
        "khtm\n"
        "fn int32 helper ( int32 a , int32 b , int32 c ) shro\n"
        "    wapis a + b + c\n"
        "khtm\n"
        "fn int32 main ( ) shro\n"
        "    struct B b\n"
        "    int32 z\n"
        "    shro\n"
        "        int32 inner\n"
        "        inner medalo 1\n"
        "        jabtak ( inner ) shro\n"
        "            agar ( inner ) shro\n"
        "                noop ( )\n"
        "                inner medalo 0\n"
        "            khtm\n"
        "        khtm\n"
        "    khtm\n"
        "    b.ch medalo '|'\n"
        "    z medalo helper ( 1 , 2 , 3 )\n"
        "    wapis z\n"
        "khtm";

    std::vector<ASTNode> ast;
    std::string raw;
    if (runFullFilePipeline(ctx, src, "all_constructs.citu", "p4_all", ast, raw)) {
        // Verify root has 2 STRUCT_DEF and 3 FUNC_DEF children
        ctx.checkEqual(ast[0].childIds.size(), static_cast<std::size_t>(5), "PROG has 2 structs + 3 funcs");
        ctx.checkEqual(ast[static_cast<std::size_t>(ast[0].childIds[0])].nodeKind, std::string("STRUCT_DEF"), "child 0 STRUCT_DEF");
        ctx.checkEqual(ast[static_cast<std::size_t>(ast[0].childIds[1])].nodeKind, std::string("STRUCT_DEF"), "child 1 STRUCT_DEF");
        ctx.checkEqual(ast[static_cast<std::size_t>(ast[0].childIds[2])].nodeKind, std::string("FUNC_DEF"), "child 2 FUNC_DEF");

        // Verify '|' CHAR_LIT was escaped as '%7C' in .ast and reconstructed as "'|'"
        ctx.checkTrue(raw.find("'%7C'") != std::string::npos, "P4: '|' CHAR_LIT escaped as '%7C' in .ast");
        bool foundPipeCharLit = false;
        bool foundNestedBlockStmt = false;
        bool foundZeroArgCall = false;
        for (const ASTNode& n : ast) {
            if (n.nodeKind == "CHAR_LIT" && n.lexeme == "'|'" && n.litVal == 124) {
                foundPipeCharLit = true;
            }
            if (n.nodeKind == "BLOCK_CST" && n.parentId >= 0 &&
                ast[static_cast<std::size_t>(n.parentId)].nodeKind == "STMT_LIST") {
                foundNestedBlockStmt = true;
            }
            if (n.nodeKind == "CALL_STMT" && n.lexeme == "noop" && n.childIds.empty()) {
                foundZeroArgCall = true;
            }
        }
        ctx.checkTrue(foundPipeCharLit, "P4: '|' CHAR_LIT reconstructed after round-trip");
        ctx.checkTrue(foundNestedBlockStmt, "P4: standalone nested BLOCK_CST statement parsed");
        ctx.checkTrue(foundZeroArgCall, "P4: zero-argument CALL_STMT parsed");
    }
}

// ============================================================================
// P5: Syntax Errors (Empty, Missing Delimiters, Bad Order, Malformed Exprs)
// ============================================================================

void testP5_SyntaxErrors(TestContext& ctx) {
    // 1. Empty program (no functions)
    expectSyntaxError(ctx, "", 1, "empty_program");

    // 2. Struct-only program (missing FUNC_LIST)
    expectSyntaxError(ctx, "struct Box shro int32 w khtm", 1, "struct_only_no_func");

    // 3. Empty struct (FIELD_LIST requires >= 1 field)
    expectSyntaxError(ctx, "struct Empty shro khtm\nfn int32 main ( ) shro wapis 0 khtm", 1, "empty_struct");

    // 4. Declaration after executable statement (violates DLIST before SLIST)
    expectSyntaxError(ctx,
                      "fn int32 main ( ) shro\n"
                      "    int32 a\n"
                      "    a medalo 1\n"
                      "    int32 b\n"
                      "    wapis a\n"
                      "khtm",
                      4, "decl_after_stmt");

    // 5. Declaration with initializer (forbidden by grammar)
    expectSyntaxError(ctx,
                      "fn int32 main ( ) shro\n"
                      "    int32 a medalo 5\n"
                      "    wapis a\n"
                      "khtm",
                      2, "decl_with_init");

    // 6. Missing 'shro' / missing 'khtm' / missing ')'
    expectSyntaxError(ctx, "fn int32 main ( ) wapis 0 khtm", 1, "missing_shro");
    expectSyntaxError(ctx, "fn int32 main ( ) shro\n    wapis 0\n", 3, "missing_khtm");
    expectSyntaxError(ctx, "fn int32 main ( ) shro\n    agar ( a < b shro wapis 0 khtm\n    wapis 0\nkhtm",
                      2, "missing_rparen");

    // 7. Bare 'wapis' without expression or 'khali'
    expectSyntaxError(ctx, "fn khali f ( ) shro\n    wapis\nkhtm", 3, "bare_wapis");

    // 8. Unary minus (not supported by grammar; must write 0 - 5)
    expectSyntaxError(ctx, "fn int32 main ( ) shro\n    int32 x\n    x medalo -5\n    wapis x\nkhtm",
                      3, "unary_minus");

    // 9. Chained comparison (a < b < c)
    expectSyntaxError(ctx, "fn int32 main ( ) shro\n    agar ( a < b < c ) shro wapis 0 khtm\n    wapis 0\nkhtm",
                      2, "chained_comparison");

    // 10. Chained field access (a.b.c)
    expectSyntaxError(ctx, "fn int32 main ( ) shro\n    int32 x\n    x medalo a.b.c\n    wapis x\nkhtm",
                      3, "chained_field_access");

    // 11. Struct definition appearing after function definition
    expectSyntaxError(ctx,
                      "fn int32 main ( ) shro wapis 0 khtm\n"
                      "struct Late shro int32 x khtm",
                      2, "struct_after_func");

    // 12. Direct parseTokens() on empty token vector
    Parser parser;
    ctx.checkTrue(!parser.parseTokens({}, "empty.citu"), "empty token vector rejected");
    ctx.checkEqual(parser.getErrorCode(), std::string("SYNTAX_ERROR"), "empty token vector SYNTAX_ERROR");
}

// ============================================================================
// P6: Forbidden Constructs ('class', 'do') & Invalid .lex Propagation
// ============================================================================

void testP6_ForbiddenConstructsAndInvalidLex(TestContext& ctx) {
    // 1. Forbidden 'class' rejected by Lexer in the pipeline
    {
        Lexer lex("class Box shro int32 x khtm", "bad_class.citu");
        ctx.checkTrue(!lex.scanTokens(), "P6: 'class' rejected by Lexer");
        ctx.checkEqual(lex.getErrorCode(), std::string("LEXICAL_ERROR_FORBIDDEN_CONSTRUCT"),
                       "P6: 'class' diagnostic code");
    }

    // 2. Forbidden 'do' rejected by Lexer in the pipeline
    {
        Lexer lex("fn int32 main ( ) shro do shro khtm khtm", "bad_do.citu");
        ctx.checkTrue(!lex.scanTokens(), "P6: 'do' rejected by Lexer");
        ctx.checkEqual(lex.getErrorCode(), std::string("LEXICAL_ERROR_FORBIDDEN_CONSTRUCT"),
                       "P6: 'do' diagnostic code");
    }

    // 3. Forged .lex containing 'class' as terminal rejected by Parser::parseFromLexFile via LexReader
    const std::string forgedLex = "bin/test_p6_forged.lex";
    const std::string forgedContent =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | forged.citu\n"
        "@TOTAL_TOKENS | 2\n"
        "0 | 1 | 1 | class | KEYWORD | class | -1\n"
        "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";
    ctx.checkTrue(writeRawFileBinary(forgedLex, forgedContent), "P6: write forged .lex");

    Parser parser;
    ctx.checkTrue(!parser.parseFromLexFile(forgedLex), "P6: Parser rejects forged .lex");
    ctx.checkTrue(parser.hasError(), "P6: hasError true");
    ctx.checkEqual(parser.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"),
                   "P6: LexReader invariant error propagated by Parser");
    std::remove(forgedLex.c_str());
}

// ============================================================================
// P7: AstReader Validation Protocol (All 6 Invariants + Format/Escape Errors)
// ============================================================================

void expectAstReaderError(TestContext& ctx,
                          const std::string& rawAst,
                          const std::string& expectedCode,
                          const std::string& label) {
    const std::string path = "bin/test_ast_bad_fixture.ast";
    ctx.checkTrue(writeRawFileBinary(path, rawAst), label + ": write bad .ast fixture");

    AstReader reader;
    ctx.checkTrue(!reader.read(path), label + ": AstReader must fail");
    ctx.checkTrue(reader.hasError(), label + ": hasError true");
    ctx.checkEqual(reader.getErrorCode(), expectedCode, label + ": errorCode");
    ctx.checkTrue(reader.getAstNodes().empty(), label + ": failed read clears astNodes");
    ctx.checkTrue(reader.getSourceFilename().empty(), label + ": failed read clears sourceFilename");

    std::remove(path.c_str());
}

void testP7_AstReaderValidation(TestContext& ctx) {
    // Missing file (OS error)
    {
        AstReader r;
        ctx.checkTrue(!r.read("bin/no_such_ast_file_xyz.ast"), "P7: missing .ast fails");
        ctx.checkEqual(r.getErrorCode(), std::string(""), "P7: OS error leaves errorCode empty");
    }

    // Wrong format tag & BOM
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 1\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_FORMAT_MISMATCH", "P7_wrong_tag");
    expectAstReaderError(ctx,
                         std::string("\xEF\xBB\xBF", 3) + "@FORMAT | ICC_AST | v1.0\n",
                         "FATAL_IO_FORMAT_MISMATCH", "P7_utf8_bom");

    // Missing @END_FILE & count mismatch
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 7\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n",
                         "FATAL_IO_MISSING_END_FILE", "P7_missing_end");
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 7\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_COUNT_MISMATCH", "P7_too_few_nodes");

    // Malformed percent escape in LEXEME
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 7\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
                         "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\n"
                         "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
                         "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1\n"
                         "4 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 2\n"
                         "5 | 4 | 0 | RETURN_STMT | wapis | wapis | -1 | 2\n"
                         "6 | 5 | 0 | CHAR_LIT | @NONE | '%7c' | 124 | 2\n@END_FILE\n",
                         "FATAL_IO_MALFORMED_ESCAPE", "P7_bad_escape");

    // Invariant 3: Root not PROG
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 1\n"
                         "0 | -1 | -1 | BLOCK_CST | @NONE | @NONE | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_INVARIANT_VIOLATION", "P7_root_not_prog");

    // Invariant 4: Parent-before-child violation (parentId >= nodeId)
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 2\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
                         "1 | 1 | 0 | FUNC_DEF | int32 | main | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_INVARIANT_VIOLATION", "P7_self_parent");

    // Invariant 5: Non-contiguous childIndex
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 2\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
                         "1 | 0 | 1 | FUNC_DEF | int32 | main | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_INVARIANT_VIOLATION", "P7_bad_child_index");

    // Schema violation: PROG with 0 FUNC_DEF children
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 1\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_INVARIANT_VIOLATION", "P7_prog_no_func");

    // Schema violation: BLOCK_CST with only 1 child
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 4\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
                         "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\n"
                         "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
                         "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1\n@END_FILE\n",
                         "FATAL_IO_INVARIANT_VIOLATION", "P7_block_missing_stmt_list");
}

// ============================================================================
// P8: LL(1) Table Cell-by-Cell Check Against Section 3.3
// ============================================================================

void testP8_LL1TableCells(TestContext& ctx) {
    const std::vector<std::string> nonTerminals = {
        "PROG", "STRUCT_LIST", "STRUCT_DEF", "FIELD_LIST", "FIELD_TAIL", "FIELD",
        "FUNC_LIST", "FUNC_TAIL", "FUNC_DEF", "RET_TYPE", "FUNC_NAME",
        "PARAM_LIST", "PARAM_TAIL", "PARAM", "CST", "DLIST", "DS", "TYPE",
        "SLIST", "ST", "WS", "IF_STMT", "ELSE_PART", "RET_STMT", "RET_VAL",
        "AS", "AS_TAIL", "VAR_TAIL", "COND", "COND_TAIL", "REL_OP",
        "EX", "EX'", "T", "T'", "F", "F_TAIL", "ARG_LIST", "ARG_TAIL"
    };
    const std::vector<std::string> terminals = {
        "struct", "fn", "main", "khali", "wapis", "shro", "khtm", "jabtak", "agar",
        "magar", "medalo", "int16", "int32", "uint16", "uint32", "char", "ID", "LT",
        "+", "-", "*", "/", "<", ">", "=", "!=", "<=", ">=", "(", ")", ",", ".", "$"
    };
    // "Populated Cells" column of the Section 3.3 table, rows 1..39
    const std::vector<int> expectedRowCounts = {
        2, 2, 1, 5, 6, 5, 1, 2, 1, 6, 2, 6, 2, 5, 1, 12, 6, 5, 6, 5,
        1, 1, 7, 1, 4, 1, 3, 2, 3, 7, 6, 3, 16, 3, 18, 3, 20, 4, 2
    };
    ctx.checkEqual(nonTerminals.size(), static_cast<std::size_t>(39), "39 row names");
    ctx.checkEqual(terminals.size(), static_cast<std::size_t>(33), "33 column names");

    std::vector<bool> ruleUsed(78, false);
    int total = 0;
    for (std::size_t r = 0; r < nonTerminals.size(); ++r) {
        int rowCount = 0;
        for (const std::string& t : terminals) {
            const int rule = Parser::getTableRule(nonTerminals[r], t);
            ctx.checkTrue(rule >= 0 && rule <= 77, "cell [" + nonTerminals[r] + ", " + t + "] in range");
            if (rule > 0) {
                ++rowCount;
                ruleUsed[static_cast<std::size_t>(rule)] = true;
            }
        }
        ctx.checkEqual(rowCount, expectedRowCounts[r], "populated cells in row " + nonTerminals[r]);
        total += rowCount;
    }
    ctx.checkEqual(total, 186, "186 populated cells in total");
    for (int rule = 1; rule <= 77; ++rule) {
        ctx.checkTrue(ruleUsed[static_cast<std::size_t>(rule)],
                      "rule " + std::to_string(rule) + " appears in the table");
    }

    // Spot checks of individual Section 3.3 entries
    ctx.checkEqual(Parser::getTableRule("FUNC_TAIL", "$"), 11, "FUNC_TAIL,$ -> 11");
    ctx.checkEqual(Parser::getTableRule("DLIST", "khtm"), 24, "DLIST,khtm -> 24");
    ctx.checkEqual(Parser::getTableRule("ELSE_PART", "magar"), 41, "ELSE_PART,magar -> 41");
    ctx.checkEqual(Parser::getTableRule("RET_VAL", "khali"), 45, "RET_VAL,khali -> 45");
    ctx.checkEqual(Parser::getTableRule("AS_TAIL", "("), 48, "AS_TAIL,( -> 48");
    ctx.checkEqual(Parser::getTableRule("F", "LT"), 69, "F,LT -> 69");
    ctx.checkEqual(Parser::getTableRule("F_TAIL", ","), 73, "F_TAIL,, -> 73");
    ctx.checkEqual(Parser::getTableRule("T'", "+"), 67, "T',+ -> 67");
    ctx.checkEqual(Parser::getTableRule("EX'", "*"), 0, "EX',* is an error cell");
    ctx.checkEqual(Parser::getTableRule("PROG", "$"), 0, "PROG,$ is an error cell");
    ctx.checkEqual(Parser::getTableRule("NOPE", "$"), -1, "unknown non-terminal -> -1");
    ctx.checkEqual(Parser::getTableRule("PROG", "class"), -1, "unknown terminal -> -1");
}

// ============================================================================
// P9: AstWriter Negative Tests (Schema, Identifier, Literal & childIds Consistency)
// ============================================================================

ASTNode makeAstNode(int id, int parent, int idx, const std::string& kind, const std::string& op,
                    const std::string& lex, long long lit, int line) {
    ASTNode n;
    n.nodeId = id;
    n.parentId = parent;
    n.childIndex = idx;
    n.nodeKind = kind;
    n.astOp = op;
    n.lexeme = lex;
    n.litVal = lit;
    n.line = line;
    return n;
}

// Exactly the 7-node min_return.citu tree of Section 5.3 (childIds left empty)
std::vector<ASTNode> goldenMinReturnAst() {
    return {
        makeAstNode(0, -1, -1, "PROG", "@NONE", "@NONE", -1, 1),
        makeAstNode(1, 0, 0, "FUNC_DEF", "int32", "main", -1, 1),
        makeAstNode(2, 1, 0, "BLOCK_CST", "@NONE", "@NONE", -1, 1),
        makeAstNode(3, 2, 0, "DECL_LIST", "@NONE", "@NONE", -1, 1),
        makeAstNode(4, 2, 1, "STMT_LIST", "@NONE", "@NONE", -1, 2),
        makeAstNode(5, 4, 0, "RETURN_STMT", "wapis", "wapis", -1, 2),
        makeAstNode(6, 5, 0, "INT_LIT", "@NONE", "0", 0, 2)
    };
}

void expectAstWriterError(TestContext& ctx,
                          const std::vector<ASTNode>& nodes,
                          const std::string& label) {
    const std::string path = "bin/test_p9_writer_reject.ast";
    std::remove(path.c_str());
    AstWriter writer;
    ctx.checkTrue(!writer.write(path, "neg.citu", nodes), label + ": AstWriter must reject");
    ctx.checkTrue(writer.hasError(), label + ": hasError true");
    ctx.checkEqual(writer.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), label + ": errorCode");
    std::string dummy;
    ctx.checkTrue(!loadFileBinary(path, dummy), label + ": rejected AST must not create an output file");
    std::remove(path.c_str());
}

void testP9_AstWriterRejections(TestContext& ctx) {
    // Positive control: the golden tree is accepted and serialized exactly
    {
        const std::string path = "bin/test_p9_writer_ok.ast";
        AstWriter writer;
        ctx.checkTrue(writer.write(path, "min_return.citu", goldenMinReturnAst()), "P9: golden tree accepted");
        std::string raw;
        ctx.checkTrue(loadFileBinary(path, raw), "P9: golden file written");
        ctx.checkTrue(raw.find("6 | 5 | 0 | INT_LIT | @NONE | 0 | 0 | 2\n@END_FILE\n") != std::string::npos,
                      "P9: golden INT_LIT record and sentinel present");
        std::remove(path.c_str());
    }

    expectAstWriterError(ctx, {}, "P9_empty_tree");

    std::vector<ASTNode> v = goldenMinReturnAst();
    v[1].lexeme = "int32";               // keyword used as function name
    expectAstWriterError(ctx, v, "P9_keyword_func_name");

    v = goldenMinReturnAst();
    v[1].lexeme = "9lives";              // not an identifier
    expectAstWriterError(ctx, v, "P9_bad_identifier");

    v = goldenMinReturnAst();
    v[6].litVal = 7;                     // LEXEME "0" disagrees with LIT_VAL
    expectAstWriterError(ctx, v, "P9_int_lit_mismatch");

    v = goldenMinReturnAst();
    v[6].nodeKind = "CHAR_LIT";          // "0" is not a quoted char literal
    expectAstWriterError(ctx, v, "P9_char_lit_bad_lexeme");

    v = goldenMinReturnAst();
    v[4].childIndex = 0;                 // duplicate child index under BLOCK_CST
    expectAstWriterError(ctx, v, "P9_duplicate_child_index");

    v = goldenMinReturnAst();
    v[2].childIds = {4, 3};              // childIds disagree with parentId/childIndex
    expectAstWriterError(ctx, v, "P9_childIds_inconsistent");

    v = goldenMinReturnAst();
    v[5].nodeKind = "CALL_STMT";         // CALL_STMT requires AST_OP 'call'
    expectAstWriterError(ctx, v, "P9_wrong_op_for_kind");

    v = goldenMinReturnAst();
    v[3].line = 0;                       // LINE_NUM must be >= 1
    expectAstWriterError(ctx, v, "P9_zero_line");

    v = goldenMinReturnAst();
    v[2].nodeId = 5;                     // non-contiguous IDs
    expectAstWriterError(ctx, v, "P9_noncontiguous_ids");

    // Reserved '@NONE' source filename sentinel
    AstWriter writer;
    ctx.checkTrue(!writer.write("bin/test_p9_none.ast", "@NONE", goldenMinReturnAst()),
                  "P9: '@NONE' source filename rejected");
    std::remove("bin/test_p9_none.ast");
}

// ============================================================================
// P10: Additional AstReader Corruption Cases (Identifiers, Literals, Framing)
// ============================================================================

std::string minAstWithRecord6(const std::string& record6) {
    return "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 7\n"
           "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
           "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\n"
           "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
           "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1\n"
           "4 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 2\n"
           "5 | 4 | 0 | RETURN_STMT | wapis | wapis | -1 | 2\n" +
           record6 + "\n@END_FILE\n";
}

void testP10_AstReaderMoreCorruption(TestContext& ctx) {
    const std::string inv = "FATAL_IO_INVARIANT_VIOLATION";
    const std::string fmt = "FATAL_IO_FORMAT_MISMATCH";

    // Positive control: the fixture builder produces a readable file
    {
        const std::string path = "bin/test_p10_ok.ast";
        ctx.checkTrue(writeRawFileBinary(path, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | x | -1 | 2")),
                      "P10: write positive fixture");
        AstReader r;
        ctx.checkTrue(r.read(path), "P10: positive fixture accepted (" + r.getErrorMessage() + ")");
        ctx.checkEqual(r.getAstNodes().size(), static_cast<std::size_t>(7), "P10: 7 nodes");
        std::remove(path.c_str());
    }

    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | wapis | -1 | 2"), inv, "P10_keyword_var_ref");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | a b | -1 | 2"), inv, "P10_space_in_identifier");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | INT_LIT | @NONE | 12 | 13 | 2"), inv, "P10_int_value_mismatch");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | INT_LIT | @NONE | 4294967296 | 4294967296 | 2"), inv, "P10_int_too_large");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | CHAR_LIT | @NONE | 'A' | 66 | 2"), inv, "P10_char_value_mismatch");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | CHAR_LIT | @NONE | 'AB' | 65 | 2"), inv, "P10_char_multi");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | REL_EXPR | < | > | -1 | 2"), inv, "P10_rel_lexeme_mismatch");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VOID_KHALI | khali | khali | -1 | 0"), inv, "P10_zero_line");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | FOO_NODE | @NONE | x | -1 | 2"), inv, "P10_unknown_kind");
    expectAstReaderError(ctx, minAstWithRecord6("7 | 5 | 0 | VAR_REF | @NONE | x | -1 | 2"), inv, "P10_noncontiguous_id");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | x | -1"), fmt, "P10_seven_fields");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | x | abc | 2"), fmt, "P10_non_numeric_litval");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | x | -1 | 2") + "junk\n", fmt, "P10_data_after_end");
    expectAstReaderError(ctx, minAstWithRecord6("6 | 5 | 0 | VAR_REF | @NONE | x | -1 | 2\n"
                                                "7 | 5 | 1 | VAR_REF | @NONE | y | -1 | 2"),
                         "FATAL_IO_COUNT_MISMATCH", "P10_too_many_records");
    expectAstReaderError(ctx, "@SOURCE | a.citu\n@FORMAT | ICC_AST | v1.0\n@TOTAL_NODES | 1\n@END_FILE\n",
                         fmt, "P10_header_order");
    expectAstReaderError(ctx, "@FORMAT | ICC_AST | v2.0\n@SOURCE | a.citu\n@TOTAL_NODES | 1\n@END_FILE\n",
                         fmt, "P10_wrong_version");
    expectAstReaderError(ctx, "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 0\n@END_FILE\n",
                         inv, "P10_zero_total");
    expectAstReaderError(ctx, "", fmt, "P10_empty_file");

    // STRUCT_VAR_DECL whose struct-type AST_OP is not an identifier
    expectAstReaderError(ctx,
                         "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_NODES | 8\n"
                         "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\n"
                         "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\n"
                         "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\n"
                         "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 2\n"
                         "4 | 3 | 0 | STRUCT_VAR_DECL | 9Box | b | -1 | 2\n"
                         "5 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 3\n"
                         "6 | 5 | 0 | RETURN_STMT | wapis | wapis | -1 | 3\n"
                         "7 | 6 | 0 | INT_LIT | @NONE | 0 | 0 | 3\n@END_FILE\n",
                         inv, "P10_bad_struct_type_name");

    // CRLF line endings and '#' comment lines are tolerated (Section 5.1 Rules 1 and 5)
    const std::string path = "bin/test_p10_crlf.ast";
    ctx.checkTrue(writeRawFileBinary(path,
                                     "@FORMAT | ICC_AST | v1.0\r\n@SOURCE | a%7Cb.citu\r\n@TOTAL_NODES | 7\r\n"
                                     "# comment\r\n\r\n"
                                     "0 | -1 | -1 | PROG | @NONE | @NONE | -1 | 1\r\n"
                                     "1 | 0 | 0 | FUNC_DEF | int32 | main | -1 | 1\r\n"
                                     "2 | 1 | 0 | BLOCK_CST | @NONE | @NONE | -1 | 1\r\n"
                                     "3 | 2 | 0 | DECL_LIST | @NONE | @NONE | -1 | 1\r\n"
                                     "4 | 2 | 1 | STMT_LIST | @NONE | @NONE | -1 | 2\r\n"
                                     "5 | 4 | 0 | RETURN_STMT | wapis | wapis | -1 | 2\r\n"
                                     "6 | 5 | 0 | CHAR_LIT | @NONE | '%7C' | 124 | 2\r\n@END_FILE\r\n"),
                  "P10: write CRLF fixture");
    AstReader r;
    ctx.checkTrue(r.read(path), "P10: CRLF fixture accepted (" + r.getErrorMessage() + ")");
    ctx.checkEqual(r.getSourceFilename(), std::string("a|b.citu"), "P10: @SOURCE percent-decoded");
    if (r.getAstNodes().size() == 7) {
        ctx.checkEqual(r.getAstNodes()[6].lexeme, std::string("'|'"), "P10: LEXEME percent-decoded");
        ctx.checkEqual(r.getAstNodes()[2].childIds.size(), static_cast<std::size_t>(2), "P10: childIds rebuilt");
    }
    std::remove(path.c_str());
}

// ============================================================================
// P11: Instance Reuse (Parser, AstWriter, AstReader) and parseTokens Preconditions
// ============================================================================

Token makeToken(int idx, int line, int col, const std::string& term, const std::string& cat,
                const std::string& lex, long long val) {
    Token t;
    t.index = idx;
    t.line = line;
    t.col = col;
    t.terminal = term;
    t.category = cat;
    t.lexeme = lex;
    t.parsedIntVal = val;
    return t;
}

void testP11_ReuseAndTokenPreconditions(TestContext& ctx) {
    const std::string goodLex = "bin/test_p11_good.lex";
    const std::string badLex = "bin/test_p11_bad.lex";
    {
        Lexer l1("fn int32 main ( ) shro\n    wapis 0\nkhtm", "good.citu");
        Lexer l2("fn int32 main ( ) shro\n    wapis\nkhtm", "bad.citu");
        ctx.checkTrue(l1.scanTokens() && l2.scanTokens(), "P11: fixtures lex");
        LexWriter w;
        ctx.checkTrue(w.write(goodLex, "good.citu", l1.getTokens()), "P11: write good .lex");
        ctx.checkTrue(w.write(badLex, "bad.citu", l2.getTokens()), "P11: write bad .lex");
    }

    // Parser: good -> bad -> good on one instance
    Parser parser;
    ctx.checkTrue(parser.parseFromLexFile(goodLex), "P11: first parse ok");
    ctx.checkEqual(parser.getAstNodes().size(), static_cast<std::size_t>(7), "P11: 7 nodes");
    ctx.checkTrue(!parser.parseFromLexFile(badLex), "P11: bad parse fails");
    ctx.checkEqual(parser.getErrorCode(), std::string("SYNTAX_ERROR"), "P11: SYNTAX_ERROR");
    ctx.checkEqual(parser.getErrorLine(), 3, "P11: error at 'khtm' on line 3");
    ctx.checkTrue(parser.getAstNodes().empty(), "P11: AST cleared after failure");
    ctx.checkTrue(parser.parseFromLexFile(goodLex), "P11: recovery parse ok");
    ctx.checkTrue(!parser.hasError(), "P11: hasError reset");
    ctx.checkEqual(parser.getErrorMessage(), std::string(""), "P11: errorMessage reset");
    ctx.checkEqual(parser.getSourceFilename(), std::string("good.citu"), "P11: source restored");
    ctx.checkEqual(parser.getAstNodes().size(), static_cast<std::size_t>(7), "P11: 7 nodes again");

    // Missing .lex file is reported and does not crash
    ctx.checkTrue(!parser.parseFromLexFile("bin/no_such_p11.lex"), "P11: missing .lex fails");
    ctx.checkTrue(parser.hasError(), "P11: missing .lex sets hasError");

    // AstWriter + AstReader reuse
    const std::string astA = "bin/test_p11_a.ast";
    const std::string astBad = "bin/test_p11_bad.ast";
    ctx.checkTrue(parser.parseFromLexFile(goodLex), "P11: parse for writer");
    AstWriter aw;
    ctx.checkTrue(aw.write(astA, parser.getSourceFilename(), parser.getAstNodes()), "P11: write A");
    ctx.checkTrue(!aw.write(astBad, "x.citu", {}), "P11: writer rejects empty tree");
    ctx.checkTrue(aw.write(astA, parser.getSourceFilename(), parser.getAstNodes()), "P11: writer recovers");
    ctx.checkTrue(!aw.hasError(), "P11: writer hasError reset");

    ctx.checkTrue(writeRawFileBinary(astBad, "@FORMAT | ICC_AST | v1.0\n"), "P11: write truncated .ast");
    AstReader ar;
    ctx.checkTrue(ar.read(astA), "P11: reader A ok");
    ctx.checkTrue(!ar.read(astBad), "P11: reader rejects truncated");
    ctx.checkTrue(ar.getAstNodes().empty(), "P11: reader cleared after failure");
    ctx.checkTrue(ar.read(astA), "P11: reader recovers");
    ctx.checkTrue(!ar.hasError(), "P11: reader hasError reset");
    ctx.checkEqual(ar.getAstNodes().size(), static_cast<std::size_t>(7), "P11: reader 7 nodes");
    ctx.checkEqual(ar.getSourceFilename(), std::string("good.citu"), "P11: reader source");

    // parseTokens() preconditions on hand-built token vectors
    const std::vector<Token> toks = {
        makeToken(0, 1, 1, "fn", "KEYWORD", "fn", -1),
        makeToken(1, 1, 4, "int32", "TYPE", "int32", -1),
        makeToken(2, 1, 10, "main", "KEYWORD", "main", -1),
        makeToken(3, 1, 15, "(", "PUNCTUATION", "(", -1),
        makeToken(4, 1, 17, ")", "PUNCTUATION", ")", -1),
        makeToken(5, 1, 19, "shro", "KEYWORD", "shro", -1),
        makeToken(6, 2, 5, "wapis", "KEYWORD", "wapis", -1),
        makeToken(7, 2, 11, "LT", "INT_LITERAL", "0", 0),
        makeToken(8, 3, 1, "khtm", "KEYWORD", "khtm", -1),
        makeToken(9, 3, 5, "$", "EOF_MARKER", "$", -1)
    };
    Parser p2;
    ctx.checkTrue(p2.parseTokens(toks, "t.citu"), "P11: hand-built tokens parse");

    std::vector<Token> badCat = toks;
    badCat[7].category = "BOGUS";
    ctx.checkTrue(!p2.parseTokens(badCat, "t.citu"), "P11: LT with bogus category rejected");
    ctx.checkEqual(p2.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), "P11: LT category code");

    std::vector<Token> badId = toks;
    badId[7] = makeToken(7, 2, 11, "ID", "IDENTIFIER", "x|y", -1);
    ctx.checkTrue(!p2.parseTokens(badId, "t.citu"), "P11: ID with invalid lexeme rejected");
    ctx.checkEqual(p2.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), "P11: ID lexeme code");

    std::vector<Token> noEof = toks;
    noEof.pop_back();
    ctx.checkTrue(!p2.parseTokens(noEof, "t.citu"), "P11: missing $ rejected");
    ctx.checkEqual(p2.getErrorCode(), std::string("SYNTAX_ERROR"), "P11: missing $ code");

    std::vector<Token> extra = toks;
    extra.push_back(makeToken(10, 3, 6, "fn", "KEYWORD", "fn", -1));
    ctx.checkTrue(!p2.parseTokens(extra, "t.citu"), "P11: token after $ rejected");
    ctx.checkEqual(p2.getErrorCode(), std::string("SYNTAX_ERROR"), "P11: trailing token code");

    std::remove(goodLex.c_str());
    std::remove(badLex.c_str());
    std::remove(astA.c_str());
    std::remove(astBad.c_str());
}

// ============================================================================
// P12: Section 4.2-C LINE_NUM Rules and Multi-Line Constructs
// ============================================================================

void testP12_LineNumbers(TestContext& ctx) {
    const std::string src =
        "struct S shro\n"          // 1
        "  int32 f\n"              // 2
        "khtm\n"                   // 3
        "fn khali g ( ) shro\n"    // 4
        "  wapis\n"                // 5
        "    khali\n"              // 6
        "khtm\n"                   // 7
        "fn int32 main ( )\n"      // 8
        "shro\n"                   // 9
        "  struct S s\n"           // 10
        "  s.f medalo 1 +\n"       // 11
        "    2\n"                  // 12
        "  jabtak ( s.f\n"         // 13
        "     < 9 ) shro khtm\n"   // 14
        "  g ( )\n"                // 15
        "  wapis s.f\n"            // 16
        "khtm\n";                  // 17
    std::vector<ASTNode> ast;
    std::string raw;
    if (!runFullFilePipeline(ctx, src, "lines.citu", "p12_lines", ast, raw)) {
        return;
    }
    auto findKind = [&](const std::string& kind, std::size_t nth) -> int {
        std::size_t seen = 0;
        for (const ASTNode& n : ast) {
            if (n.nodeKind == kind && seen++ == nth) {
                return n.nodeId;
            }
        }
        return -1;
    };
    auto lineOf = [&](int id) { return (id < 0) ? -1 : ast[static_cast<std::size_t>(id)].line; };

    ctx.checkEqual(lineOf(0), 1, "PROG takes line of first token");
    ctx.checkEqual(lineOf(findKind("STRUCT_DEF", 0)), 1, "STRUCT_DEF line of 'struct'");
    ctx.checkEqual(lineOf(findKind("FIELD_DECL", 0)), 2, "FIELD_DECL line of type keyword");
    ctx.checkEqual(lineOf(findKind("DECL_LIST", 0)), 4, "g(): empty DECL_LIST line of 'shro'");
    ctx.checkEqual(lineOf(findKind("RETURN_STMT", 0)), 5, "RETURN_STMT line of 'wapis'");
    ctx.checkEqual(lineOf(findKind("VOID_KHALI", 0)), 6, "VOID_KHALI line of 'khali'");
    ctx.checkEqual(lineOf(findKind("FUNC_DEF", 1)), 8, "FUNC_DEF line of 'fn'");
    ctx.checkEqual(lineOf(findKind("BLOCK_CST", 1)), 9, "BLOCK_CST line of 'shro'");
    ctx.checkEqual(lineOf(findKind("DECL_LIST", 1)), 10, "DECL_LIST line of first declaration");
    ctx.checkEqual(lineOf(findKind("STMT_LIST", 1)), 11, "STMT_LIST line of first statement");
    ctx.checkEqual(lineOf(findKind("STRUCT_VAR_DECL", 0)), 10, "STRUCT_VAR_DECL line of 'struct'");
    ctx.checkEqual(lineOf(findKind("BIN_EXPR", 0)), 11, "BIN_EXPR line of operator");
    ctx.checkEqual(lineOf(findKind("INT_LIT", 1)), 12, "INT_LIT line of literal");
    ctx.checkEqual(lineOf(findKind("WHILE_STMT", 0)), 13, "WHILE_STMT line of 'jabtak'");
    ctx.checkEqual(lineOf(findKind("REL_EXPR", 0)), 14, "REL_EXPR line of operator");
    ctx.checkEqual(lineOf(findKind("CALL_STMT", 0)), 15, "CALL_STMT line of callee identifier");
    // Empty DECL_LIST / STMT_LIST inside the loop body take the line of its 'shro'
    const int loopBody = findKind("BLOCK_CST", 2);
    ctx.checkEqual(lineOf(loopBody), 14, "loop BLOCK_CST line of 'shro'");
    ctx.checkEqual(lineOf(childAt(ast, loopBody, 0)), 14, "empty DECL_LIST line of 'shro'");
    ctx.checkEqual(lineOf(childAt(ast, loopBody, 1)), 14, "empty STMT_LIST line of 'shro'");
}

} // namespace

int main() {
    struct TestCase {
        std::string name;
        std::function<void(TestContext&)> fn;
    };

    const std::vector<TestCase> suite = {
        {"P1_Table_Dimensions_And_Golden_ASTs",        testP1_TableDimensionsAndGoldenAsts},
        {"P2_Approved_Sample_Programs_Full_Pipeline",  testP2_ApprovedSamplePrograms},
        {"P3_Precedence_And_Left_Associativity",       testP3_PrecedenceAndAssociativity},
        {"P4_All_Constructs_Nested_And_Alternatives",  testP4_AllConstructsAndAlternatives},
        {"P5_Syntax_Errors_And_Malformed_Inputs",      testP5_SyntaxErrors},
        {"P6_Forbidden_Constructs_And_Invalid_Lex",    testP6_ForbiddenConstructsAndInvalidLex},
        {"P7_AstReader_Invariants_And_Corruption",     testP7_AstReaderValidation},
        {"P8_LL1_Table_Cells_Match_Section_3_3",       testP8_LL1TableCells},
        {"P9_AstWriter_Rejections",                    testP9_AstWriterRejections},
        {"P10_AstReader_More_Corruption_Cases",        testP10_AstReaderMoreCorruption},
        {"P11_Instance_Reuse_And_Token_Preconditions", testP11_ReuseAndTokenPreconditions},
        {"P12_Line_Number_Rules",                      testP12_LineNumbers}
    };

    int passedCount = 0;
    int failedCount = 0;

    std::cout << "=== ICC Phase 2 Parser & AST Pipeline Test Suite ===\n\n";

    for (const TestCase& tc : suite) {
        TestContext ctx;
        tc.fn(ctx);

        if (ctx.passed) {
            ++passedCount;
            std::cout << "[PASS] " << tc.name << std::endl;
        } else {
            ++failedCount;
            std::cout << "[FAIL] " << tc.name << "\n";
            for (const std::string& msg : ctx.messages) {
                std::cout << "       -> " << msg << "\n";
            }
        }
    }

    std::cout << "\n=== Summary: " << passedCount << " passed, "
              << failedCount << " failed, "
              << suite.size() << " total ===\n";

    return (failedCount == 0) ? 0 : 1;
}