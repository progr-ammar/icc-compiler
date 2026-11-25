#ifndef ICC_AST_WRITER_H
#define ICC_AST_WRITER_H

#include <string>
#include <vector>

#include "ast.h"

// Phase 2 AST Serializer (Specification v1.4, Sections 4.2, 5.1 & 5.3)
// Responsibility: Validates AST structural and schema invariants and serializes
// a pre-order std::vector<ASTNode> to an ICC_AST v1.0 file in binary LF mode.
class AstWriter {
public:
    AstWriter() = default;

    // Serializes nodes to outputAstPath in ICC_AST v1.0 format.
    // Returns true on success; returns false and records diagnostics on failure.
    bool write(const std::string& outputAstPath,
               const std::string& sourceFilename,
               const std::vector<ASTNode>& nodes);

    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;

private:
    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;

    bool validateInput(const std::string& sourceFilename,
                       const std::vector<ASTNode>& nodes);
    bool recordError(const std::string& code, const std::string& message);
};

#endif // ICC_AST_WRITER_H