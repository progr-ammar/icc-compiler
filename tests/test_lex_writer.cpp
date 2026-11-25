#include <cstdio>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "lex_writer.h"
#include "lexer.h"
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
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    outContent = ss.str();
    return true;
}

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    return file.is_open();
}

std::vector<std::string> splitLinesStrictLF(const std::string& content) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < content.size()) {
        const std::size_t pos = content.find('\n', start);
        if (pos == std::string::npos) {
            lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

std::size_t countChar(const std::string& s, char target) {
    std::size_t count = 0;
    for (char c : s) {
        if (c == target) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Test 1: Exact Golden Byte-for-Byte Match Against Spec v1.4 Section 5.2
// ============================================================================

void testGoldenMinReturnExactBytes(TestContext& ctx) {
    const std::string source = "fn int32 main ( ) shro\n    wapis 0\nkhtm";
    const std::string outPath = "bin/test_min_return.lex";
    std::remove(outPath.c_str());

    Lexer lexer(source, "min_return.citu");
    ctx.checkTrue(lexer.scanTokens(), "Lexer must scan min_return.citu");

    LexWriter writer;
    ctx.checkTrue(writer.write(outPath, "min_return.citu", lexer.getTokens()),
                  "LexWriter must write min_return.lex (err: " + writer.getErrorMessage() + ")");
    ctx.checkTrue(!writer.hasError(), "hasError() must be false after valid write");
    ctx.checkEqual(writer.getErrorCode(), std::string(""), "errorCode must be empty");

    std::string actualBytes;
    ctx.checkTrue(loadFileBinary(outPath, actualBytes), "Must read back generated min_return.lex");

    const std::string expectedBytes =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | min_return.citu\n"
        "@TOTAL_TOKENS | 10\n"
        "# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL\n"
        "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
        "1 | 1 | 4 | int32 | TYPE | int32 | -1\n"
        "2 | 1 | 10 | main | KEYWORD | main | -1\n"
        "3 | 1 | 15 | ( | PUNCTUATION | ( | -1\n"
        "4 | 1 | 17 | ) | PUNCTUATION | ) | -1\n"
        "5 | 1 | 19 | shro | KEYWORD | shro | -1\n"
        "6 | 2 | 5 | wapis | KEYWORD | wapis | -1\n"
        "7 | 2 | 11 | LT | INT_LITERAL | 0 | 0\n"
        "8 | 3 | 1 | khtm | KEYWORD | khtm | -1\n"
        "9 | 3 | 5 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    ctx.checkEqual(actualBytes.find('\r'), std::string::npos, "Output must use LF only (zero CR bytes)");
    ctx.checkEqual(actualBytes, expectedBytes, "Generated min_return.lex must match Spec v1.4 Section 5.2 byte-for-byte");

    std::remove(outPath.c_str());
}

// ============================================================================
// Test 2: Class A Escaping (%25, %7C, %0A, %0D) & Literal "@NONE" Lexeme
// ============================================================================

void testClassAEscapingAndAtNoneLexeme(TestContext& ctx) {
    // Part A: Source contains '|', '%', ' ', and all 5 Citu backslash escape literals
    const std::string source = "'|' '%' ' ' '\\n' '\\t' '\\0' '\\\\' '\\''";
    const std::string trickySourceName = " dir%7C|name\n\r ";
    const std::string outPath = "bin/test_escaping.lex";
    std::remove(outPath.c_str());

    Lexer lexer(source, trickySourceName);
    ctx.checkTrue(lexer.scanTokens(), "Lexer must scan escaping test tokens");

    LexWriter writer;
    ctx.checkTrue(writer.write(outPath, trickySourceName, lexer.getTokens()),
                  "LexWriter must succeed on special Class A characters");

    std::string actualBytes;
    ctx.checkTrue(loadFileBinary(outPath, actualBytes), "Must read back escaping .lex");

    const std::string expectedBytes =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE |  dir%257C%7Cname%0A%0D \n"
        "@TOTAL_TOKENS | 9\n"
        "# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL\n"
        "0 | 1 | 1 | LT | CHAR_LITERAL | '%7C' | 124\n"
        "1 | 1 | 5 | LT | CHAR_LITERAL | '%25' | 37\n"
        "2 | 1 | 9 | LT | CHAR_LITERAL | ' ' | 32\n"
        "3 | 1 | 13 | LT | CHAR_LITERAL | '\\n' | 10\n"
        "4 | 1 | 18 | LT | CHAR_LITERAL | '\\t' | 9\n"
        "5 | 1 | 23 | LT | CHAR_LITERAL | '\\0' | 0\n"
        "6 | 1 | 28 | LT | CHAR_LITERAL | '\\\\' | 92\n"
        "7 | 1 | 33 | LT | CHAR_LITERAL | '\\'' | 39\n"
        "8 | 1 | 37 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    ctx.checkEqual(actualBytes.find('\r'), std::string::npos, "Escaped output must contain zero raw CR bytes");
    ctx.checkEqual(actualBytes, expectedBytes, "Escaped @SOURCE and LEXEME records must match v1.4 exactly");

    std::remove(outPath.c_str());

    // Part B: Manually constructed valid token vector whose lexeme is literally "@NONE"
    const std::string outAtNone = "bin/test_atnone_lexeme.lex";
    std::remove(outAtNone.c_str());

    const std::vector<Token> atNoneTokens = {
        Token{0, 1, 1, "ID", "IDENTIFIER", "@NONE", -1},
        Token{1, 1, 6, "$",  "EOF_MARKER", "$",     -1}
    };

    ctx.checkTrue(writer.write(outAtNone, "manual_atnone.citu", atNoneTokens),
                  "LexWriter must accept '@NONE' as an ordinary Class A lexeme (err: " +
                  writer.getErrorMessage() + ")");
    ctx.checkTrue(!writer.hasError(), "hasError() must be false after writing '@NONE' lexeme");

    std::string actualAtNone;
    ctx.checkTrue(loadFileBinary(outAtNone, actualAtNone), "Must read back @NONE lexeme .lex");
    const std::string expectedAtNone =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | manual_atnone.citu\n"
        "@TOTAL_TOKENS | 2\n"
        "# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL\n"
        "0 | 1 | 1 | ID | IDENTIFIER | @NONE | -1\n"
        "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";
    ctx.checkEqual(actualAtNone, expectedAtNone,
                   "LexWriter must serialize '@NONE' lexeme as an ordinary Class A field");

    std::remove(outAtNone.c_str());
}

// ============================================================================
// Test 3: Empty @SOURCE Sentinel (@NONE) and Literal "@NONE" Source Rejection
// ============================================================================

void testEmptyAndSentinelSourceFilename(TestContext& ctx) {
    const std::string outEmpty = "bin/test_empty_source.lex";
    const std::string outForbidden = "bin/test_sentinel_source.lex";
    std::remove(outEmpty.c_str());
    std::remove(outForbidden.c_str());

    const std::vector<Token> eofOnly = {
        Token{0, 1, 1, "$", "EOF_MARKER", "$", -1}
    };

    LexWriter writer;
    ctx.checkTrue(writer.write(outEmpty, "", eofOnly), "Empty sourceFilename must serialize as @NONE");

    std::string actualBytes;
    ctx.checkTrue(loadFileBinary(outEmpty, actualBytes), "Must read back empty-source .lex");
    const std::string expectedBytes =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | @NONE\n"
        "@TOTAL_TOKENS | 1\n"
        "# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL\n"
        "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";
    ctx.checkEqual(actualBytes, expectedBytes, "Empty sourceFilename must produce '@SOURCE | @NONE'");

    // Literal "@NONE" sourceFilename must still be rejected without creating the file
    ctx.checkTrue(!writer.write(outForbidden, "@NONE", eofOnly),
                  "Literal sourceFilename '@NONE' must be rejected");
    ctx.checkTrue(writer.hasError(), "hasError() must be true after '@NONE' source rejection");
    ctx.checkEqual(writer.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"),
                   "Error code for '@NONE' source collision");
    ctx.checkTrue(!fileExists(outForbidden), "Failed precondition must not create output file");

    std::remove(outEmpty.c_str());
    std::remove(outForbidden.c_str());
}

// ============================================================================
// Test 4: Invalid Token Vector Precondition Rejections (Full v1.4 Contract)
// ============================================================================

void expectWriterPreconditionFailure(TestContext& ctx,
                                     const std::vector<Token>& badTokens,
                                     const std::string& label) {
    const std::string outPath = "bin/test_should_not_exist.lex";
    std::remove(outPath.c_str());

    LexWriter writer;
    const bool ok = writer.write(outPath, label + ".citu", badTokens);
    ctx.checkTrue(!ok, label + ": write() must fail");
    ctx.checkTrue(writer.hasError(), label + ": hasError() must be true");
    ctx.checkEqual(writer.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"),
                   label + ": errorCode must be FATAL_IO_INVARIANT_VIOLATION");
    ctx.checkTrue(!writer.getErrorMessage().empty(), label + ": errorMessage non-empty");
    ctx.checkTrue(!fileExists(outPath), label + ": output file must not be created on precondition failure");
}

void testInvalidTokenVectors(TestContext& ctx) {
    // Empty vector & EOF invariants
    expectWriterPreconditionFailure(ctx, {}, "empty_tokens");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "khtm", "KEYWORD", "khtm", -1}
    }, "missing_eof");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "$", "EOF_MARKER", "$", -1},
        Token{1, 1, 2, "$", "EOF_MARKER", "$", -1}
    }, "duplicate_eof");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "$", "EOF_MARKER", "$", 0}
    }, "eof_nonzero_val");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "$", "EOF_MARKER", "EOF", -1}
    }, "eof_wrong_lexeme");

    // Index & coordinate invariants
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "fn", "KEYWORD", "fn", -1},
        Token{2, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "index_gap");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 0, 1, "$", "EOF_MARKER", "$", -1}
    }, "line_zero");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 0, "$", "EOF_MARKER", "$", -1}
    }, "col_zero");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 2, 1, "fn", "KEYWORD", "fn", -1},
        Token{1, 1, 5, "$", "EOF_MARKER", "$", -1}
    }, "decreasing_line");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 5, "fn", "KEYWORD", "fn", -1},
        Token{1, 1, 5, "$", "EOF_MARKER", "$", -1}
    }, "non_increasing_col_same_line");

    // Empty lexeme & 33-terminal / 9-category validation
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "ID", "IDENTIFIER", "", -1},
        Token{1, 1, 2, "$", "EOF_MARKER", "$", -1}
    }, "empty_lexeme");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "fn|bad", "KEYWORD", "fn", -1},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "pipe_in_terminal");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "bogus", "KEYWORD", "bogus", -1},
        Token{1, 1, 6, "$", "EOF_MARKER", "$", -1}
    }, "unknown_terminal");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "int32", "KEYWORD", "int32", -1},
        Token{1, 1, 6, "$", "EOF_MARKER", "$", -1}
    }, "type_with_keyword_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "fn", "TYPE", "fn", -1},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "keyword_with_type_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "+", "REL_OP", "+", -1},
        Token{1, 1, 2, "$", "EOF_MARKER", "$", -1}
    }, "arith_with_rel_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "<=", "ARITH_OP", "<=", -1},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "rel_with_arith_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "(", "KEYWORD", "(", -1},
        Token{1, 1, 2, "$", "EOF_MARKER", "$", -1}
    }, "punct_with_keyword_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "ID", "KEYWORD", "score", -1},
        Token{1, 1, 6, "$", "EOF_MARKER", "$", -1}
    }, "id_with_keyword_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "fn", "KEYWORD", "main", -1},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "fixed_terminal_lexeme_mismatch");

    // Literal parsedIntVal invariants
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "LT", "INT_LITERAL", "4294967296", 4294967296LL},
        Token{1, 1, 11, "$", "EOF_MARKER", "$", -1}
    }, "int_literal_above_uint32_max");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "LT", "INT_LITERAL", "0", -1},
        Token{1, 1, 2, "$", "EOF_MARKER", "$", -1}
    }, "int_literal_negative");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "LT", "CHAR_LITERAL", "'A'", 128},
        Token{1, 1, 4, "$", "EOF_MARKER", "$", -1}
    }, "char_literal_above_127");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "LT", "CHAR_LITERAL", "'A'", -1},
        Token{1, 1, 4, "$", "EOF_MARKER", "$", -1}
    }, "char_literal_negative");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "fn", "KEYWORD", "fn", 5},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "non_lt_with_parsed_val");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "LT", "KEYWORD", "10", 10},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "lt_with_non_literal_category");
    expectWriterPreconditionFailure(ctx, {
        Token{0, 1, 1, "ID", "INT_LITERAL", "42", 42},
        Token{1, 1, 3, "$", "EOF_MARKER", "$", -1}
    }, "id_with_int_literal_category");
}

