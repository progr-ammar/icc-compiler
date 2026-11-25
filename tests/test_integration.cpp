// Phase 2 integration verifier.
//
// Usage: test_integration <dir-with-icc-output>
//
// Expects that `icc -d <dir>` has already been run on examples/prog1..3 and on
// tests/fixtures/{min_return,computeHalf}.citu. This program inspects the
// files the CLI actually wrote: it re-reads every .lex with LexReader and every
// .ast with AstReader, checks the Section 2 token/node counts, compares the
// spec's golden files (Sections 5.2/5.3) byte-for-byte, and cross-checks the
// CLI output against an independent in-memory Lexer/Parser run.

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast.h"
#include "ast_reader.h"
#include "lex_reader.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cout << "  [FAIL] " << what << "\n";
    }
}

template <typename A, typename B>
void checkEq(const A& actual, const B& expected, const std::string& what) {
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failures;
        std::cout << "  [FAIL] " << what << ": expected [" << expected << "], actual [" << actual << "]\n";
    }
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string stripCR(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

struct Sample {
    std::string citu;
    std::string stem;
    std::size_t tokens;
    std::size_t nodes;
    std::vector<std::string> topLevel;  // "STRUCT_DEF:Box", "FUNC_DEF:calcArea", ...
};

void verifySample(const std::string& outDir, const Sample& s) {
    std::cout << "--- " << s.citu << "\n";
    const int before = g_failures;
    const std::string lexPath = outDir + "/" + s.stem + ".lex";
    const std::string astPath = outDir + "/" + s.stem + ".ast";
    const std::string baseName = s.stem + ".citu";

    // 1. The .lex written by the CLI passes the full LexReader protocol
    LexReader lr;
    check(lr.read(lexPath), "LexReader accepts " + lexPath + " (" + lr.getErrorMessage() + ")");
    checkEq(lr.getTokens().size(), s.tokens, s.stem + ": token count (Spec Sec 2, incl. $)");
    checkEq(lr.getSourceFilename(), baseName, s.stem + ": .lex @SOURCE");

    // 2. The .ast written by the CLI passes the full AstReader protocol
    AstReader ar;
    check(ar.read(astPath), "AstReader accepts " + astPath + " (" + ar.getErrorMessage() + ")");
    const std::vector<ASTNode>& ast = ar.getAstNodes();
    checkEq(ast.size(), s.nodes, s.stem + ": AST node count (Spec Sec 2)");
    checkEq(ar.getSourceFilename(), baseName, s.stem + ": .ast @SOURCE");

    // 3. Top-level structure (PROG children, in order)
    if (!ast.empty()) {
        checkEq(ast[0].childIds.size(), s.topLevel.size(), s.stem + ": PROG child count");
        for (std::size_t i = 0; i < ast[0].childIds.size() && i < s.topLevel.size(); ++i) {
            const ASTNode& c = ast[static_cast<std::size_t>(ast[0].childIds[i])];
            checkEq(c.nodeKind + ":" + c.lexeme, s.topLevel[i], s.stem + ": PROG child " + std::to_string(i));
        }
    }

    // 4. Every .lex/.ast record and line ends with LF only
    std::string rawLex;
    std::string rawAst;
    check(readFile(lexPath, rawLex) && readFile(astPath, rawAst), s.stem + ": raw files readable");
    check(rawLex.find('\r') == std::string::npos && rawAst.find('\r') == std::string::npos,
          s.stem + ": intermediate files use LF line endings");
    check(rawLex.size() >= 10 && rawLex.compare(rawLex.size() - 10, 10, "@END_FILE\n") == 0,
          s.stem + ": .lex ends with @END_FILE");
    check(rawAst.size() >= 10 && rawAst.compare(rawAst.size() - 10, 10, "@END_FILE\n") == 0,
          s.stem + ": .ast ends with @END_FILE");

    // 5. Cross-check against an independent in-memory run of the same source
    std::string source;
    check(readFile(s.citu, source), s.stem + ": source readable");
    Lexer lexer(source, baseName);
    check(lexer.scanTokens(), s.stem + ": independent lex");
    const std::vector<Token>& ref = lexer.getTokens();
    checkEq(ref.size(), lr.getTokens().size(), s.stem + ": independent token count");
    for (std::size_t i = 0; i < ref.size() && i < lr.getTokens().size(); ++i) {
        const Token& a = lr.getTokens()[i];
        const Token& b = ref[i];
        if (a.index != b.index || a.line != b.line || a.col != b.col || a.terminal != b.terminal ||
            a.category != b.category || a.lexeme != b.lexeme || a.parsedIntVal != b.parsedIntVal) {
            check(false, s.stem + ": token " + std::to_string(i) + " differs between .lex and in-memory lex");
            break;
        }
    }
    Parser parser;
    check(parser.parseTokens(ref, baseName), s.stem + ": independent parse");
    const std::vector<ASTNode>& refAst = parser.getAstNodes();
    checkEq(refAst.size(), ast.size(), s.stem + ": independent AST node count");
    for (std::size_t i = 0; i < refAst.size() && i < ast.size(); ++i) {
        const ASTNode& a = ast[i];
        const ASTNode& b = refAst[i];
        if (a.nodeId != b.nodeId || a.parentId != b.parentId || a.childIndex != b.childIndex ||
            a.nodeKind != b.nodeKind || a.astOp != b.astOp || a.lexeme != b.lexeme ||
            a.litVal != b.litVal || a.line != b.line || a.childIds != b.childIds) {
            check(false, s.stem + ": AST node " + std::to_string(i) + " differs between .ast and in-memory parse");
            break;
        }
    }

    std::cout << ((g_failures == before) ? "  OK: " : "  ERRORS: ")
              << lr.getTokens().size() << " tokens, " << ast.size() << " AST nodes\n";
}

void verifyGolden(const std::string& produced, const std::string& golden) {
    std::string a;
    std::string b;
    const bool okA = readFile(produced, a);
    const bool okB = readFile(golden, b);
    check(okA, "read " + produced);
    check(okB, "read " + golden);
    // Golden files are copied from the spec text; tolerate CRLF checkouts of them.
    const bool same = okA && okB && a == stripCR(b);
    check(same, produced + " is byte-identical to " + golden);
    std::cout << "--- golden " << golden << ": " << (same ? "OK" : "MISMATCH") << "\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: test_integration <dir-with-icc-output>\n";
        return 2;
    }
    const std::string outDir = argv[1];
    std::cout << "=== ICC Phase 2 Integration Verification (CLI output in '" << outDir << "') ===\n";

    const std::vector<Sample> samples = {
        {"examples/prog1_geometry.citu", "prog1_geometry", 122, 84,
         {"STRUCT_DEF:Box", "FUNC_DEF:calcArea", "FUNC_DEF:scaleBox", "FUNC_DEF:main"}},
        {"examples/prog2_payroll.citu", "prog2_payroll", 141, 104,
         {"STRUCT_DEF:Employee", "FUNC_DEF:computeBonus", "FUNC_DEF:main"}},
        {"examples/prog3_grades.citu", "prog3_grades", 144, 107,
         {"STRUCT_DEF:Student", "FUNC_DEF:evaluateGrade", "FUNC_DEF:auditRecord", "FUNC_DEF:main"}},
        {"tests/fixtures/min_return.citu", "min_return", 10, 7, {"FUNC_DEF:main"}},
        {"tests/fixtures/computeHalf.citu", "computeHalf", 19, 14, {"FUNC_DEF:computeHalf"}}
    };
    for (const Sample& s : samples) {
        verifySample(outDir, s);
    }

    verifyGolden(outDir + "/min_return.lex", "tests/golden/min_return.lex");
    verifyGolden(outDir + "/min_return.ast", "tests/golden/min_return.ast");
    verifyGolden(outDir + "/computeHalf.ast", "tests/golden/computeHalf.ast");

    std::cout << "\n=== Summary: " << (g_checks - g_failures) << " checks passed, "
              << g_failures << " failed, " << g_checks << " total ===\n";
    return (g_failures == 0) ? 0 : 1;
}
