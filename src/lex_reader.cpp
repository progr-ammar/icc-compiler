#include "lex_reader.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {

enum class ParseStage {
    ExpectFormat,
    ExpectSource,
    ExpectTotalTokens,
    ReadRecords,
    AfterEndFile
};

std::string formatOsError(const std::string& action,
                          const std::string& path,
                          int errNum) {
    std::ostringstream oss;
    oss << "OS I/O error: failed to " << action << " '" << path << "'";
    if (errNum != 0) {
        oss << " (" << std::strerror(errNum) << ")";
    }
    return oss.str();
}

// Splits a line on raw '|' (0x7C) and removes at most one delimiter-padding
// ASCII space (0x20) adjacent to each '|', preserving all other spaces
// (Specification v1.4, Section 5.1 Rule 2).
std::vector<std::string> splitPipeFramedLine(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (true) {
        const std::size_t barPos = line.find('|', start);
        if (barPos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, barPos - start));
        start = barPos + 1;
    }

    const std::size_t count = fields.size();
    for (std::size_t i = 0; i < count; ++i) {
        std::string& f = fields[i];
        if (i > 0 && !f.empty() && f.front() == ' ') {
            f.erase(0, 1);
        }
        if (i + 1 < count && !f.empty() && f.back() == ' ') {
            f.pop_back();
        }
    }

    return fields;
}

// Single-pass Class A percent-decoding (Specification v1.4, Section 5.1 Rule 3):
// "%25" -> '%', "%7C" -> '|', "%0A" -> '\n', "%0D" -> '\r'.
// Returns false on any '%' not followed by one of these four exact uppercase codes.
bool unescapeClassA(const std::string& encoded, std::string& decodedOut) {
    decodedOut.clear();
    decodedOut.reserve(encoded.size());

    std::size_t i = 0;
    while (i < encoded.size()) {
        if (encoded[i] != '%') {
            decodedOut.push_back(encoded[i]);
            ++i;
            continue;
        }

        if (i + 2 >= encoded.size()) {
            return false;
        }

        const char h1 = encoded[i + 1];
        const char h2 = encoded[i + 2];

        if (h1 == '2' && h2 == '5') {
            decodedOut.push_back('%');
        } else if (h1 == '7' && h2 == 'C') {
            decodedOut.push_back('|');
        } else if (h1 == '0' && h2 == 'A') {
            decodedOut.push_back('\n');
        } else if (h1 == '0' && h2 == 'D') {
            decodedOut.push_back('\r');
        } else {
            return false;
        }

        i += 3;
    }

    return true;
}

// Strict Class N integer parsing matching ^-?[0-9]+$ with overflow checking
// (Specification v1.4, Section 5.1 Rule 3).
bool parseStrictInt64(const std::string& field, long long& outVal) {
    if (field.empty()) {
        return false;
    }

    std::size_t pos = 0;
    bool negative = false;
    if (field[0] == '-') {
        negative = true;
        pos = 1;
        if (pos == field.size()) {
            return false; // lone "-"
        }
    }

    const unsigned long long maxPos =
        static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    const unsigned long long limit = negative ? (maxPos + 1ULL) : maxPos;

    unsigned long long acc = 0;
    for (; pos < field.size(); ++pos) {
        const char c = field[pos];
        if (c < '0' || c > '9') {
            return false;
        }
        const unsigned long long digit = static_cast<unsigned long long>(c - '0');
        if (acc > (limit - digit) / 10ULL) {
            return false; // 64-bit signed overflow
        }
        acc = acc * 10ULL + digit;
    }

    if (!negative) {
        outVal = static_cast<long long>(acc);
    } else if (acc == maxPos + 1ULL) {
        outVal = std::numeric_limits<long long>::min();
    } else {
        outVal = -static_cast<long long>(acc);
    }
    return true;
}

