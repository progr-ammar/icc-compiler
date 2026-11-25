#include "lexer.h"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace {

std::string formatByteForDiagnostic(char c) {
    static const char kHexDigits[] = "0123456789ABCDEF";
    const unsigned char uch = static_cast<unsigned char>(c);

    std::string hex = "0x";
    hex.push_back(kHexDigits[(uch >> 4) & 0x0F]);
    hex.push_back(kHexDigits[uch & 0x0F]);

    if (uch >= 32 && uch <= 126) {
        return std::string("'") + c + "' (" + hex + ")";
    }
    return "byte " + hex;
}

} // namespace

// ============================================================================
// Constructor and Read-Only Accessors
// ============================================================================

Lexer::Lexer(std::string sourceCode, std::string sourceFilename)
    : source_(std::move(sourceCode)),
      sourceFilename_(std::move(sourceFilename)) {}

const std::vector<Token>& Lexer::getTokens() const {
    return tokens_;
}

bool Lexer::hasError() const {
    return hasError_;
}

const std::string& Lexer::getErrorCode() const {
    return errorCode_;
}

const std::string& Lexer::getErrorMessage() const {
    return errorMessage_;
}

int Lexer::getErrorLine() const {
    return errorLine_;
}

int Lexer::getErrorCol() const {
    return errorCol_;
}

// ============================================================================
// Cursor Management (Previously Verified Group 1 — Preserved Verbatim)
// ============================================================================

bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

char Lexer::peek() const {
    if (isAtEnd()) {
        return '\0';
    }
    return source_[current_];
}

char Lexer::advance() {
    if (isAtEnd()) {
        return '\0';
    }

    char c = source_[current_];
    ++current_;

    if (c == '\n') {
        ++currentLine_;
        currentCol_ = 1;
    } else {
        ++currentCol_;
    }

    return c;
}

// ============================================================================
// Locale-Independent ASCII Classification
// ============================================================================

bool Lexer::isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::isAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::isAsciiIdentifierPart(char c) {
    return isAsciiAlpha(c) || isAsciiDigit(c) || c == '_';
}

// ============================================================================
// Whitespace and '#' Single-Line Comment Skipping
// ============================================================================

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '#') {
            advance(); // consume '#'
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
        } else {
            break;
        }
    }
}

// ============================================================================
// Token Emission and Dispatch
// ============================================================================

void Lexer::emitToken(const std::string& terminal,
                      const std::string& category,
                      const std::string& lexeme,
                      long long parsedIntVal) {
    Token tok;
    tok.index = static_cast<int>(tokens_.size());
    tok.line = tokenLine_;
    tok.col = tokenCol_;
    tok.terminal = terminal;
    tok.category = category;
    tok.lexeme = lexeme;
    tok.parsedIntVal = parsedIntVal;
    tokens_.push_back(std::move(tok));
}

bool Lexer::scanNextToken() {
    tokenStartOffset_ = current_;
    tokenLine_ = currentLine_;
    tokenCol_ = currentCol_;

    const char c = advance();

    switch (c) {
        // Arithmetic operators (Section 3.1, Rule 6)
        case '+':
        case '-':
        case '*':
        case '/': {
            const std::string text(1, c);
            emitToken(text, "ARITH_OP", text, -1);
            return true;
        }

        // Punctuation (Section 3.1, Rule 6)
        case '(':
        case ')':
        case ',':
        case '.': {
            const std::string text(1, c);
            emitToken(text, "PUNCTUATION", text, -1);
            return true;
        }

        // Relational and equality operators (1-character lookahead)
        case '<':
        case '>':
        case '=':
        case '!':
            return handleRelationalOrEquality(c);

        // Character literals (Section 3.1, Rule 5)
        case '\'':
            return handleCharacter();

        default:
            if (isAsciiAlpha(c) || c == '_') {
                return handleWord();
            }
            if (isAsciiDigit(c)) {
                return handleNumber(c);
            }
            return reportError("LEXICAL_ERROR_ILLEGAL_CHARACTER",
                               "Unexpected character " + formatByteForDiagnostic(c));
    }
}

