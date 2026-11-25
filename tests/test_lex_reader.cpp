#include <cstdio>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "lex_reader.h"
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

struct ExpectedToken {
    int index;
    int line;
    int col;
    std::string terminal;
    std::string category;
    std::string lexeme;
    long long parsedIntVal;
};

bool writeRawFileBinary(const std::string& path, const std::string& rawBytes) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(rawBytes.data(), static_cast<std::streamsize>(rawBytes.size()));
    out.flush();
    return out.good();
}

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

void checkTokenEquals(TestContext& ctx,
                      const Token& actual,
                      const ExpectedToken& exp,
                      const std::string& label) {
    ctx.checkEqual(actual.index, exp.index, label + ".index");
    ctx.checkEqual(actual.line, exp.line, label + ".line");
    ctx.checkEqual(actual.col, exp.col, label + ".col");
    ctx.checkEqual(actual.terminal, exp.terminal, label + ".terminal");
    ctx.checkEqual(actual.category, exp.category, label + ".category");
    ctx.checkEqual(actual.lexeme, exp.lexeme, label + ".lexeme");
    ctx.checkEqual(actual.parsedIntVal, exp.parsedIntVal, label + ".parsedIntVal");
}

void expectReaderError(TestContext& ctx,
                       const std::string& rawLexContent,
                       const std::string& expectedCode,
                       int expectedLine,
                       const std::string& label) {
    const std::string fixturePath = "bin/test_lex_reader_fixture.lex";
    ctx.checkTrue(writeRawFileBinary(fixturePath, rawLexContent), label + ": write fixture");

    LexReader reader;
    const bool ok = reader.read(fixturePath);
    ctx.checkTrue(!ok, label + ": read() must return false");
    ctx.checkTrue(reader.hasError(), label + ": hasError() must be true");
    ctx.checkEqual(reader.getErrorCode(), expectedCode, label + ": errorCode");
    if (expectedLine > 0) {
        ctx.checkEqual(reader.getErrorLine(), expectedLine, label + ": errorLine");
    }
    ctx.checkTrue(!reader.getErrorMessage().empty(), label + ": errorMessage non-empty");
    ctx.checkTrue(reader.getTokens().empty(), label + ": failed read must expose empty tokens");
    ctx.checkTrue(reader.getSourceFilename().empty(), label + ": failed read must clear sourceFilename");

    std::remove(fixturePath.c_str());
}

// ============================================================================
// R1: Golden Valid File (Spec v1.4 Section 5.2 min_return.lex)
// ============================================================================

void testR1_GoldenValidFile(TestContext& ctx) {
    const std::string fixturePath = "bin/test_r1_min_return.lex";
    const std::string golden =
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

    ctx.checkTrue(writeRawFileBinary(fixturePath, golden), "R1: write golden fixture");

    LexReader reader;
    ctx.checkTrue(reader.read(fixturePath), "R1: read() must succeed (err: " + reader.getErrorMessage() + ")");
    ctx.checkTrue(!reader.hasError(), "R1: hasError() false");
    ctx.checkEqual(reader.getErrorCode(), std::string(""), "R1: errorCode empty");
    ctx.checkEqual(reader.getErrorLine(), 0, "R1: errorLine == 0");
    ctx.checkEqual(reader.getSourceFilename(), std::string("min_return.citu"), "R1: sourceFilename");

    const std::vector<ExpectedToken> expected = {
        {0, 1, 1,  "fn",    "KEYWORD",     "fn",    -1},
        {1, 1, 4,  "int32", "TYPE",        "int32", -1},
        {2, 1, 10, "main",  "KEYWORD",     "main",  -1},
        {3, 1, 15, "(",     "PUNCTUATION", "(",     -1},
        {4, 1, 17, ")",     "PUNCTUATION", ")",     -1},
        {5, 1, 19, "shro",  "KEYWORD",     "shro",  -1},
        {6, 2, 5,  "wapis", "KEYWORD",     "wapis", -1},
        {7, 2, 11, "LT",    "INT_LITERAL", "0",      0},
        {8, 3, 1,  "khtm",  "KEYWORD",     "khtm",  -1},
        {9, 3, 5,  "$",     "EOF_MARKER",  "$",     -1}
    };

    const std::vector<Token>& tokens = reader.getTokens();
    ctx.checkEqual(tokens.size(), expected.size(), "R1: token count");
    const std::size_t n = (tokens.size() < expected.size()) ? tokens.size() : expected.size();
    for (std::size_t i = 0; i < n; ++i) {
        checkTokenEquals(ctx, tokens[i], expected[i], "R1 token[" + std::to_string(i) + "]");
    }

    std::remove(fixturePath.c_str());
}

