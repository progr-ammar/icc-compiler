#ifndef ICC_AST_SCHEMA_H
#define ICC_AST_SCHEMA_H

#include <string>
#include <vector>

#include "ast.h"

// Shared Section 4.2-B AST schema validation (Specification v1.4).
// Used by both AstWriter (before serializing) and AstReader (after
// deserializing) so the two sides of the .ast contract enforce exactly the
// same rules.
namespace ast_schema {

// True iff s matches [a-zA-Z_][a-zA-Z0-9_]* and is not one of the 16 reserved
// or 2 forbidden Citu keywords (Section 3.1, Rules 2-4).
bool isCituIdentifier(const std::string& s);

// True iff s is one of the five primitive type keywords.
bool isPrimitiveType(const std::string& s);

// Decodes a raw Citu character literal lexeme (e.g. "'A'", "'\\n'") into its
// ASCII code. Returns false if the lexeme is not a valid literal (Section 3.1, Rule 5).
bool decodeCharLiteral(const std::string& lexeme, long long& outValue);

// Validates one node's attributes (AST_OP, LEXEME, LIT_VAL) and its ordered
// children (given as node IDs into `nodes`) against the 23-kind schema.
// On failure returns false and stores a human-readable reason in errOut.
bool validateNode(const ASTNode& node,
                  const std::vector<ASTNode>& nodes,
                  const std::vector<int>& children,
                  std::string& errOut);

} // namespace ast_schema

#endif // ICC_AST_SCHEMA_H
