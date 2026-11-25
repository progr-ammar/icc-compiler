
#ifndef ICC_TOKEN_H
#define ICC_TOKEN_H

#include <string>

// Section 4.1 (Specification v1.4): Lexical Token
// Created in RAM by Phase 2 Lexer -> Serialized to .lex by LexWriter
// Deserialized from .lex into independent RAM by LexReader -> Consumed by Phase 2 Parser
struct Token {
    int index = 0;                // 0 .. TotalTokens - 1 (Column 1: TOKEN_INDEX)
    int line = 1;                 // 1-based source line number (Column 2: LINE_NUM)
    int col = 1;                  // 1-based byte-offset column number (Column 3: COL_NUM)
    std::string terminal;         // LL(1) table column terminal: "struct", "fn", "ID", "LT", "$", etc. (Column 4: TERMINAL)
    std::string category;         // KEYWORD, TYPE, IDENTIFIER, INT_LITERAL, CHAR_LITERAL, ARITH_OP, REL_OP, PUNCTUATION, EOF_MARKER (Column 5: CATEGORY)
    std::string lexeme;           // Raw source text in RAM; Class A percent-escaped in .lex (Column 6: LEXEME)
    long long parsedIntVal = -1;  // Numeric value for INT_LITERAL / ASCII code (0..127) for CHAR_LITERAL; -1 otherwise (Column 7: PARSED_INT_VAL)
};

#endif // ICC_TOKEN_H