// ============================================================================
// R2: Class A Escaping (%25, %7C, %0A, %0D, Single-Pass %257C, Spaces)
// ============================================================================

void testR2_ClassAEscaping(TestContext& ctx) {
    const std::string fixturePath = "bin/test_r2_escaping.lex";
    // @SOURCE has meaningful leading and trailing spaces plus %257C (must become %7C, not |)
    // and %7C, %25, %0A, %0D.
    const std::string content =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE |  dir%257C%7C%25%0A%0Dname \n"
        "@TOTAL_TOKENS | 6\n"
        "0 | 1 | 1 | LT | CHAR_LITERAL | '%7C' | 124\n"
        "1 | 1 | 5 | LT | CHAR_LITERAL | '%25' | 37\n"
        "2 | 1 | 9 | LT | CHAR_LITERAL | ' ' | 32\n"
        "3 | 1 | 13 | LT | CHAR_LITERAL | '\\n' | 10\n"
        "4 | 1 | 18 | ID | IDENTIFIER |  a%257C%2525%7C%0A%0Db  | -1\n"
        "5 | 1 | 30 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    ctx.checkTrue(writeRawFileBinary(fixturePath, content), "R2: write fixture");

    LexReader reader;
    ctx.checkTrue(reader.read(fixturePath), "R2: read() must succeed (err: " + reader.getErrorMessage() + ")");
    ctx.checkEqual(reader.getSourceFilename(),
                   std::string(" dir%7C|%\n\rname "),
                   "R2: @SOURCE single-pass decode and edge-space preservation");

    const std::vector<Token>& tokens = reader.getTokens();
    ctx.checkEqual(tokens.size(), static_cast<std::size_t>(6), "R2: token count");
    if (tokens.size() == 6) {
        ctx.checkEqual(tokens[0].lexeme, std::string("'|'"), "R2: '%7C' -> '|'");
        ctx.checkEqual(tokens[0].parsedIntVal, 124LL, "R2: '|' ASCII 124");
        ctx.checkEqual(tokens[1].lexeme, std::string("'%'"), "R2: '%25' -> '%'");
        ctx.checkEqual(tokens[1].parsedIntVal, 37LL, "R2: '%' ASCII 37");
        ctx.checkEqual(tokens[2].lexeme, std::string("' '"), "R2: ' ' preserved");
        ctx.checkEqual(tokens[3].lexeme, std::string("'\\n'"), "R2: Citu backslash literal preserved verbatim");
        ctx.checkEqual(tokens[4].lexeme,
                       std::string(" a%7C%25|\n\rb "),
                       "R2: single-pass decode (%257C -> %7C, %2525 -> %25) and edge spaces preserved");
    }

    std::remove(fixturePath.c_str());
}

// ============================================================================
// R3: @NONE Sentinel Behavior (@SOURCE vs. Token LEXEME)
// ============================================================================

void testR3_AtNoneBehavior(TestContext& ctx) {
    const std::string fixturePath = "bin/test_r3_atnone.lex";
    const std::string content =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | @NONE\n"
        "@TOTAL_TOKENS | 2\n"
        "0 | 1 | 1 | ID | IDENTIFIER | @NONE | -1\n"
        "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    ctx.checkTrue(writeRawFileBinary(fixturePath, content), "R3: write fixture");

    LexReader reader;
    ctx.checkTrue(reader.read(fixturePath), "R3: read() must succeed (err: " + reader.getErrorMessage() + ")");
    ctx.checkEqual(reader.getSourceFilename(), std::string(""), "R3: @SOURCE | @NONE -> empty sourceFilename");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(2), "R3: token count");
    if (reader.getTokens().size() == 2) {
        ctx.checkEqual(reader.getTokens()[0].lexeme, std::string("@NONE"),
                       "R3: literal '@NONE' in LEXEME must be preserved as '@NONE'");
    }

    std::remove(fixturePath.c_str());
}

