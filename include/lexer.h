#ifndef ICC_LEXER_H
#define ICC_LEXER_H

#include <cstddef>
#include <string>
#include <vector>

#include "token.h"

// Phase 2 Lexical Scanning Engine (Specification v1.4, Section 3.1)
// Responsibility: Scans an in-memory UTF-8 (without BOM) Citu source buffer
// into a validated std::vector<Token> ending with the '$' EOF_MARKER token,
// or halts on the first lexical violation with an exact Section 3.6 diagnostic code.
class Lexer {
public:
    explicit Lexer(std::string sourceCode, std::string sourceFilename = "<input>");

    // Runs the scanning engine from byte 0 to EOF.
    // Returns true and appends '$' EOF_MARKER if all tokens are lexically valid.
    // Returns false, records diagnostic state, and clears tokens_ on any lexical error.
    bool scanTokens();

    // Read-only accessors for the produced token stream and diagnostic state
    const std::vector<Token>& getTokens() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;
    int getErrorCol() const;

private:
    // Source buffer and byte-offset cursors
    std::string source_;
    std::string sourceFilename_;
    std::size_t current_ = 0;
    std::size_t tokenStartOffset_ = 0;

    // 1-based source coordinates (Section 3.1, Rule 1)
    int currentLine_ = 1;
    int currentCol_ = 1;
    int tokenLine_ = 1;
    int tokenCol_ = 1;

    // Accumulated token list and error state
    std::vector<Token> tokens_;
    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;
    int errorCol_ = 0;

    // Cursor navigation primitives
    bool isAtEnd() const;
    char peek() const;
    char advance();

    // Locale-independent ASCII character classification helpers
    static bool isAsciiAlpha(char c);
    static bool isAsciiDigit(char c);
    static bool isAsciiIdentifierPart(char c);

    // Whitespace and '#' single-line comment skipper (Section 3.1, Rule 7)
    void skipWhitespaceAndComments();

    // Token dispatch and emission
    bool scanNextToken();
    void emitToken(const std::string& terminal,
                   const std::string& category,
                   const std::string& lexeme,
                   long long parsedIntVal = -1);

    // Category-specific recognizers
    bool handleWord();
    bool handleNumber(char firstDigit);
    bool handleCharacter();
    bool handleRelationalOrEquality(char firstChar);

    // Diagnostic reporting anchored at (tokenLine_, tokenCol_) (Section 3.6)
    bool reportError(const std::string& code, const std::string& detail);
};

#endif // ICC_LEXER_H