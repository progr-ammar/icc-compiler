#include <cstddef>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "lexer.h"
#include "token.h"

namespace {

// ============================================================================
// Lightweight Unit-Test Harness (Non-Aborting Assertions with PASS/FAIL Report)
// ============================================================================

struct TestContext {
    bool passed = true;
    std::vector<std::string> messages;

    void fail(const std::string& msg) {
        passed = false;
        messages.push_back(msg);
    }

    void checkTrue(bool condition, const std::string& description) {
        if (!condition) {
            fail("Expected true: " + description);
        }
    }

    template <typename T>
    void checkEqual(const T& actual, const T& expected, const std::string& description) {
        if (!(actual == expected)) {
            std::ostringstream oss;
            oss << description << " | expected: [" << expected << "] actual: [" << actual << "]";
            fail(oss.str());
        }
    }
};

struct ExpectedToken {
    int line;
    int col;
    std::string terminal;
    std::string category;
    std::string lexeme;
    long long parsedIntVal;
};

// Verifies universal structural invariants on any successfully scanned token stream
// (Specification v1.4 Section 5.2 LexReader invariants).
void checkStreamInvariants(TestContext& ctx, const std::vector<Token>& tokens, const std::string& label) {
    ctx.checkTrue(!tokens.empty(), label + ": token list must contain at least EOF");
    if (tokens.empty()) {
        return;
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        ctx.checkEqual(tok.index, static_cast<int>(i),
                       label + ": token[" + std::to_string(i) + "] contiguous index");
        ctx.checkTrue(tok.line >= 1,
                      label + ": token[" + std::to_string(i) + "] line >= 1");
        ctx.checkTrue(tok.col >= 1,
                      label + ": token[" + std::to_string(i) + "] col >= 1");
        ctx.checkTrue(!tok.lexeme.empty(),
                      label + ": token[" + std::to_string(i) + "] lexeme non-empty");

        // Lexicographical (line, col) ordering across consecutive tokens
        if (i > 0) {
            const Token& prev = tokens[i - 1];
            const bool ordered = (tok.line > prev.line) ||
                                 (tok.line == prev.line && tok.col > prev.col);
            ctx.checkTrue(ordered,
                          label + ": token[" + std::to_string(i) + "] lexicographical (line, col) order");
        }

        // Exactly one EOF token, strictly at the final position
        if (i + 1 < tokens.size()) {
            ctx.checkTrue(tok.terminal != "$" && tok.category != "EOF_MARKER",
                          label + ": non-final token[" + std::to_string(i) + "] must not be EOF");
        } else {
            ctx.checkEqual(tok.terminal, std::string("$"), label + ": final token terminal == '$'");
            ctx.checkEqual(tok.category, std::string("EOF_MARKER"), label + ": final token category == 'EOF_MARKER'");
            ctx.checkEqual(tok.lexeme, std::string("$"), label + ": final token lexeme == '$'");
            ctx.checkEqual(tok.parsedIntVal, -1LL, label + ": final token parsedIntVal == -1");
        }

        // Literal parsedIntVal bounds consistency (Specification v1.4 Section 5.2)
        if (tok.terminal == "LT") {
            const bool validCat = (tok.category == "INT_LITERAL" || tok.category == "CHAR_LITERAL");
            ctx.checkTrue(validCat, label + ": LT token[" + std::to_string(i) + "] category valid");
            if (tok.category == "INT_LITERAL") {
                ctx.checkTrue(tok.parsedIntVal >= 0 && tok.parsedIntVal <= 4294967295LL,
                              label + ": INT_LITERAL[" + std::to_string(i) + "] range [0, 4294967295]");
            } else if (tok.category == "CHAR_LITERAL") {
                ctx.checkTrue(tok.parsedIntVal >= 0 && tok.parsedIntVal <= 127LL,
                              label + ": CHAR_LITERAL[" + std::to_string(i) + "] ASCII range [0, 127]");
            }
        } else {
            ctx.checkEqual(tok.parsedIntVal, -1LL,
                           label + ": non-LT token[" + std::to_string(i) + "] parsedIntVal == -1");
        }
    }
}

void checkTokenMatches(TestContext& ctx,
                       const Token& actual,
                       const ExpectedToken& expected,
                       int expectedIndex,
                       const std::string& label) {
    const std::string prefix = label + " token[" + std::to_string(expectedIndex) + "]";
    ctx.checkEqual(actual.index, expectedIndex, prefix + ".index");
    ctx.checkEqual(actual.line, expected.line, prefix + ".line");
    ctx.checkEqual(actual.col, expected.col, prefix + ".col");
    ctx.checkEqual(actual.terminal, expected.terminal, prefix + ".terminal");
    ctx.checkEqual(actual.category, expected.category, prefix + ".category");
    ctx.checkEqual(actual.lexeme, expected.lexeme, prefix + ".lexeme");
    ctx.checkEqual(actual.parsedIntVal, expected.parsedIntVal, prefix + ".parsedIntVal");
}

void expectValidTokens(TestContext& ctx,
                       const std::string& source,
                       const std::vector<ExpectedToken>& expected,
                       const std::string& label) {
    Lexer lexer(source, label + ".citu");
    const bool ok = lexer.scanTokens();
    ctx.checkTrue(ok, label + ": scanTokens() should succeed (err: " + lexer.getErrorMessage() + ")");
    ctx.checkTrue(!lexer.hasError(), label + ": hasError() should be false");
    ctx.checkEqual(lexer.getErrorCode(), std::string(""), label + ": errorCode should be empty");

    const std::vector<Token>& tokens = lexer.getTokens();
    checkStreamInvariants(ctx, tokens, label);
    ctx.checkEqual(tokens.size(), expected.size(), label + ": total token count");

    const std::size_t limit = (tokens.size() < expected.size()) ? tokens.size() : expected.size();
    for (std::size_t i = 0; i < limit; ++i) {
        checkTokenMatches(ctx, tokens[i], expected[i], static_cast<int>(i), label);
    }
}

void expectLexicalError(TestContext& ctx,
                        const std::string& source,
                        const std::string& expectedCode,
                        int expectedLine,
                        int expectedCol,
                        const std::string& label) {
    Lexer lexer(source, label + ".citu");
    const bool ok = lexer.scanTokens();
    ctx.checkTrue(!ok, label + ": scanTokens() must return false");
    ctx.checkTrue(lexer.hasError(), label + ": hasError() must be true");
    ctx.checkTrue(lexer.getTokens().empty(), label + ": failed scan must clear getTokens() (no partial tokens or EOF)");
    ctx.checkEqual(lexer.getErrorCode(), expectedCode, label + ": diagnostic error code");
    ctx.checkEqual(lexer.getErrorLine(), expectedLine, label + ": error starting line");
    ctx.checkEqual(lexer.getErrorCol(), expectedCol, label + ": error starting col");
    ctx.checkTrue(!lexer.getErrorMessage().empty(), label + ": errorMessage must be non-empty");
}

bool loadFileBinary(const std::string& path, std::string& outContent) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    outContent = buffer.str();
    return true;
}