// ============================================================================
// R4: Valid Structural Variations (Blank Lines, Comments, CRLF, Zero-Pad Pipes)
// ============================================================================

void testR4_ValidStructuralVariations(TestContext& ctx) {
    // Part A: Blank lines, '#' comments everywhere, and CRLF line endings
    const std::string fixtureA = "bin/test_r4_crlf.lex";
    const std::string contentA =
        "\r\n"
        "# leading comment before @FORMAT\r\n"
        "@FORMAT | ICC_LEX | v1.0\r\n"
        "\r\n"
        "# comment between headers\r\n"
        "@SOURCE | crlf_test.citu\r\n"
        "@TOTAL_TOKENS | 3\r\n"
        "# column header comment\r\n"
        "\r\n"
        "0 | 1 | 1 | LT | INT_LITERAL | 4294967295 | 4294967295\r\n"
        "# comment between records\r\n"
        "1 | 2 | 1 | LT | CHAR_LITERAL | '\\0' | 0\r\n"
        "2 | 2 | 5 | $ | EOF_MARKER | $ | -1\r\n"
        "@END_FILE\r\n"
        "\r\n"
        "# trailing comment after @END_FILE\r\n";

    ctx.checkTrue(writeRawFileBinary(fixtureA, contentA), "R4A: write CRLF fixture");
    LexReader reader;
    ctx.checkTrue(reader.read(fixtureA), "R4A: CRLF + comments must succeed (err: " + reader.getErrorMessage() + ")");
    ctx.checkEqual(reader.getSourceFilename(), std::string("crlf_test.citu"), "R4A: sourceFilename");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(3), "R4A: token count");
    if (reader.getTokens().size() == 3) {
        ctx.checkEqual(reader.getTokens()[0].parsedIntVal, 4294967295LL, "R4A: UINT32_MAX");
        ctx.checkEqual(reader.getTokens()[1].parsedIntVal, 0LL, "R4A: CHAR_LITERAL 0");
    }
    std::remove(fixtureA.c_str());

    // Part B: Zero delimiter-padding spaces around '|' and no trailing newline after @END_FILE
    const std::string fixtureB = "bin/test_r4_zeropad.lex";
    const std::string contentB =
        "@FORMAT|ICC_LEX|v1.0\n"
        "@SOURCE|compact.citu\n"
        "@TOTAL_TOKENS|2\n"
        "0|1|1|fn|KEYWORD|fn|-1\n"
        "1|1|3|$|EOF_MARKER|$|-1\n"
        "@END_FILE";

    ctx.checkTrue(writeRawFileBinary(fixtureB, contentB), "R4B: write zero-pad fixture");
    ctx.checkTrue(reader.read(fixtureB), "R4B: zero-pad pipes must succeed (err: " + reader.getErrorMessage() + ")");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(2), "R4B: token count");
    if (reader.getTokens().size() == 2) {
        ctx.checkEqual(reader.getTokens()[0].terminal, std::string("fn"), "R4B: token 0 terminal");
    }
    std::remove(fixtureB.c_str());
}

// ============================================================================
// R5: Header, Footer, BOM, OS Open, and Count Failures
// ============================================================================