bool parseStrictInt(const std::string& field, int& outVal) {
    long long val64 = 0;
    if (!parseStrictInt64(field, val64)) {
        return false;
    }
    if (val64 < static_cast<long long>(std::numeric_limits<int>::min()) ||
        val64 > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    outVal = static_cast<int>(val64);
    return true;
}

// Class B base character set: ^[A-Za-z0-9_.:+%[\] -]+$ (Section 5.1 Rule 3).
bool isClassBBaseChar(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == ':' || c == '+' ||
           c == '%' || c == '[' || c == ']' || c == ' ' || c == '-';
}

// Validates Class B syntax for CATEGORY (disallows empty, @NONE, and edge spaces).
bool isValidClassBCategorySyntax(const std::string& s) {
    if (s.empty() || s == "@NONE" || s.front() == ' ' || s.back() == ' ') {
        return false;
    }
    for (char c : s) {
        if (!isClassBBaseChar(c)) {
            return false;
        }
    }
    return true;
}

// Validates Class B syntax for TERMINAL, including Section 5.1 Rule 3 operator exceptions.
bool isValidClassBTerminalSyntax(const std::string& s) {
    if (s.empty() || s == "@NONE" || s.front() == ' ' || s.back() == ' ') {
        return false;
    }
    if (s == "$" || s == "<" || s == ">" || s == "=" || s == "!=" ||
        s == "<=" || s == ">=" || s == "*" || s == "/" ||
        s == "(" || s == ")" || s == ",") {
        return true;
    }
    for (char c : s) {
        if (!isClassBBaseChar(c)) {
            return false;
        }
    }
    return true;
}

// Validates that (terminal, category) is a legal pair from the 33-terminal /
// 9-category Citu lexical specification (Specification v1.4, Sections 3.1, 3.2 & 4.1).
bool isValidTerminalCategoryPair(const std::string& terminal,
                                 const std::string& category) {
    static const std::unordered_map<std::string, std::string> kTerminalToCategory = {
        // 11 KEYWORD terminals
        {"struct", "KEYWORD"}, {"fn", "KEYWORD"}, {"main", "KEYWORD"},
        {"khali", "KEYWORD"}, {"wapis", "KEYWORD"}, {"shro", "KEYWORD"},
        {"khtm", "KEYWORD"}, {"jabtak", "KEYWORD"}, {"agar", "KEYWORD"},
        {"magar", "KEYWORD"}, {"medalo", "KEYWORD"},
        // 5 TYPE terminals
        {"int16", "TYPE"}, {"int32", "TYPE"}, {"uint16", "TYPE"},
        {"uint32", "TYPE"}, {"char", "TYPE"},
        // 1 IDENTIFIER terminal
        {"ID", "IDENTIFIER"},
        // 4 ARITH_OP terminals
        {"+", "ARITH_OP"}, {"-", "ARITH_OP"}, {"*", "ARITH_OP"}, {"/", "ARITH_OP"},
        // 6 REL_OP terminals
        {"<", "REL_OP"}, {">", "REL_OP"}, {"=", "REL_OP"},
        {"!=", "REL_OP"}, {"<=", "REL_OP"}, {">=", "REL_OP"},
        // 4 PUNCTUATION terminals
        {"(", "PUNCTUATION"}, {")", "PUNCTUATION"}, {",", "PUNCTUATION"}, {".", "PUNCTUATION"},
        // 1 EOF_MARKER terminal
        {"$", "EOF_MARKER"}
    };

    if (terminal == "LT") {
        return category == "INT_LITERAL" || category == "CHAR_LITERAL";
    }

    const auto it = kTerminalToCategory.find(terminal);
    if (it == kTerminalToCategory.end()) {
        return false;
    }
    return it->second == category;
}

} // namespace

const std::vector<Token>& LexReader::getTokens() const {
    return tokens_;
}

const std::string& LexReader::getSourceFilename() const {
    return sourceFilename_;
}

bool LexReader::hasError() const {
    return hasError_;
}

const std::string& LexReader::getErrorCode() const {
    return errorCode_;
}

const std::string& LexReader::getErrorMessage() const {
    return errorMessage_;
}

int LexReader::getErrorLine() const {
    return errorLine_;
}

bool LexReader::fail(const std::string& code, int fileLine, const std::string& detail) {
    tokens_.clear();
    sourceFilename_.clear();
    hasError_ = true;
    errorCode_ = code;
    errorLine_ = fileLine;

    std::ostringstream oss;
    if (!code.empty()) {
        oss << code;
        if (fileLine > 0) {
            oss << " (line " << fileLine << ")";
        }
        oss << ": ";
    }
    oss << detail;
    errorMessage_ = oss.str();

    return false;
}

