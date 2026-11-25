#ifndef ICC_AST_H
#define ICC_AST_H

#include <string>
#include <vector>

// Section 4.1 (Specification v1.4): Abstract Syntax Tree Node
// Created in RAM by Phase 2 Parser -> Serialized to .ast by AstWriter
// Deserialized from .ast into independent RAM by AstReader -> Consumed by Phase 3+
struct ASTNode {
    int nodeId = 0;                // Pre-order integer ID (0 .. TotalNodes - 1; 0 is always root PROG)
    int parentId = -1;             // Parent nodeId (-1 for root PROG node)
    int childIndex = -1;           // 0-based index among parent's children (-1 for root PROG node)
    std::string nodeKind;          // One of the 23 AST node kinds defined in Section 4.2
    std::string astOp;             // Operator/type tag ("+", "-", "*", "/", "<", "=", "medalo", "int32", "@NONE")
    std::string lexeme;            // Source identifier/literal text, or "@NONE" if not applicable
    long long litVal = -1;         // Numeric value for INT_LIT / ASCII code for CHAR_LIT; -1 otherwise
    int line = 1;                  // 1-based source line number (Section 4.2-C)
    std::vector<int> childIds;     // Reconstructed in RAM from parentId and childIndex
};

#endif // ICC_AST_H