void testR5_HeaderFooterAndCountFailures(TestContext& ctx) {
    // Missing file (OS I/O error: empty errorCode, non-empty errorMessage)
    {
        LexReader reader;
        ctx.checkTrue(!reader.read("bin/definitely_no_such_file_xyz.lex"), "R5: missing file fails");
        ctx.checkTrue(reader.hasError(), "R5: missing file sets hasError");
        ctx.checkEqual(reader.getErrorCode(), std::string(""), "R5: OS open error leaves errorCode empty");
        ctx.checkTrue(!reader.getErrorMessage().empty(), "R5: OS open error sets message");
        ctx.checkTrue(reader.getTokens().empty(), "R5: missing file leaves tokens empty");
    }

    // Empty file
    expectReaderError(ctx, "", "FATAL_IO_FORMAT_MISMATCH", 0, "R5_empty_file");

    // UTF-8 BOM at beginning of file (before @FORMAT and before '#' comment)
    expectReaderError(ctx,
                      std::string("\xEF\xBB\xBF", 3) +
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 1, "R5_utf8_bom_format");
    expectReaderError(ctx,
                      std::string("\xEF\xBB\xBF", 3) + "# comment\n@FORMAT | ICC_LEX | v1.0\n",
                      "FATAL_IO_FORMAT_MISMATCH", 1, "R5_utf8_bom_comment");

    // Wrong format tag and version
    expectReaderError(ctx,
                      "@FORMAT | ICC_AST | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 1, "R5_wrong_tag");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v2.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 1, "R5_wrong_version");

    // Missing / swapped / duplicated headers & empty @SOURCE
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 2, "R5_missing_source");
    expectReaderError(ctx,
                      "@SOURCE | a.citu\n@FORMAT | ICC_LEX | v1.0\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 1, "R5_swapped_headers");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@SOURCE | b.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 3, "R5_duplicate_source");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | \n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 2, "R5_empty_source_field");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n",
                      "FATAL_IO_FORMAT_MISMATCH", 2, "R5_missing_total_tokens_eof");

    // Malformed, zero, and negative @TOTAL_TOKENS
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | abc\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 3, "R5_malformed_total");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | +1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 3, "R5_plus_sign_total");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 0\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 3, "R5_zero_total");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | -2\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 3, "R5_negative_total");

    // Too few records and too many records
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 2\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_COUNT_MISMATCH", 5, "R5_too_few_records");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_COUNT_MISMATCH", 5, "R5_too_many_records");

    // Missing @END_FILE and unexpected data after @END_FILE
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n",
                      "FATAL_IO_MISSING_END_FILE", 4, "R5_missing_end_file");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n"
                      "trailing garbage\n",
                      "FATAL_IO_FORMAT_MISMATCH", 6, "R5_data_after_end_file");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n"
                      "   \n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R5_spaces_only_line");
}

// ============================================================================
// R6: Record Structure, Numeric Parsing, Indices & Coordinate Failures
// ============================================================================

void testR6_RecordStructureAndNumericFailures(TestContext& ctx) {
    const std::string hdr1 = "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n";
    const std::string hdr2 = "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 2\n";

    // Wrong field counts (6 fields, 8 fields, trailing pipe)
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | EOF_MARKER | $\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_six_fields");
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | EOF_MARKER | $ | -1 | extra\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_eight_fields");
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | EOF_MARKER | $ | -1 |\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_trailing_pipe");

    // Malformed integer fields (non-digit, leading '+', extra space, lone '-', overflow)
    expectReaderError(ctx, hdr1 + "0a | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_index_nondigit");
    expectReaderError(ctx, hdr1 + "0 | +1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_line_plus_sign");
    expectReaderError(ctx, hdr1 + "0 |  1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_line_extra_space");
    expectReaderError(ctx, hdr1 + "0 | 1 | - | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_col_lone_minus");
    expectReaderError(ctx, hdr1 + "99999999999999999999999 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_index_overflow");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | INT_LITERAL | 99 | 99999999999999999999999\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R6_parsed_val_int64_overflow");

    // Invalid token indices (start at 1, gap, duplicate)
    expectReaderError(ctx, hdr1 + "1 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R6_index_starts_at_1");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
                      "2 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 5, "R6_index_gap");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
                      "0 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 5, "R6_index_duplicate");

    // Invalid coordinates (line 0, col 0, decreasing line, equal/decreasing col on same line)
    expectReaderError(ctx, hdr1 + "0 | 0 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R6_line_zero");
    expectReaderError(ctx, hdr1 + "0 | 1 | 0 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R6_col_zero");
    expectReaderError(ctx, hdr2 +
                      "0 | 2 | 1 | fn | KEYWORD | fn | -1\n"
                      "1 | 1 | 5 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 5, "R6_decreasing_line");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 5 | fn | KEYWORD | fn | -1\n"
                      "1 | 1 | 5 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 5, "R6_equal_col_same_line");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 5 | fn | KEYWORD | fn | -1\n"
                      "1 | 1 | 4 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 5, "R6_decreasing_col_same_line");
}

// ============================================================================
// R7: EOF and Literal Invariants
// ============================================================================

void testR7_EofAndLiteralInvariants(TestContext& ctx) {
    const std::string hdr1 = "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 1\n";
    const std::string hdr2 = "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 2\n";

    // Missing EOF, premature EOF, duplicate EOF, malformed EOF fields
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | khtm | KEYWORD | khtm | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_missing_eof");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n"
                      "1 | 1 | 2 | khtm | KEYWORD | khtm | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_premature_eof");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n"
                      "1 | 1 | 2 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_duplicate_eof");
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | EOF_MARKER | EOF | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_eof_wrong_lexeme");
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | EOF_MARKER | $ | 0\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_eof_wrong_val");
    expectReaderError(ctx, hdr1 + "0 | 1 | 1 | $ | KEYWORD | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_eof_wrong_category");

    // Literal categories & value ranges
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | KEYWORD | 10 | 10\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_lt_invalid_category");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | ID | INT_LITERAL | 10 | 10\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_non_lt_literal_category");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | INT_LITERAL | 4294967296 | 4294967296\n"
                      "1 | 1 | 11 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_int_literal_too_large");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | INT_LITERAL | 0 | -1\n"
                      "1 | 1 | 2 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_int_literal_negative");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | CHAR_LITERAL | 'A' | 128\n"
                      "1 | 1 | 4 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_char_literal_above_127");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | CHAR_LITERAL | 'A' | -1\n"
                      "1 | 1 | 4 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_char_literal_negative");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn | KEYWORD | fn | 0\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R7_non_lt_nonzero_val");
}