bool LexReader::read(const std::string& inputLexPath) {
    // Reset all prior output and diagnostic state for safe instance reuse.
    tokens_.clear();
    sourceFilename_.clear();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;

    try {
        errno = 0;
        std::ifstream in(inputLexPath, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            return fail("", 0, formatOsError("open input file", inputLexPath, errno));
        }

        constexpr long long kMaxUint32 = 4294967295LL;

        ParseStage stage = ParseStage::ExpectFormat;
        int expectedTotalTokens = 0;
        int fileLineNum = 0;
        std::vector<int> recordFileLines;

        std::string rawLine;
        while (std::getline(in, rawLine)) {
            ++fileLineNum;

            // Reject UTF-8 BOM (0xEF 0xBB 0xBF) at the start of the file (Section 5.1 Rule 1).
            if (fileLineNum == 1 && rawLine.size() >= 3 &&
                static_cast<unsigned char>(rawLine[0]) == 0xEF &&
                static_cast<unsigned char>(rawLine[1]) == 0xBB &&
                static_cast<unsigned char>(rawLine[2]) == 0xBF) {
                return fail("FATAL_IO_FORMAT_MISMATCH", 1,
                            "UTF-8 BOM (0xEF 0xBB 0xBF) is not allowed; .lex files must be UTF-8 without BOM");
            }

            // Strip at most one trailing '\r' for CRLF compatibility (Section 5.1 Rule 1).
            if (!rawLine.empty() && rawLine.back() == '\r') {
                rawLine.pop_back();
            }

            // Skip uncounted blank lines and '#' comment lines (Section 5.1 Rule 5).
            if (rawLine.empty() || rawLine[0] == '#') {
                continue;
            }

            const std::vector<std::string> fields = splitPipeFramedLine(rawLine);

            if (stage == ParseStage::ExpectFormat) {
                if (fields.size() != 3 ||
                    fields[0] != "@FORMAT" ||
                    fields[1] != "ICC_LEX" ||
                    fields[2] != "v1.0") {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@FORMAT | ICC_LEX | v1.0'");
                }
                stage = ParseStage::ExpectSource;
                continue;
            }

            if (stage == ParseStage::ExpectSource) {
                if (fields.size() != 2 || fields[0] != "@SOURCE" || fields[1].empty()) {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@SOURCE | <escaped_source_or_@NONE>'");
                }
                if (fields[1] == "@NONE") {
                    sourceFilename_.clear();
                } else {
                    std::string decodedSource;
                    if (!unescapeClassA(fields[1], decodedSource)) {
                        return fail("FATAL_IO_MALFORMED_ESCAPE", fileLineNum,
                                    "Malformed percent-escape sequence in @SOURCE header");
                    }
                    sourceFilename_ = std::move(decodedSource);
                }
                stage = ParseStage::ExpectTotalTokens;
                continue;
            }

            if (stage == ParseStage::ExpectTotalTokens) {
                if (fields.size() != 2 || fields[0] != "@TOTAL_TOKENS") {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@TOTAL_TOKENS | <N>'");
                }
                long long parsedTotal = 0;
                if (!parseStrictInt64(fields[1], parsedTotal)) {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Malformed integer value in @TOTAL_TOKENS header: '" + fields[1] + "'");
                }
                if (parsedTotal < 1 ||
                    parsedTotal > static_cast<long long>(std::numeric_limits<int>::max())) {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "@TOTAL_TOKENS must be at least 1 and fit within int range");
                }
                expectedTotalTokens = static_cast<int>(parsedTotal);
                tokens_.reserve(static_cast<std::size_t>(expectedTotalTokens));
                recordFileLines.reserve(static_cast<std::size_t>(expectedTotalTokens));
                stage = ParseStage::ReadRecords;
                continue;
            }

            if (stage == ParseStage::AfterEndFile) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Unexpected non-comment data after @END_FILE");
            }

            // Stage == ParseStage::ReadRecords
            if (fields.size() == 1 && fields[0] == "@END_FILE") {
                stage = ParseStage::AfterEndFile;
                continue;
            }

            if (fields.size() != 7) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Expected 7-field token record or '@END_FILE', found " +
                            std::to_string(fields.size()) + " field(s)");
            }

            if (tokens_.size() >= static_cast<std::size_t>(expectedTotalTokens)) {
                return fail("FATAL_IO_COUNT_MISMATCH", fileLineNum,
                            "Found more token records than declared in @TOTAL_TOKENS (" +
                            std::to_string(expectedTotalTokens) + ")");
            }

            // Parse Class N integer fields strictly
            int tokIndex = 0;
            int tokLine = 0;
            int tokCol = 0;
            long long tokVal = 0;

            if (!parseStrictInt(fields[0], tokIndex)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed TOKEN_INDEX field: '" + fields[0] + "'");
            }
            if (!parseStrictInt(fields[1], tokLine)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed LINE_NUM field: '" + fields[1] + "'");
            }
            if (!parseStrictInt(fields[2], tokCol)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed COL_NUM field: '" + fields[2] + "'");
            }
            if (!parseStrictInt64(fields[6], tokVal)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed PARSED_INT_VAL field: '" + fields[6] + "'");
            }

            // Validate Class B field syntax for TERMINAL and CATEGORY
            const std::string& terminal = fields[3];
            const std::string& category = fields[4];

            if (!isValidClassBTerminalSyntax(terminal)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Invalid Class B TERMINAL syntax: '" + terminal + "'");
            }
            if (!isValidClassBCategorySyntax(category)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Invalid Class B CATEGORY syntax: '" + category + "'");
            }

            // Decode Class A LEXEME field (literal "@NONE" is preserved as ordinary text)
            std::string decodedLexeme;
            if (!unescapeClassA(fields[5], decodedLexeme)) {
                return fail("FATAL_IO_MALFORMED_ESCAPE", fileLineNum,
                            "Malformed percent-escape sequence in LEXEME field");
            }
            if (decodedLexeme.empty()) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Token LEXEME field cannot be empty");
            }

            // Section 5.2 Rule 1: Contiguous TOKEN_INDEX from 0 .. N - 1
            const int expectedIdx = static_cast<int>(tokens_.size());
            if (tokIndex != expectedIdx) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Expected TOKEN_INDEX " + std::to_string(expectedIdx) +
                            ", found " + std::to_string(tokIndex));
            }

            // Section 5.2 Rule 2: Positive coordinates & lexicographical (LINE_NUM, COL_NUM) ordering
            if (tokLine < 1 || tokCol < 1) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Token coordinates must satisfy LINE_NUM >= 1 and COL_NUM >= 1");
            }
            if (!tokens_.empty()) {
                const Token& prev = tokens_.back();
                const bool ordered = (tokLine > prev.line) ||
                                     (tokLine == prev.line && tokCol > prev.col);
                if (!ordered) {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "Token coordinates (" + std::to_string(tokLine) + ", " +
                                std::to_string(tokCol) + ") violate lexicographical ordering after (" +
                                std::to_string(prev.line) + ", " + std::to_string(prev.col) + ")");
                }
            }

            // Terminal & Category validity (Sections 3.1, 3.2, 4.1, 5.2)
            if (!isValidTerminalCategoryPair(terminal, category)) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Invalid TERMINAL/CATEGORY pairing: ('" + terminal + "', '" + category + "')");
            }

            // Section 5.2 Rule 4: Literal consistency
            if (terminal == "LT") {
                if (category == "INT_LITERAL") {
                    if (tokVal < 0 || tokVal > kMaxUint32) {
                        return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                    "INT_LITERAL PARSED_INT_VAL out of range [0, 4294967295]");
                    }
                } else {
                    // category == "CHAR_LITERAL" guaranteed by isValidTerminalCategoryPair
                    if (tokVal < 0 || tokVal > 127LL) {
                        return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                    "CHAR_LITERAL PARSED_INT_VAL out of range [0, 127]");
                    }
                }
            } else if (tokVal != -1LL) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Non-LT token must have PARSED_INT_VAL == -1");
            }

            Token tok;
            tok.index = tokIndex;
            tok.line = tokLine;
            tok.col = tokCol;
            tok.terminal = terminal;
            tok.category = category;
            tok.lexeme = std::move(decodedLexeme);
            tok.parsedIntVal = tokVal;

            tokens_.push_back(std::move(tok));
            recordFileLines.push_back(fileLineNum);
        }

        if (in.bad()) {
            return fail("", fileLineNum, formatOsError("read input file", inputLexPath, errno));
        }

        // Verify required headers were present
        if (stage == ParseStage::ExpectFormat ||
            stage == ParseStage::ExpectSource ||
            stage == ParseStage::ExpectTotalTokens) {
            return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                        "Incomplete or missing required .lex header block");
        }

        // Verify @END_FILE sentinel was present
        if (stage != ParseStage::AfterEndFile) {
            return fail("FATAL_IO_MISSING_END_FILE", fileLineNum,
                        "Reached end of file without encountering '@END_FILE'");
        }

        // Verify exact payload record count
        if (tokens_.size() != static_cast<std::size_t>(expectedTotalTokens)) {
            return fail("FATAL_IO_COUNT_MISMATCH", fileLineNum,
                        "Expected " + std::to_string(expectedTotalTokens) +
                        " token record(s), found " + std::to_string(tokens_.size()));
        }

        // Section 5.2 Rule 3: Exactly one EOF token, strictly at N - 1
        for (std::size_t i = 0; i + 1 < tokens_.size(); ++i) {
            if (tokens_[i].terminal == "$" || tokens_[i].category == "EOF_MARKER") {
                return fail("FATAL_IO_INVARIANT_VIOLATION", recordFileLines[i],
                            "Premature EOF token at index " + std::to_string(i) +
                            " before final token");
            }
        }

        const Token& last = tokens_.back();
        if (last.terminal != "$" || last.category != "EOF_MARKER" ||
            last.lexeme != "$" || last.parsedIntVal != -1LL) {
            return fail("FATAL_IO_INVARIANT_VIOLATION", recordFileLines.back(),
                        "Final token must have TERMINAL='$', CATEGORY='EOF_MARKER', LEXEME='$', PARSED_INT_VAL=-1");
        }

        return true;
    } catch (const std::exception& ex) {
        return fail("", 0, std::string("Unexpected runtime exception while reading '") +
                               inputLexPath + "': " + ex.what());
    } catch (...) {
        return fail("", 0, "Unknown runtime exception while reading '" + inputLexPath + "'");
    }
}