// ============================================================================
// Test 5: OS File-Writing Failures & Error-State Reset on Reuse
// ============================================================================

void testOsWriteFailureAndReuse(TestContext& ctx) {
    const std::vector<Token> validTokens = {
        Token{0, 1, 1, "$", "EOF_MARKER", "$", -1}
    };

    LexWriter writer;

    // Missing parent directory
    const bool okBadDir = writer.write("tests/nonexistent_dir_xyz/out.lex", "a.citu", validTokens);
    ctx.checkTrue(!okBadDir, "Writing into non-existent directory must fail");
    ctx.checkTrue(writer.hasError(), "hasError() must be true after OS open failure");
    ctx.checkEqual(writer.getErrorCode(), std::string(""),
                   "OS open failure must leave errorCode empty (no invented FATAL_IO_* code)");
    ctx.checkTrue(!writer.getErrorMessage().empty(), "OS open failure must populate errorMessage");

    // Subsequent valid write on the same LexWriter instance must reset error state
    const std::string outOk = "bin/test_reuse_ok.lex";
    std::remove(outOk.c_str());
    ctx.checkTrue(writer.write(outOk, "a.citu", validTokens),
                  "Subsequent valid write on same LexWriter instance must succeed");
    ctx.checkTrue(!writer.hasError(), "hasError() must reset to false");
    ctx.checkEqual(writer.getErrorCode(), std::string(""), "errorCode must remain empty");
    ctx.checkEqual(writer.getErrorMessage(), std::string(""), "errorMessage must reset to empty");

    std::remove(outOk.c_str());
}