// ============================================================================
// Dedicated Category Recognizers
// ============================================================================

bool Lexer::handleWord() {
    static const std::unordered_set<std::string> kForbiddenKeywords = {
        "class", "do"
    };

    static const std::unordered_set<std::string> kTypeKeywords = {
        "int16", "int32", "uint16", "uint32", "char"
    };

    static const std::unordered_set<std::string> kReservedKeywords = {
        "struct", "fn", "main", "khali", "wapis",
        "shro", "khtm", "jabtak", "agar", "magar", "medalo"
    };

    while (!isAtEnd() && isAsciiIdentifierPart(peek())) {
        advance();
    }

    const std::string lexeme = source_.substr(tokenStartOffset_, current_ - tokenStartOffset_);

    if (kForbiddenKeywords.find(lexeme) != kForbiddenKeywords.end()) {
        return reportError("LEXICAL_ERROR_FORBIDDEN_CONSTRUCT",
                           "Forbidden construct '" + lexeme + "' is not allowed in Citu");
    }

    if (kTypeKeywords.find(lexeme) != kTypeKeywords.end()) {
        emitToken(lexeme, "TYPE", lexeme, -1);
        return true;
    }

    if (kReservedKeywords.find(lexeme) != kReservedKeywords.end()) {
        emitToken(lexeme, "KEYWORD", lexeme, -1);
        return true;
    }

    emitToken("ID", "IDENTIFIER", lexeme, -1);
    return true;
}

bool Lexer::handleNumber(char firstDigit) {
    constexpr long long kMaxUint32 = 4294967295LL;

    long long value = static_cast<long long>(firstDigit - '0');
    bool overflowed = false;

    while (!isAtEnd() && isAsciiDigit(peek())) {
        const char digitChar = advance();
        if (!overflowed) {
            value = value * 10 + static_cast<long long>(digitChar - '0');
            if (value > kMaxUint32) {
                overflowed = true;
            }
        }
    }

    // Priority 1: Token-shape check (digits immediately followed by [a-zA-Z_.])
    if (!isAtEnd() && (isAsciiAlpha(peek()) || peek() == '_' || peek() == '.')) {
        while (!isAtEnd() && (isAsciiIdentifierPart(peek()) || peek() == '.')) {
            advance();
        }
        const std::string badLexeme = source_.substr(tokenStartOffset_, current_ - tokenStartOffset_);
        return reportError("LEXICAL_ERROR_MALFORMED_NUMBER",
                           "Malformed integer literal '" + badLexeme + "'");
    }

    const std::string lexeme = source_.substr(tokenStartOffset_, current_ - tokenStartOffset_);

    // Priority 2: Numeric-range check (value > 4,294,967,295)
    if (overflowed) {
        return reportError("LEXICAL_ERROR_INTEGER_TOO_LARGE",
                           "Integer literal '" + lexeme + "' exceeds maximum value 4294967295");
    }

    emitToken("LT", "INT_LITERAL", lexeme, value);
    return true;
}

bool Lexer::handleCharacter() {
    if (isAtEnd()) {
        return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                           "Unterminated character literal at end of file");
    }

    const char ch = advance();

    if (ch == '\'') {
        return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                           "Empty character literal '' is not allowed");
    }

    if (ch == '\n' || ch == '\r') {
        return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                           "Unterminated character literal before end of line");
    }

    long long asciiVal = -1;

    if (ch == '\\') {
        if (isAtEnd()) {
            return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                               "Unterminated escape sequence in character literal");
        }
        const char esc = advance();
        switch (esc) {
            case 'n':  asciiVal = 10; break;
            case 't':  asciiVal = 9;  break;
            case '0':  asciiVal = 0;  break;
            case '\\': asciiVal = 92; break;
            case '\'': asciiVal = 39; break;
            default:
                return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                                   "Invalid escape sequence '\\" + formatByteForDiagnostic(esc) +
                                   "' in character literal");
        }
    } else {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32 || uch > 126) {
            return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                               "Non-printable or non-ASCII byte " + formatByteForDiagnostic(ch) +
                               " inside character literal");
        }
        asciiVal = static_cast<long long>(uch);
    }

    if (isAtEnd() || peek() != '\'') {
        return reportError("LEXICAL_ERROR_MALFORMED_CHAR",
                           "Expected closing '\\'' after single character literal");
    }

    advance(); // consume closing '\''

    const std::string lexeme = source_.substr(tokenStartOffset_, current_ - tokenStartOffset_);
    emitToken("LT", "CHAR_LITERAL", lexeme, asciiVal);
    return true;
}

