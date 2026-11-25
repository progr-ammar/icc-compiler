#ifndef ICC_AST_READER_H
#define ICC_AST_READER_H

#include <string>
#include <vector>

#include "ast.h"

// Phase 2/3 AST Deserializer (Specification v1.4, Sections 4.2, 5.1 & 5.3)
// Responsibility: Reads an ICC_AST v1.0 file from disk, enforces all 6 structural
// invariants and the 23-kind AST schema, and reconstructs an independent
// std::vector<ASTNode> (including childIds) and source filename in memory.
class AstReader {
public:
    AstReader() = default;

    // Reads and validates inputAstPath in ICC_AST v1.0 format.
    // Clears previous state at entry. Returns true iff every check passes.
    bool read(const std::string& inputAstPath);

    const std::vector<ASTNode>& getAstNodes() const;
    const std::string& getSourceFilename() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;

private:
    std::vector<ASTNode> astNodes_;
    std::string sourceFilename_;

    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;

    bool fail(const std::string& code, int fileLine, const std::string& detail);
};

#endif // ICC_AST_READER_H