// ============================================================================
// Test 6: All Three Approved Citu Sample Programs (Independent Format Checks)
// ============================================================================

void verifySampleProgramLexFile(TestContext& ctx,
                                const std::string& cituPath,
                                const std::string& outLexPath,
                                std::size_t expectedTokenCount,
                                const std::string& expectedStructName,
                                std::size_t spotCheckTokenIndex,
                                const std::string& expectedSpotCheckLine,
                                const std::string& label) {
    std::remove(outLexPath.c_str());

    std::string source;
    if (!loadFileBinary(cituPath, source)) {
        ctx.fail(label + ": could not open sample file '" + cituPath + "' (run from repo root)");
        return;
    }

    Lexer lexer(source, cituPath);
    ctx.checkTrue(lexer.scanTokens(), label + ": Lexer failed: " + lexer.getErrorMessage());
    const std::vector<Token>& tokens = lexer.getTokens();
    ctx.checkEqual(tokens.size(), expectedTokenCount, label + ": Lexer token count");

    LexWriter writer;
    ctx.checkTrue(writer.write(outLexPath, cituPath, tokens),
                  label + ": LexWriter failed: " + writer.getErrorMessage());

    std::string rawLex;
    ctx.checkTrue(loadFileBinary(outLexPath, rawLex), label + ": failed to read generated .lex");
    ctx.checkEqual(rawLex.find('\r'), std::string::npos, label + ": .lex must contain zero CR bytes");
    ctx.checkTrue(!rawLex.empty() && rawLex.back() == '\n', label + ": .lex must end with LF");

    const std::vector<std::string> lines = splitLinesStrictLF(rawLex);
    // 3 headers + 1 comment + N token records + 1 @END_FILE = N + 5 lines
    ctx.checkEqual(lines.size(), expectedTokenCount + 5, label + ": exact line count (N + 5)");

    if (lines.size() == expectedTokenCount + 5) {
        ctx.checkEqual(lines[0], std::string("@FORMAT | ICC_LEX | v1.0"), label + ": line 1 @FORMAT");
        ctx.checkEqual(lines[1], std::string("@SOURCE | ") + cituPath, label + ": line 2 @SOURCE");
        ctx.checkEqual(lines[2], std::string("@TOTAL_TOKENS | ") + std::to_string(expectedTokenCount),
                       label + ": line 3 @TOTAL_TOKENS");
        ctx.checkEqual(lines[3],
                       std::string("# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL"),
                       label + ": line 4 column comment");
        ctx.checkEqual(lines[4], std::string("0 | 1 | 1 | struct | KEYWORD | struct | -1"),
                       label + ": token 0 exact serialized line");
        ctx.checkEqual(lines[5],
                       std::string("1 | 1 | 8 | ID | IDENTIFIER | ") + expectedStructName + " | -1",
                       label + ": token 1 exact serialized line");
        ctx.checkEqual(lines.back(), std::string("@END_FILE"), label + ": final line @END_FILE");

        for (std::size_t i = 0; i < expectedTokenCount; ++i) {
            const std::string& rec = lines[4 + i];
            ctx.checkEqual(countChar(rec, '|'), static_cast<std::size_t>(6),
                           label + ": record[" + std::to_string(i) + "] has exactly 6 pipes (7 fields)");
        }

        // Exact spot-check record at its verified token index
        ctx.checkEqual(lines[4 + spotCheckTokenIndex], expectedSpotCheckLine,
                       label + ": spot-check record at token index " + std::to_string(spotCheckTokenIndex));

        // Verify final token record ends with "$ | EOF_MARKER | $ | -1"
        const std::string& eofRec = lines[4 + expectedTokenCount - 1];
        const std::string eofSuffix = " | $ | EOF_MARKER | $ | -1";
        ctx.checkTrue(eofRec.size() >= eofSuffix.size() &&
                      eofRec.compare(eofRec.size() - eofSuffix.size(), eofSuffix.size(), eofSuffix) == 0,
                      label + ": final token record is EOF_MARKER");
    }

    std::remove(outLexPath.c_str());
}