// ============================================================================
// R8: Escape and Field-Class Failures
// ============================================================================

void testR8_EscapeAndFieldClassFailures(TestContext& ctx) {
    const std::string hdr2 = "@FORMAT | ICC_LEX | v1.0\n@SOURCE | a.citu\n@TOTAL_TOKENS | 2\n";

    // Malformed percent-escapes in LEXEME and @SOURCE
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | CHAR_LITERAL | '%7c' | 124\n"
                      "1 | 1 | 5 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_MALFORMED_ESCAPE", 4, "R8_lowercase_escape_lexeme");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | LT | CHAR_LITERAL | '%20' | 32\n"
                      "1 | 1 | 5 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_MALFORMED_ESCAPE", 4, "R8_unknown_escape_lexeme");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | ID | IDENTIFIER | abc%2\n"
                      "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R8_six_fields_sanity");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | ID | IDENTIFIER | abc%2 | -1\n"
                      "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_MALFORMED_ESCAPE", 4, "R8_incomplete_escape_lexeme");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | ID | IDENTIFIER | abc% | -1\n"
                      "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_MALFORMED_ESCAPE", 4, "R8_trailing_percent_lexeme");
    expectReaderError(ctx,
                      "@FORMAT | ICC_LEX | v1.0\n@SOURCE | bad%0a.citu\n@TOTAL_TOKENS | 1\n"
                      "0 | 1 | 1 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_MALFORMED_ESCAPE", 2, "R8_lowercase_escape_source");

    // Invalid raw Class B fields (invalid chars, extra edge spaces, @NONE in Class B)
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn@bad | KEYWORD | fn | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R8_illegal_char_in_terminal");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn | KEY,WORD | fn | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R8_comma_in_category");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | fn  | KEYWORD | fn | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R8_extra_space_in_terminal");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | @NONE | KEYWORD | fn | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_FORMAT_MISMATCH", 4, "R8_atnone_in_terminal");

    // Valid Class B syntax but illegal terminal/category or empty lexeme
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | int32 | KEYWORD | int32 | -1\n"
                      "1 | 1 | 6 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R8_mismatched_terminal_category");
    expectReaderError(ctx, hdr2 +
                      "0 | 1 | 1 | ID | IDENTIFIER |  | -1\n"
                      "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n@END_FILE\n",
                      "FATAL_IO_INVARIANT_VIOLATION", 4, "R8_empty_lexeme");
}

