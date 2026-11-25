#ifndef ICC_PARSER_H
#define ICC_PARSER_H

#include <cstddef>
#include <string>
#include <vector>

#include "ast.h"
#include "token.h"

// Phase 2 Table-Driven LL(1) Parser & Left-Associative AST Builder
// (Specification v1.4, Sections 3.2, 3.3, 4.1 & 4.2)
// Primary phase entry point: parseFromLexFile(), which deserializes <prog>.lex
// via LexReader rather than accepting the Lexer's in-memory token vector.
class Parser {
public:
    Parser() = default;

    // Phase-decoupled entry point: reads tokens from an ICC_LEX v1.0 file via LexReader,
    // validates syntax with the 39 x 33 LL(1) parsing table, and constructs the pre-order AST.
    bool parseFromLexFile(const std::string& inputLexPath);

    // Direct token-stream entry point for isolated unit testing.
    bool parseTokens(const std::vector<Token>& tokens,
                     const std::string& sourceFilename = "<input>");

    // Read-only accessors for AST output and diagnostic state
    const std::vector<ASTNode>& getAstNodes() const;
    const std::string& getSourceFilename() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;
    int getErrorCol() const;

    // Static grammar & LL(1) table introspection helpers (Section 3.2 & 3.3)
    static std::size_t getProductionCount();
    static std::size_t getNonTerminalCount();
    static std::size_t getTerminalCount();
    static std::size_t getPopulatedTableCellCount();

    // Returns the rule number in table cell [nonTerminal, terminal] using the
    // Section 3.2/3.3 spellings (e.g. "EX'", "$"); 0 for an empty error cell,
    // -1 if either name is unknown.
    static int getTableRule(const std::string& nonTerminal, const std::string& terminal);

private:
    std::vector<ASTNode> astNodes_;
    std::string sourceFilename_;

    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;
    int errorCol_ = 0;

    bool reportError(const std::string& code,
                     int line,
                     int col,
                     const std::string& detail);
};

#endif // ICC_PARSER_H