void testApprovedSamplePrograms(TestContext& ctx) {
    verifySampleProgramLexFile(
        ctx,
        "examples/prog1_geometry.citu",
        "bin/test_prog1_geometry.lex",
        122,
        "Box",
        22,
        "22 | 8 | 10 | medalo | KEYWORD | medalo | -1",
        "prog1_geometry"
    );

    verifySampleProgramLexFile(
        ctx,
        "examples/prog2_payroll.citu",
        "bin/test_prog2_payroll.lex",
        141,
        "Employee",
        26,
        "26 | 9 | 17 | = | REL_OP | = | -1",
        "prog2_payroll"
    );

    verifySampleProgramLexFile(
        ctx,
        "examples/prog3_grades.citu",
        "bin/test_prog3_grades.lex",
        144,
        "Student",
        29,
        "29 | 10 | 28 | LT | CHAR_LITERAL | 'A' | 65",
        "prog3_grades"
    );
}

} // namespace

int main() {
    struct TestCase {
        std::string name;
        std::function<void(TestContext&)> fn;
    };

    const std::vector<TestCase> suite = {
        {"W1_Golden_MinReturn_Exact_Bytes",      testGoldenMinReturnExactBytes},
        {"W2_ClassA_Escaping_And_AtNone_Lexeme", testClassAEscapingAndAtNoneLexeme},
        {"W3_Empty_And_Sentinel_SourceFilename", testEmptyAndSentinelSourceFilename},
        {"W4_Invalid_Token_Vectors",             testInvalidTokenVectors},
        {"W5_OS_Write_Failure_And_Reuse",        testOsWriteFailureAndReuse},
        {"W6_Approved_Sample_Programs",          testApprovedSamplePrograms}
    };

    int passedCount = 0;
    int failedCount = 0;

    std::cout << "=== ICC Phase 2 LexWriter Runtime Test Suite ===\n\n";

    for (const TestCase& tc : suite) {
        TestContext ctx;
        tc.fn(ctx);

        if (ctx.passed) {
            ++passedCount;
            std::cout << "[PASS] " << tc.name << "\n";
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