// Computes exact EOF (line, col) directly from raw file bytes per Spec v1.4 Section 3.1 Rule 8.
std::pair<int, int> computeExpectedEofPosition(const std::string& content) {
    int line = 1;
    int col = 1;
    for (char c : content) {
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    return {line, col};
}

// ============================================================================
// Category A: Basic Token Recognition
// ============================================================================

void testCategoryA_KeywordsAndTypes(TestContext& ctx) {
    const std::string src =
        "struct fn main khali wapis shro khtm jabtak agar magar medalo "
        "int16 int32 uint16 uint32 char";

    const std::vector<std::pair<std::string, std::string>> expectedWords = {
        {"struct", "KEYWORD"}, {"fn", "KEYWORD"}, {"main", "KEYWORD"},
        {"khali", "KEYWORD"}, {"wapis", "KEYWORD"}, {"shro", "KEYWORD"},
        {"khtm", "KEYWORD"}, {"jabtak", "KEYWORD"}, {"agar", "KEYWORD"},
        {"magar", "KEYWORD"}, {"medalo", "KEYWORD"},
        {"int16", "TYPE"}, {"int32", "TYPE"}, {"uint16", "TYPE"},
        {"uint32", "TYPE"}, {"char", "TYPE"}
    };

    Lexer lexer(src, "keywords_types.citu");
    ctx.checkTrue(lexer.scanTokens(), "All 16 keywords and types must scan cleanly");
    const auto& tokens = lexer.getTokens();
    checkStreamInvariants(ctx, tokens, "keywords_types");
    ctx.checkEqual(tokens.size(), expectedWords.size() + 1, "16 keywords/types + 1 EOF");

    if (tokens.size() == expectedWords.size() + 1) {
        for (std::size_t i = 0; i < expectedWords.size(); ++i) {
            ctx.checkEqual(tokens[i].terminal, expectedWords[i].first, "word terminal " + expectedWords[i].first);
            ctx.checkEqual(tokens[i].category, expectedWords[i].second, "word category " + expectedWords[i].first);
            ctx.checkEqual(tokens[i].lexeme, expectedWords[i].first, "word lexeme " + expectedWords[i].first);
            ctx.checkEqual(tokens[i].parsedIntVal, -1LL, "word parsedIntVal " + expectedWords[i].first);
        }
    }
}

void testCategoryA_ForbiddenKeywords(TestContext& ctx) {
    expectLexicalError(ctx, "class Box", "LEXICAL_ERROR_FORBIDDEN_CONSTRUCT", 1, 1, "forbidden_class");
    expectLexicalError(ctx, "int32 x\n    do shro", "LEXICAL_ERROR_FORBIDDEN_CONSTRUCT", 2, 5, "forbidden_do");
}

void testCategoryA_IdentifiersAndCaseSensitivity(TestContext& ctx) {
    const std::string src = "score _tmp_1 Main Class DO Int32 Agar";
    expectValidTokens(ctx, src, {
        {1, 1,  "ID", "IDENTIFIER", "score",  -1},
        {1, 7,  "ID", "IDENTIFIER", "_tmp_1", -1},
        {1, 14, "ID", "IDENTIFIER", "Main",   -1},
        {1, 19, "ID", "IDENTIFIER", "Class",  -1},
        {1, 25, "ID", "IDENTIFIER", "DO",     -1},
        {1, 28, "ID", "IDENTIFIER", "Int32",  -1},
        {1, 34, "ID", "IDENTIFIER", "Agar",   -1},
        {1, 38, "$",  "EOF_MARKER", "$",      -1}
    }, "identifiers_case_sensitivity");
}

void testCategoryA_OperatorsPunctuationAndLiterals(TestContext& ctx) {
    const std::string src = "+ - * / < > = != <= >= ( ) , . 42 'Z'";
    expectValidTokens(ctx, src, {
        {1, 1,  "+",  "ARITH_OP",     "+",   -1},
        {1, 3,  "-",  "ARITH_OP",     "-",   -1},
        {1, 5,  "*",  "ARITH_OP",     "*",   -1},
        {1, 7,  "/",  "ARITH_OP",     "/",   -1},
        {1, 9,  "<",  "REL_OP",       "<",   -1},
        {1, 11, ">",  "REL_OP",       ">",   -1},
        {1, 13, "=",  "REL_OP",       "=",   -1},
        {1, 15, "!=", "REL_OP",       "!=",  -1},
        {1, 18, "<=", "REL_OP",       "<=",  -1},
        {1, 21, ">=", "REL_OP",       ">=",  -1},
        {1, 24, "(",  "PUNCTUATION",  "(",   -1},
        {1, 26, ")",  "PUNCTUATION",  ")",   -1},
        {1, 28, ",",  "PUNCTUATION",  ",",   -1},
        {1, 30, ".",  "PUNCTUATION",  ".",   -1},
        {1, 32, "LT", "INT_LITERAL",  "42",  42},
        {1, 35, "LT", "CHAR_LITERAL", "'Z'", 90},
        {1, 38, "$",  "EOF_MARKER",   "$",   -1}
    }, "ops_punct_literals");
}

// ============================================================================
// Category B: Maximal Munch & Adjacent Token Separation
// ============================================================================

void testCategoryB_MaximalMunchWords(TestContext& ctx) {
    const std::string src = "jabtak jabtakCount classRoom my_class doWork todo int32_val main_fn";
    expectValidTokens(ctx, src, {
        {1, 1,  "jabtak", "KEYWORD",    "jabtak",      -1},
        {1, 8,  "ID",     "IDENTIFIER", "jabtakCount", -1},
        {1, 20, "ID",     "IDENTIFIER", "classRoom",   -1},
        {1, 30, "ID",     "IDENTIFIER", "my_class",    -1},
        {1, 39, "ID",     "IDENTIFIER", "doWork",      -1},
        {1, 46, "ID",     "IDENTIFIER", "todo",        -1},
        {1, 51, "ID",     "IDENTIFIER", "int32_val",   -1},
        {1, 61, "ID",     "IDENTIFIER", "main_fn",     -1},
        {1, 68, "$",      "EOF_MARKER", "$",           -1}
    }, "maximal_munch_words");
}

void testCategoryB_MaximalMunchAdjacentTokens(TestContext& ctx) {
    const std::string src = "a-10 myBox.width calc(a,b) x<=y m>=n p!=q r=s 10+20*30/40";
    expectValidTokens(ctx, src, {
        {1, 1,  "ID", "IDENTIFIER",  "a",     -1},
        {1, 2,  "-",  "ARITH_OP",    "-",     -1},
        {1, 3,  "LT", "INT_LITERAL", "10",    10},
        {1, 6,  "ID", "IDENTIFIER",  "myBox", -1},
        {1, 11, ".",  "PUNCTUATION", ".",     -1},
        {1, 12, "ID", "IDENTIFIER",  "width", -1},
        {1, 18, "ID", "IDENTIFIER",  "calc",  -1},
        {1, 22, "(",  "PUNCTUATION", "(",     -1},
        {1, 23, "ID", "IDENTIFIER",  "a",     -1},
        {1, 24, ",",  "PUNCTUATION", ",",     -1},
        {1, 25, "ID", "IDENTIFIER",  "b",     -1},
        {1, 26, ")",  "PUNCTUATION", ")",     -1},
        {1, 28, "ID", "IDENTIFIER",  "x",     -1},
        {1, 29, "<=", "REL_OP",      "<=",    -1},
        {1, 31, "ID", "IDENTIFIER",  "y",     -1},
        {1, 33, "ID", "IDENTIFIER",  "m",     -1},
        {1, 34, ">=", "REL_OP",      ">=",    -1},
        {1, 36, "ID", "IDENTIFIER",  "n",     -1},
        {1, 38, "ID", "IDENTIFIER",  "p",     -1},
        {1, 39, "!=", "REL_OP",      "!=",    -1},
        {1, 41, "ID", "IDENTIFIER",  "q",     -1},
        {1, 43, "ID", "IDENTIFIER",  "r",     -1},
        {1, 44, "=",  "REL_OP",      "=",     -1},
        {1, 45, "ID", "IDENTIFIER",  "s",     -1},
        {1, 47, "LT", "INT_LITERAL", "10",    10},
        {1, 49, "+",  "ARITH_OP",    "+",     -1},
        {1, 50, "LT", "INT_LITERAL", "20",    20},
        {1, 52, "*",  "ARITH_OP",    "*",     -1},
        {1, 53, "LT", "INT_LITERAL", "30",    30},
        {1, 55, "/",  "ARITH_OP",    "/",     -1},
        {1, 56, "LT", "INT_LITERAL", "40",    40},
        {1, 58, "$",  "EOF_MARKER",  "$",     -1}
    }, "maximal_munch_adjacent");
}

// ============================================================================
// Category C: Integer Literals, Boundaries & Error Precedence
// ============================================================================

void testCategoryC_ValidIntegers(TestContext& ctx) {
    const std::string src = "0 00 007 32767 65535 2147483647 4294967295";
    expectValidTokens(ctx, src, {
        {1, 1,  "LT", "INT_LITERAL", "0",          0LL},
        {1, 3,  "LT", "INT_LITERAL", "00",         0LL},
        {1, 6,  "LT", "INT_LITERAL", "007",        7LL},
        {1, 10, "LT", "INT_LITERAL", "32767",      32767LL},
        {1, 16, "LT", "INT_LITERAL", "65535",      65535LL},
        {1, 22, "LT", "INT_LITERAL", "2147483647", 2147483647LL},
        {1, 33, "LT", "INT_LITERAL", "4294967295", 4294967295LL},
        {1, 43, "$",  "EOF_MARKER",  "$",          -1}
    }, "valid_integers");
}

void testCategoryC_IntegerOverflowAndMalformedPrecedence(TestContext& ctx) {
    // Pure overflow (UINT32_MAX + 1 and 30-digit integer)
    expectLexicalError(ctx, "uint32 n\n    n medalo 4294967296",
                       "LEXICAL_ERROR_INTEGER_TOO_LARGE", 2, 14, "uint32_max_plus_1");
    expectLexicalError(ctx, "999999999999999999999999999999",
                       "LEXICAL_ERROR_INTEGER_TOO_LARGE", 1, 1, "very_long_digits_overflow");

    // Standard malformed numbers
    expectLexicalError(ctx, "123abc", "LEXICAL_ERROR_MALFORMED_NUMBER", 1, 1, "malformed_123abc");
    expectLexicalError(ctx, "x medalo 3.14", "LEXICAL_ERROR_MALFORMED_NUMBER", 1, 10, "malformed_float_3_14");
    expectLexicalError(ctx, "0_1", "LEXICAL_ERROR_MALFORMED_NUMBER", 1, 1, "malformed_0_1");

    // Malformed-number priority over integer-too-large
    expectLexicalError(ctx, "int32 x\n    x medalo 4294967296abc",
                       "LEXICAL_ERROR_MALFORMED_NUMBER", 2, 14, "priority_4294967296abc");
    expectLexicalError(ctx, "999999999999999999999999999999abc",
                       "LEXICAL_ERROR_MALFORMED_NUMBER", 1, 1, "priority_30digits_abc");
    expectLexicalError(ctx, "4294967296.5",
                       "LEXICAL_ERROR_MALFORMED_NUMBER", 1, 1, "priority_overflow_dot");
}

// ============================================================================
// Category D: Character Literals & Escape Sequences
// ============================================================================

void testCategoryD_ValidCharactersAndEscapes(TestContext& ctx) {
    // All 5 valid escapes + printable ASCII boundaries: 32 (' '), 65 ('A'), 124 ('|'), 126 ('~')
    const std::string src = "'\\n' '\\t' '\\0' '\\\\' '\\'' ' ' 'A' '|' '~'";
    expectValidTokens(ctx, src, {
        {1, 1,  "LT", "CHAR_LITERAL", "'\\n'",  10},
        {1, 6,  "LT", "CHAR_LITERAL", "'\\t'",  9},
        {1, 11, "LT", "CHAR_LITERAL", "'\\0'",  0},
        {1, 16, "LT", "CHAR_LITERAL", "'\\\\'", 92},
        {1, 21, "LT", "CHAR_LITERAL", "'\\''",  39},
        {1, 26, "LT", "CHAR_LITERAL", "' '",    32},
        {1, 30, "LT", "CHAR_LITERAL", "'A'",    65},
        {1, 34, "LT", "CHAR_LITERAL", "'|'",    124},
        {1, 38, "LT", "CHAR_LITERAL", "'~'",    126},
        {1, 41, "$",  "EOF_MARKER",   "$",      -1}
    }, "valid_chars_and_escapes");
}

void testCategoryD_InvalidCharacterLiterals(TestContext& ctx) {
    // Empty and multi-character literals
    expectLexicalError(ctx, "''", "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_empty");
    expectLexicalError(ctx, "char c\n    c medalo 'ab'", "LEXICAL_ERROR_MALFORMED_CHAR", 2, 14, "char_multi");
    expectLexicalError(ctx, "'\\nA'", "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_escape_plus_extra");

    // Invalid escape sequences
    expectLexicalError(ctx, "'\\r'", "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_invalid_esc_r");
    expectLexicalError(ctx, "'\\a'", "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_invalid_esc_a");
    expectLexicalError(ctx, "'\\x'", "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_invalid_esc_x");

    // Unterminated literals
    expectLexicalError(ctx, "'",     "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_unterminated_quote_only");
    expectLexicalError(ctx, "'a",    "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_unterminated_eof");
    expectLexicalError(ctx, "'a\n",  "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_unterminated_lf");
    expectLexicalError(ctx, "'a\r",  "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_unterminated_cr");
    expectLexicalError(ctx, "'\\",   "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_unterminated_escape");

    // Raw control and non-ASCII bytes inside single quotes
    const std::string rawNullChar("'\0'", 3);
    const std::string rawTabChar = std::string(1, '\'') + '\t' + '\'';
    const std::string rawCtrl31  = std::string(1, '\'') + static_cast<char>(0x1Fu) + '\'';
    const std::string rawDel127  = std::string(1, '\'') + static_cast<char>(0x7Fu) + '\'';
    const std::string rawUtf128  = std::string(1, '\'') + static_cast<char>(0x80u) + '\'';
    const std::string rawUtf255  = std::string(1, '\'') + static_cast<char>(0xFFu) + '\'';

    expectLexicalError(ctx, rawNullChar, "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_null");
    expectLexicalError(ctx, rawTabChar,  "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_tab");
    expectLexicalError(ctx, rawCtrl31,   "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_0x1F");
    expectLexicalError(ctx, rawDel127,   "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_0x7F");
    expectLexicalError(ctx, rawUtf128,   "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_0x80");
    expectLexicalError(ctx, rawUtf255,   "LEXICAL_ERROR_MALFORMED_CHAR", 1, 1, "char_raw_0xFF");
}

// ============================================================================
// Category E: Whitespace, Line Endings & Comments
// ============================================================================

void testCategoryE_WhitespaceAndComments(TestContext& ctx) {
    // Empty file
    expectValidTokens(ctx, "", {
        {1, 1, "$", "EOF_MARKER", "$", -1}
    }, "empty_file");

    // Comment-only file without trailing newline ("# only comment" = 14 chars -> EOF at col 15)
    expectValidTokens(ctx, "# only comment", {
        {1, 15, "$", "EOF_MARKER", "$", -1}
    }, "comment_only_no_nl");

    // Comment-only file with trailing newline
    expectValidTokens(ctx, "# only comment\n", {
        {2, 1, "$", "EOF_MARKER", "$", -1}
    }, "comment_only_with_nl");

    // Windows CRLF (\r\n)
    expectValidTokens(ctx, "fn\r\nmain", {
        {1, 1, "fn",   "KEYWORD",    "fn",   -1},
        {2, 1, "main", "KEYWORD",    "main", -1},
        {2, 5, "$",    "EOF_MARKER", "$",    -1}
    }, "windows_crlf");

    // Standalone CR (\r) without \n: advances column by 1, line stays 1 (Spec v1.4 Sec 3.1 Rules 1 & 7)
    expectValidTokens(ctx, "fn\rmain", {
        {1, 1, "fn",   "KEYWORD",    "fn",   -1},
        {1, 4, "main", "KEYWORD",    "main", -1},
        {1, 8, "$",    "EOF_MARKER", "$",    -1}
    }, "standalone_cr");

    // Comment followed by token on next line + trailing comment at EOF
    expectValidTokens(ctx, "fn # comment 1\nmain # comment 2", {
        {1, 1,  "fn",   "KEYWORD",    "fn",   -1},
        {2, 1,  "main", "KEYWORD",    "main", -1},
        {2, 17, "$",    "EOF_MARKER", "$",    -1}
    }, "comments_inline_and_eof");

    // Embedded null byte inside '#' comment must be skipped safely until '\n'
    const std::string commentWithNull("# a\0b\nfn", 8);
    expectValidTokens(ctx, commentWithNull, {
        {2, 1, "fn", "KEYWORD",    "fn", -1},
        {2, 3, "$",  "EOF_MARKER", "$",  -1}
    }, "comment_with_embedded_null");
}

// ============================================================================
// Category F: Exact Coordinates, Tabs & EOF Placement
// ============================================================================

void testCategoryF_CoordinatesTabsAndEof(TestContext& ctx) {
    // Tabs count as exactly 1 column each (Spec v1.4 Section 3.1 Rule 1)
    expectValidTokens(ctx, "\tfn\tmain", {
        {1, 2, "fn",   "KEYWORD",    "fn",   -1},
        {1, 5, "main", "KEYWORD",    "main", -1},
        {1, 9, "$",    "EOF_MARKER", "$",    -1}
    }, "tabs_as_one_column");

    // File without trailing newline vs. with trailing LF vs. with trailing CRLF
    expectValidTokens(ctx, "khtm", {
        {1, 1, "khtm", "KEYWORD",    "khtm", -1},
        {1, 5, "$",    "EOF_MARKER", "$",    -1}
    }, "eof_no_trailing_nl");

    expectValidTokens(ctx, "khtm\n", {
        {1, 1, "khtm", "KEYWORD",    "khtm", -1},
        {2, 1, "$",    "EOF_MARKER", "$",    -1}
    }, "eof_trailing_lf");

    expectValidTokens(ctx, "khtm\r\n", {
        {1, 1, "khtm", "KEYWORD",    "khtm", -1},
        {2, 1, "$",    "EOF_MARKER", "$",    -1}
    }, "eof_trailing_crlf");

    // Specification v1.4 Section 5.2 exact benchmark: min_return.citu (39 bytes, 10 tokens)
    const std::string minReturn = "fn int32 main ( ) shro\n    wapis 0\nkhtm";
    ctx.checkEqual(minReturn.size(), static_cast<std::size_t>(39), "min_return byte length == 39");
    expectValidTokens(ctx, minReturn, {
        {1, 1,  "fn",    "KEYWORD",     "fn",    -1},
        {1, 4,  "int32", "TYPE",        "int32", -1},
        {1, 10, "main",  "KEYWORD",     "main",  -1},
        {1, 15, "(",     "PUNCTUATION", "(",     -1},
        {1, 17, ")",     "PUNCTUATION", ")",     -1},
        {1, 19, "shro",  "KEYWORD",     "shro",  -1},
        {2, 5,  "wapis", "KEYWORD",     "wapis", -1},
        {2, 11, "LT",    "INT_LITERAL", "0",     0},
        {3, 1,  "khtm",  "KEYWORD",     "khtm",  -1},
        {3, 5,  "$",     "EOF_MARKER",  "$",     -1}
    }, "spec_v1_4_min_return");
}

// ============================================================================
// Category G: All Five Lexical Error Codes, Illegal Bytes, BOM & Nulls
// ============================================================================

void testCategoryG_IllegalCharactersBomAndNulls(TestContext& ctx) {
    // C-style '==' and standalone '!'
    expectLexicalError(ctx, "int32 x\n    agar ( x == 1 )",
                       "LEXICAL_ERROR_ILLEGAL_CHARACTER", 2, 14, "illegal_double_equals");
    expectLexicalError(ctx, "agar ( !x )",
                       "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 8, "illegal_standalone_bang");

    // Forbidden punctuation/delimiters from C
    expectLexicalError(ctx, "int32 x;",    "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 8, "illegal_semicolon");
    expectLexicalError(ctx, "\"hello\"",   "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 1, "illegal_string_quote");
    expectLexicalError(ctx, "a[0]",        "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 2, "illegal_bracket");
    expectLexicalError(ctx, "fn main() {", "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 11, "illegal_curly_brace");
    expectLexicalError(ctx, "a @ b",       "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 3, "illegal_at_symbol");

    // UTF-8 BOM (0xEF 0xBB 0xBF) rejected at Line 1, Col 1
    const std::string bomSrc("\xEF\xBB\xBF" "fn", 5);
    expectLexicalError(ctx, bomSrc, "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 1, "utf8_bom_rejected");

    // Embedded raw null byte outside comment must trigger error (never mistaken for EOF)
    const std::string embeddedNull("fn \0 main", 9);
    expectLexicalError(ctx, embeddedNull, "LEXICAL_ERROR_ILLEGAL_CHARACTER", 1, 4, "embedded_null_outside_comment");
}

// ============================================================================
// Category H: Repeated Scanning on the Same Lexer Instance
// ============================================================================

void testCategoryH_RepeatedScanning(TestContext& ctx) {
    // Valid instance scanned twice
    Lexer validLexer("uint32 score\nscore medalo 10", "repeat_valid.citu");
    ctx.checkTrue(validLexer.scanTokens(), "First scanTokens() call must succeed");
    const std::vector<Token> firstRun = validLexer.getTokens();
    checkStreamInvariants(ctx, firstRun, "repeat_valid_run1");

    ctx.checkTrue(validLexer.scanTokens(), "Second scanTokens() call must succeed");
    const std::vector<Token>& secondRun = validLexer.getTokens();
    checkStreamInvariants(ctx, secondRun, "repeat_valid_run2");

    ctx.checkEqual(secondRun.size(), firstRun.size(), "Repeated scan must produce identical token count");
    const std::size_t n = (firstRun.size() < secondRun.size()) ? firstRun.size() : secondRun.size();
    for (std::size_t i = 0; i < n; ++i) {
        ctx.checkEqual(secondRun[i].index, firstRun[i].index, "repeat index[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].line, firstRun[i].line, "repeat line[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].col, firstRun[i].col, "repeat col[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].terminal, firstRun[i].terminal, "repeat terminal[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].category, firstRun[i].category, "repeat category[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].lexeme, firstRun[i].lexeme, "repeat lexeme[" + std::to_string(i) + "]");
        ctx.checkEqual(secondRun[i].parsedIntVal, firstRun[i].parsedIntVal, "repeat val[" + std::to_string(i) + "]");
    }

    // Invalid instance scanned twice
    Lexer invalidLexer("class Bad", "repeat_invalid.citu");
    ctx.checkTrue(!invalidLexer.scanTokens(), "First invalid scan must fail");
    ctx.checkTrue(invalidLexer.getTokens().empty(), "First invalid scan leaves tokens empty");
    ctx.checkEqual(invalidLexer.getErrorCode(), std::string("LEXICAL_ERROR_FORBIDDEN_CONSTRUCT"), "First error code");

    ctx.checkTrue(!invalidLexer.scanTokens(), "Second invalid scan must also fail");
    ctx.checkTrue(invalidLexer.getTokens().empty(), "Second invalid scan leaves tokens empty");
    ctx.checkEqual(invalidLexer.getErrorCode(), std::string("LEXICAL_ERROR_FORBIDDEN_CONSTRUCT"), "Second error code");
    ctx.checkEqual(invalidLexer.getErrorLine(), 1, "Second error line");
    ctx.checkEqual(invalidLexer.getErrorCol(), 1, "Second error col");
}

// ============================================================================
// Category I: Approved Sample Programs in examples/
// ============================================================================

void verifySampleProgram(TestContext& ctx,
                         const std::string& filePath,
                         std::size_t expectedSpecTokenCount,
                         const std::string& expectedStructName,
                         const std::string& label) {
    std::string content;
    if (!loadFileBinary(filePath, content)) {
        ctx.fail(label + ": could not open required sample file '" + filePath +
                 "' (run executable from repository root)");
        return;
    }

    Lexer lexer(content, filePath);
    const bool ok = lexer.scanTokens();
    ctx.checkTrue(ok, label + ": scanTokens() failed with " + lexer.getErrorMessage());
    ctx.checkTrue(!lexer.hasError(), label + ": hasError() must be false");

    const std::vector<Token>& tokens = lexer.getTokens();
    checkStreamInvariants(ctx, tokens, label);
    ctx.checkEqual(tokens.size(), expectedSpecTokenCount,
                   label + ": token count must match Specification v1.4 Section 2");

    if (tokens.size() >= 3) {
        ctx.checkEqual(tokens[0].terminal, std::string("struct"), label + ": starts with 'struct'");
        ctx.checkEqual(tokens[1].terminal, std::string("ID"), label + ": struct name terminal is ID");
        ctx.checkEqual(tokens[1].lexeme, expectedStructName, label + ": struct name lexeme");
        ctx.checkEqual(tokens[tokens.size() - 2].terminal, std::string("khtm"),
                       label + ": final non-EOF token is 'khtm'");
    }

    if (!tokens.empty()) {
        const std::pair<int, int> expectedEof = computeExpectedEofPosition(content);
        ctx.checkEqual(tokens.back().line, expectedEof.first, label + ": EOF line matches raw file bytes");
        ctx.checkEqual(tokens.back().col, expectedEof.second, label + ": EOF col matches raw file bytes");
    }
}

void testCategoryI_ApprovedSamplePrograms(TestContext& ctx) {
    // Verified counts from Specification v1.4 Section 2 (including final '$' EOF token)
    verifySampleProgram(ctx, "examples/prog1_geometry.citu", 122, "Box",      "prog1_geometry");
    verifySampleProgram(ctx, "examples/prog2_payroll.citu",  141, "Employee", "prog2_payroll");
    verifySampleProgram(ctx, "examples/prog3_grades.citu",   144, "Student",  "prog3_grades");

    // Additional spot-check on prog3_grades.citu for CHAR_LITERAL tokens ('A', 'B') and 'khali'
    std::string prog3Content;
    if (loadFileBinary("examples/prog3_grades.citu", prog3Content)) {
        Lexer lexer(prog3Content, "examples/prog3_grades.citu");
        if (lexer.scanTokens()) {
            int charLitCount = 0;
            int khaliCount = 0;
            for (const Token& tok : lexer.getTokens()) {
                if (tok.category == "CHAR_LITERAL") {
                    ++charLitCount;
                    ctx.checkTrue(tok.parsedIntVal == 65 || tok.parsedIntVal == 66,
                                  "prog3 CHAR_LITERAL must be 'A'(65) or 'B'(66)");
                }
                if (tok.terminal == "khali" && tok.category == "KEYWORD") {
                    ++khaliCount;
                }
            }
            ctx.checkEqual(charLitCount, 3, "prog3 contains exactly 3 CHAR_LITERAL tokens ('A', 'B', 'A')");
            ctx.checkEqual(khaliCount, 2, "prog3 contains exactly 2 'khali' tokens (fn khali, wapis khali)");
        }
    }
}

} // namespace

int main() {
    struct TestCase {
        std::string name;
        std::function<void(TestContext&)> fn;
    };

    const std::vector<TestCase> suite = {
        {"A1_Keywords_And_Types",                 testCategoryA_KeywordsAndTypes},
        {"A2_Forbidden_Keywords",                 testCategoryA_ForbiddenKeywords},
        {"A3_Identifiers_And_Case_Sensitivity",   testCategoryA_IdentifiersAndCaseSensitivity},
        {"A4_Operators_Punctuation_And_Literals", testCategoryA_OperatorsPunctuationAndLiterals},
        {"B1_Maximal_Munch_Words",                testCategoryB_MaximalMunchWords},
        {"B2_Maximal_Munch_Adjacent_Tokens",      testCategoryB_MaximalMunchAdjacentTokens},
        {"C1_Valid_Integer_Boundaries",           testCategoryC_ValidIntegers},
        {"C2_Integer_Overflow_And_Malformed",     testCategoryC_IntegerOverflowAndMalformedPrecedence},
        {"D1_Valid_Characters_And_Escapes",       testCategoryD_ValidCharactersAndEscapes},
        {"D2_Invalid_Character_Literals",         testCategoryD_InvalidCharacterLiterals},
        {"E1_Whitespace_LineEndings_Comments",    testCategoryE_WhitespaceAndComments},
        {"F1_Coordinates_Tabs_And_EOF",           testCategoryF_CoordinatesTabsAndEof},
        {"G1_Illegal_Characters_BOM_And_Nulls",   testCategoryG_IllegalCharactersBomAndNulls},
        {"H1_Repeated_Scanning_Idempotency",      testCategoryH_RepeatedScanning},
        {"I1_Approved_Sample_Programs",           testCategoryI_ApprovedSamplePrograms}
    };

    int passedCount = 0;
    int failedCount = 0;

    std::cout << "=== ICC Phase 2 Lexer Runtime Test Suite ===\n\n";

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