#ifndef ICC_SEMANTIC_ANALYZER_H
#define ICC_SEMANTIC_ANALYZER_H

#include <map>
#include <string>
#include <vector>

#include "ast.h"
#include "sym.h"

// Phase 3 Two-Pass Semantic Analyzer (Specification v1.4, Sections 3.4, 3.6, 4.1 & 5.4)
//
// File-decoupled pipeline:
//   <prog>.ast  --AstReader-->  (tree)
//   <prog>.lex  --LexReader-->  (only to recover each block's 'khtm' line for LINE_END)
//   earlier .sym --SymReader--> (Sections 1-2 imports, command-line order)
//   -> Pass 1 (struct/function headers) -> Pass 2 (bodies, scopes, types, annotations)
//   -> SymbolTable  --SymWriter-->  <prog>.sym
//
// No in-memory AST or SymbolTable is ever accepted from another phase.
class SemanticAnalyzer {
public:
    SemanticAnalyzer() = default;

    // Reads inputAstPath and inputLexPath (the .lex the .ast was parsed from) and
    // every importSymPaths entry in order, then runs both passes. Clears all
    // previous state at entry. On failure the symbol table is cleared and the
    // diagnostic (SEMANTIC_ERROR_* or a propagated FATAL_IO_* / OS error) recorded.
    bool analyze(const std::string& inputAstPath,
                 const std::string& inputLexPath,
                 const std::vector<std::string>& importSymPaths = {});

    // Serializes the table of the last successful analyze() through SymWriter,
    // enforcing NODE_TYPES == @TOTAL_NODES of the analyzed .ast.
    bool writeSym(const std::string& outputSymPath);

    const SymbolTable& getSymbolTable() const;
    const std::string& getSourceFilename() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;

private:
    struct ExprType {
        std::string type;      // concrete type, "INT_LITERAL", or "khali"
        long long value = 0;   // meaningful when type == "INT_LITERAL"
    };

    struct FunctionState {
        std::string name;
        std::string returnType;
        std::map<std::string, int> symbolByName;        // function-wide unique names -> symId
        std::map<std::string, std::map<std::string, int>> childCounters;  // scopePath -> kind -> count
    };

    // Inputs
    std::vector<ASTNode> nodes_;
    std::vector<int> blockEndLine_;     // per AST node: 'khtm' line for BLOCK_CST nodes, else -1

    // Output
    SymbolTable table_;
    std::string sourceFilename_;
    int analyzedNodeCount_ = -1;        // @TOTAL_NODES of the last successful analysis

    // Pass state
    std::map<std::string, std::size_t> structByName_;
    std::map<std::string, std::size_t> funcByName_;
    std::map<int, std::size_t> funcByNode_;     // FUNC_DEF node -> funcId
    std::map<int, long long> foldedValue_;      // pure INT_LITERAL BIN_EXPR node -> value
    FunctionState fn_;

    // Diagnostics
    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;

    void reset();
    // decorate == false keeps a propagated reader message verbatim (it already names its code)
    bool fail(const std::string& code, int line, const std::string& detail, bool decorate = true);

    bool loadInputs(const std::string& astPath, const std::string& lexPath);
    bool importSym(const std::string& symPath);
    void pass1();
    void pass2();

    // Pass 2 helpers (throw on the first semantic error)
    void checkFunction(int funcNode);
    void checkBlock(int blockNode, const std::string& scopePath, const std::string& parentPath);
    std::string childScopePath(const std::string& parentPath, const std::string& kind);
    void declareVariable(int declNode, const std::string& scopePath);
    void checkStatement(int stmtNode, const std::string& scopePath);
    void checkCondition(int condNode, const std::string& scopePath);
    void checkCallArguments(int callNode, const FunctionInfo& callee, const std::string& scopePath);

    ExprType evalExpr(int node, const std::string& scopePath);
    ExprType evalFieldAccess(int node, const std::string& scopePath);
    int lookupVariable(int node, const std::string& name, const std::string& scopePath);
    void coerceTo(int exprNode, const ExprType& et, const std::string& target,
                  const std::string& mismatchCode, int errorLine);
    void adaptLiteral(int node, const std::string& target);
    void annotateLiteralSubtree(int node, const std::string& target);

    const ASTNode& node(int id) const;
    NodeTypeAnnotation& ann(int id);
};

#endif // ICC_SEMANTIC_ANALYZER_H