// ============================================================================
// R9: Approved Sample Programs (Full Pipeline Round-Trip Verification)
// ============================================================================

void verifySampleRoundTrip(TestContext& ctx,
                           const std::string& cituPath,
                           const std::string& tempLexPath,
                           std::size_t expectedTokenCount,
                           const std::string& label) {
    std::remove(tempLexPath.c_str());

    std::string source;
    if (!loadFileBinary(cituPath, source)) {
        ctx.fail(label + ": could not open sample file '" + cituPath + "' (run from repo root)");
        return;
    }

    Lexer lexer(source, cituPath);
    ctx.checkTrue(lexer.scanTokens(), label + ": Lexer failed: " + lexer.getErrorMessage());
    const std::vector<Token>& originalTokens = lexer.getTokens();
    ctx.checkEqual(originalTokens.size(), expectedTokenCount, label + ": original token count");

    LexWriter writer;
    ctx.checkTrue(writer.write(tempLexPath, cituPath, originalTokens),
                  label + ": LexWriter failed: " + writer.getErrorMessage());

    LexReader reader;
    ctx.checkTrue(reader.read(tempLexPath),
                  label + ": LexReader failed: " + reader.getErrorMessage());
    ctx.checkTrue(!reader.hasError(), label + ": reader hasError false");
    ctx.checkEqual(reader.getSourceFilename(), cituPath, label + ": reconstructed sourceFilename");

    const std::vector<Token>& readTokens = reader.getTokens();
    ctx.checkEqual(readTokens.size(), originalTokens.size(), label + ": reconstructed token count");

    const std::size_t n = (originalTokens.size() < readTokens.size()) ? originalTokens.size() : readTokens.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Token& orig = originalTokens[i];
        const Token& rec = readTokens[i];
        const std::string prefix = label + " token[" + std::to_string(i) + "]";
        ctx.checkEqual(rec.index, orig.index, prefix + ".index");
        ctx.checkEqual(rec.line, orig.line, prefix + ".line");
        ctx.checkEqual(rec.col, orig.col, prefix + ".col");
        ctx.checkEqual(rec.terminal, orig.terminal, prefix + ".terminal");
        ctx.checkEqual(rec.category, orig.category, prefix + ".category");
        ctx.checkEqual(rec.lexeme, orig.lexeme, prefix + ".lexeme");
        ctx.checkEqual(rec.parsedIntVal, orig.parsedIntVal, prefix + ".parsedIntVal");
    }

    std::remove(tempLexPath.c_str());
}

void testR9_ApprovedSamplePrograms(TestContext& ctx) {
    verifySampleRoundTrip(ctx, "examples/prog1_geometry.citu", "bin/test_r9_prog1.lex", 122, "R9_prog1_geometry");
    verifySampleRoundTrip(ctx, "examples/prog2_payroll.citu",  "bin/test_r9_prog2.lex", 141, "R9_prog2_payroll");
    verifySampleRoundTrip(ctx, "examples/prog3_grades.citu",   "bin/test_r9_prog3.lex", 144, "R9_prog3_grades");
}

// ============================================================================
// R10: Repeated Reads & Failure Recovery on the Same LexReader Instance
// ============================================================================