bool Lexer::handleRelationalOrEquality(char firstChar) {
    switch (firstChar) {
        case '<':
            if (!isAtEnd() && peek() == '=') {
                advance();
                emitToken("<=", "REL_OP", "<=", -1);
            } else {
                emitToken("<", "REL_OP", "<", -1);
            }
            return true;

        case '>':
            if (!isAtEnd() && peek() == '=') {
                advance();
                emitToken(">=", "REL_OP", ">=", -1);
            } else {
                emitToken(">", "REL_OP", ">", -1);
            }
            return true;

        case '=':
            if (!isAtEnd() && peek() == '=') {
                advance();
                return reportError("LEXICAL_ERROR_ILLEGAL_CHARACTER",
                                   "'==' is not allowed in Citu; use '=' for equality and 'medalo' for assignment");
            }
            emitToken("=", "REL_OP", "=", -1);
            return true;

        case '!':
            if (!isAtEnd() && peek() == '=') {
                advance();
                emitToken("!=", "REL_OP", "!=", -1);
            } else {
                return reportError("LEXICAL_ERROR_ILLEGAL_CHARACTER",
                                   "Standalone '!' is not a valid Citu operator; only '!=' is supported");
            }
            return true;

        default:
            return reportError("LEXICAL_ERROR_ILLEGAL_CHARACTER",
                               "Unexpected operator character " + formatByteForDiagnostic(firstChar));
    }
}

// ============================================================================
// Diagnostics and Main Scanning Loop
// ============================================================================

bool Lexer::reportError(const std::string& code, const std::string& detail) {
    hasError_ = true;
    errorCode_ = code;
    errorLine_ = tokenLine_;
    errorCol_ = tokenCol_;

    std::ostringstream oss;
    oss << code << " in " << sourceFilename_
        << " at Line " << tokenLine_ << ", Col " << tokenCol_
        << ": " << detail;
    errorMessage_ = oss.str();

    tokens_.clear();
    return false;
}

bool Lexer::scanTokens() {
    // Reset mutable scanning state so scanTokens() is safe to call repeatedly.
    current_ = 0;
    tokenStartOffset_ = 0;
    currentLine_ = 1;
    currentCol_ = 1;
    tokenLine_ = 1;
    tokenCol_ = 1;
    tokens_.clear();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;
    errorCol_ = 0;

    // Reject UTF-8 BOM (0xEF 0xBB 0xBF) explicitly at Line 1, Col 1 (Section 3.1, Rules 1 & 6).
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        tokenStartOffset_ = 0;
        tokenLine_ = 1;
        tokenCol_ = 1;
        return reportError("LEXICAL_ERROR_ILLEGAL_CHARACTER",
                           "UTF-8 BOM (0xEF 0xBB 0xBF) is not allowed; source must be UTF-8 without BOM");
    }

    while (true) {
        skipWhitespaceAndComments();
        if (isAtEnd()) {
            break;
        }
        if (!scanNextToken()) {
            return false;
        }
    }

    // Emit the unique final '$' EOF_MARKER token at the exact post-EOF coordinates (Section 3.1, Rule 8).
    tokenStartOffset_ = current_;
    tokenLine_ = currentLine_;
    tokenCol_ = currentCol_;
    emitToken("$", "EOF_MARKER", "$", -1);

    return true;
}