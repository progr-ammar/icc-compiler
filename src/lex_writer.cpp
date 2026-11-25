#include "lex_writer.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace {

// Single-pass Class A percent-escaping (Specification v1.4, Section 5.1 Rule 3):
// '%' -> "%25", '|' -> "%7C", '\n' -> "%0A", '\r' -> "%0D"
std::string escapeClassA(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        switch (c) {
            case '%':  out.append("%25"); break;
            case '|':  out.append("%7C"); break;
            case '\n': out.append("%0A"); break;
            case '\r': out.append("%0D"); break;
            default:   out.push_back(c);  break;
        }
    }
    return out;
}

// Complete 33-terminal to 9-category mapping (Specification v1.4, Sections 3.1, 3.2 & 4.1).
// Enforces exact lexeme equality for the 31 fixed-text terminals without imposing
// arbitrary restrictions on variable Class A lexemes (ID and LT).
bool isValidTerminalCategoryAndLexeme(const std::string& terminal,
                                      const std::string& category,
                                      const std::string& lexeme) {
    static const std::unordered_map<std::string, std::string> kFixedTerminals = {
        // 11 KEYWORD terminals
        {"struct", "KEYWORD"}, {"fn", "KEYWORD"}, {"main", "KEYWORD"},
        {"khali", "KEYWORD"}, {"wapis", "KEYWORD"}, {"shro", "KEYWORD"},
        {"khtm", "KEYWORD"}, {"jabtak", "KEYWORD"}, {"agar", "KEYWORD"},
        {"magar", "KEYWORD"}, {"medalo", "KEYWORD"},
        // 5 TYPE terminals
        {"int16", "TYPE"}, {"int32", "TYPE"}, {"uint16", "TYPE"},
        {"uint32", "TYPE"}, {"char", "TYPE"},
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

    if (terminal == "ID") {
        return category == "IDENTIFIER";
    }
    if (terminal == "LT") {
        return category == "INT_LITERAL" || category == "CHAR_LITERAL";
    }

    const auto it = kFixedTerminals.find(terminal);
    if (it == kFixedTerminals.end()) {
        return false;
    }
    return it->second == category && lexeme == terminal;
}

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

} // namespace

bool LexWriter::hasError() const {
    return hasError_;
}

const std::string& LexWriter::getErrorCode() const {
    return errorCode_;
}

const std::string& LexWriter::getErrorMessage() const {
    return errorMessage_;
}

bool LexWriter::recordError(const std::string& code, const std::string& message) {
    hasError_ = true;
    errorCode_ = code;
    errorMessage_ = message;
    return false;
}

bool LexWriter::validateInput(const std::string& sourceFilename,
                              const std::vector<Token>& tokens) {
    constexpr long long kMaxUint32 = 4294967295LL;

    if (sourceFilename == "@NONE") {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "Source filename literal '@NONE' collides with the v1.4 missing-value sentinel");
    }

    if (tokens.empty()) {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "Cannot serialize an empty token vector; at least the '$' EOF token is required");
    }

    if (tokens.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "Token count exceeds maximum representable int index");
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        const std::string idxLabel = "Token[" + std::to_string(i) + "]";

        if (tok.index != static_cast<int>(i)) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               idxLabel + " has non-contiguous index " + std::to_string(tok.index));
        }

        if (tok.line < 1 || tok.col < 1) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               idxLabel + " has invalid source coordinates (" +
                               std::to_string(tok.line) + ", " + std::to_string(tok.col) + ")");
        }

        if (i > 0) {
            const Token& prev = tokens[i - 1];
            const bool ordered = (tok.line > prev.line) ||
                                 (tok.line == prev.line && tok.col > prev.col);
            if (!ordered) {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   idxLabel + " coordinates (" + std::to_string(tok.line) + ", " +
                                   std::to_string(tok.col) + ") violate lexicographical ordering");
            }
        }

        if (tok.lexeme.empty()) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               idxLabel + " has empty lexeme");
        }

        if (!isValidTerminalCategoryAndLexeme(tok.terminal, tok.category, tok.lexeme)) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               idxLabel + " has invalid terminal/category/lexeme combination ('" +
                               tok.terminal + "', '" + tok.category + "', '" + tok.lexeme + "')");
        }

        const bool isLast = (i + 1 == tokens.size());
        if (!isLast) {
            if (tok.terminal == "$" || tok.category == "EOF_MARKER") {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   idxLabel + " contains premature EOF token before end of stream");
            }
        } else {
            if (tok.terminal != "$" || tok.category != "EOF_MARKER" ||
                tok.lexeme != "$" || tok.parsedIntVal != -1LL) {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   "Final token must be terminal '$', category 'EOF_MARKER', lexeme '$', parsedIntVal -1");
            }
        }

        if (tok.terminal == "LT") {
            if (tok.category == "INT_LITERAL") {
                if (tok.parsedIntVal < 0 || tok.parsedIntVal > kMaxUint32) {
                    return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                       idxLabel + " INT_LITERAL parsedIntVal out of range [0, 4294967295]");
                }
            } else {
                // category == "CHAR_LITERAL" guaranteed by isValidTerminalCategoryAndLexeme
                if (tok.parsedIntVal < 0 || tok.parsedIntVal > 127LL) {
                    return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                       idxLabel + " CHAR_LITERAL parsedIntVal out of range [0, 127]");
                }
            }
        } else if (tok.parsedIntVal != -1LL) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               idxLabel + " non-LT token must have parsedIntVal == -1");
        }
    }

    return true;
}

bool LexWriter::write(const std::string& outputLexPath,
                      const std::string& sourceFilename,
                      const std::vector<Token>& tokens) {
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();

    try {
        if (!validateInput(sourceFilename, tokens)) {
            return false;
        }

        errno = 0;
        std::ofstream out(outputLexPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return recordError("", formatOsError("open output file", outputLexPath, errno));
        }

        const std::string serializedSource =
            sourceFilename.empty() ? "@NONE" : escapeClassA(sourceFilename);

        // Header block (Specification v1.4, Sections 5.1 & 5.2)
        out << "@FORMAT | ICC_LEX | v1.0\n";
        out << "@SOURCE | " << serializedSource << '\n';
        out << "@TOTAL_TOKENS | " << tokens.size() << '\n';
        out << "# TOKEN_INDEX | LINE_NUM | COL_NUM | TERMINAL | CATEGORY | LEXEME | PARSED_INT_VAL\n";

        // Counted 7-field token records joined by exact " | " delimiter framing
        for (const Token& tok : tokens) {
            out << tok.index << " | "
                << tok.line << " | "
                << tok.col << " | "
                << tok.terminal << " | "
                << tok.category << " | "
                << escapeClassA(tok.lexeme) << " | "
                << tok.parsedIntVal << '\n';
        }

        // Footer sentinel
        out << "@END_FILE\n";

        errno = 0;
        out.flush();
        if (!out.good()) {
            const int flushErr = errno;
            out.close();
            return recordError("", formatOsError("write/flush output file", outputLexPath, flushErr));
        }

        errno = 0;
        out.close();
        if (out.fail()) {
            return recordError("", formatOsError("close output file", outputLexPath, errno));
        }

        return true;
    } catch (const std::exception& ex) {
        return recordError("", std::string("Unexpected runtime exception while writing '") +
                                   outputLexPath + "': " + ex.what());
    } catch (...) {
        return recordError("", "Unknown runtime exception while writing '" + outputLexPath + "'");
    }
}