void testR10_RepeatedReadsAndRecovery(TestContext& ctx) {
    const std::string fileA = "bin/test_r10_a.lex";
    const std::string fileB = "bin/test_r10_b.lex";
    const std::string fileBad = "bin/test_r10_bad.lex";

    const std::string contentA =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | first.citu\n"
        "@TOTAL_TOKENS | 2\n"
        "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
        "1 | 1 | 3 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    const std::string contentB =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | second.citu\n"
        "@TOTAL_TOKENS | 3\n"
        "0 | 1 | 1 | int32 | TYPE | int32 | -1\n"
        "1 | 1 | 7 | ID | IDENTIFIER | x | -1\n"
        "2 | 1 | 8 | $ | EOF_MARKER | $ | -1\n"
        "@END_FILE\n";

    const std::string contentBad =
        "@FORMAT | ICC_LEX | v1.0\n"
        "@SOURCE | broken.citu\n"
        "@TOTAL_TOKENS | 1\n"
        "0 | 1 | 1 | fn | KEYWORD | fn | -1\n"
        "@END_FILE\n";

    ctx.checkTrue(writeRawFileBinary(fileA, contentA), "R10: write fileA");
    ctx.checkTrue(writeRawFileBinary(fileB, contentB), "R10: write fileB");
    ctx.checkTrue(writeRawFileBinary(fileBad, contentBad), "R10: write fileBad");

    LexReader reader;

    // 1. Valid read A
    ctx.checkTrue(reader.read(fileA), "R10 step 1: valid read A");
    ctx.checkEqual(reader.getSourceFilename(), std::string("first.citu"), "R10 step 1: source A");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(2), "R10 step 1: count A");

    // 2. Valid read B (replaces A completely)
    ctx.checkTrue(reader.read(fileB), "R10 step 2: valid read B");
    ctx.checkTrue(!reader.hasError(), "R10 step 2: no error");
    ctx.checkEqual(reader.getSourceFilename(), std::string("second.citu"), "R10 step 2: source B");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(3), "R10 step 2: count B");
    if (reader.getTokens().size() == 3) {
        ctx.checkEqual(reader.getTokens()[1].lexeme, std::string("x"), "R10 step 2: token 1 is 'x'");
    }

    // 3. Invalid read after valid read (must wipe B completely)
    ctx.checkTrue(!reader.read(fileBad), "R10 step 3: invalid read fails");
    ctx.checkTrue(reader.hasError(), "R10 step 3: hasError true");
    ctx.checkEqual(reader.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), "R10 step 3: error code");
    ctx.checkEqual(reader.getErrorLine(), 4, "R10 step 3: error line 4");
    ctx.checkTrue(reader.getTokens().empty(), "R10 step 3: tokens cleared");
    ctx.checkTrue(reader.getSourceFilename().empty(), "R10 step 3: sourceFilename cleared");

    // 4. Valid read A after invalid read (must clear all diagnostic state)
    ctx.checkTrue(reader.read(fileA), "R10 step 4: recovery read A succeeds");
    ctx.checkTrue(!reader.hasError(), "R10 step 4: hasError reset to false");
    ctx.checkEqual(reader.getErrorCode(), std::string(""), "R10 step 4: errorCode cleared");
    ctx.checkEqual(reader.getErrorMessage(), std::string(""), "R10 step 4: errorMessage cleared");
    ctx.checkEqual(reader.getErrorLine(), 0, "R10 step 4: errorLine reset to 0");
    ctx.checkEqual(reader.getSourceFilename(), std::string("first.citu"), "R10 step 4: source A restored");
    ctx.checkEqual(reader.getTokens().size(), static_cast<std::size_t>(2), "R10 step 4: count A restored");

    std::remove(fileA.c_str());
    std::remove(fileB.c_str());
    std::remove(fileBad.c_str());
}

} // namespace

int main() {
    struct TestCase {
        std::string name;
        std::function<void(TestContext&)> fn;
    };

    const std::vector<TestCase> suite = {
        {"R1_Golden_Valid_File",                 testR1_GoldenValidFile},
        {"R2_ClassA_Escaping",                   testR2_ClassAEscaping},
        {"R3_AtNone_Behavior",                   testR3_AtNoneBehavior},
        {"R4_Valid_Structural_Variations",       testR4_ValidStructuralVariations},
        {"R5_Header_Footer_And_Count_Failures",  testR5_HeaderFooterAndCountFailures},
        {"R6_Record_Structure_And_Numeric",      testR6_RecordStructureAndNumericFailures},
        {"R7_EOF_And_Literal_Invariants",        testR7_EofAndLiteralInvariants},
        {"R8_Escape_And_FieldClass_Failures",    testR8_EscapeAndFieldClassFailures},
        {"R9_Approved_Sample_Programs",          testR9_ApprovedSamplePrograms},
        {"R10_Repeated_Reads_And_Recovery",      testR10_RepeatedReadsAndRecovery}
    };

    int passedCount = 0;
    int failedCount = 0;

    std::cout << "=== ICC Phase 2 LexReader Runtime Test Suite ===\n\n";

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