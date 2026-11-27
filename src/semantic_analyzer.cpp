#include "semantic_analyzer.h"

#include <climits>
#include <cstddef>
#include <exception>
#include <set>
#include <sstream>
#include <utility>

#include "ast_reader.h"
#include "lex_reader.h"
#include "sym_reader.h"
#include "sym_schema.h"
#include "sym_writer.h"

namespace {

const char* const kIntLiteral = "INT_LITERAL";

// Thrown internally to halt analysis at the first semantic error (Section 3.6 codes).
struct SemanticError {
    std::string code;
    int line;
    std::string detail;
};

[[noreturn]] void raise(const std::string& code, int line, const std::string& detail) {
    throw SemanticError{"SEMANTIC_ERROR_" + code, line, detail};
}

bool isIntegerType(const std::string& t) {
    return t == "int16" || t == "int32" || t == "uint16" || t == "uint32";
}

// Section 3.4 Rule 1 ranges for the four integer types an INT_LITERAL may adopt.
void rangeOf(const std::string& t, long long& lo, long long& hi) {
    if (t == "int16") {
        lo = -32768LL;
        hi = 32767LL;
    } else if (t == "uint16") {
        lo = 0;
        hi = 65535LL;
    } else if (t == "int32") {
        lo = -2147483648LL;
        hi = 2147483647LL;
    } else {  // uint32
        lo = 0;
        hi = 4294967295LL;
    }
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Section 3.4 Rule 5 "Reserved Names": "_start", or "M" + digits + "_L_" ...
bool isReservedFunctionName(const std::string& name) {
    if (name == "_start") {
        return true;
    }
    if (name.size() < 5 || name[0] != 'M') {
        return false;
    }
    std::size_t i = 1;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
        ++i;
    }
    return i > 1 && name.compare(i, 3, "_L_") == 0;
}

bool sameFields(const StructInfo& a, const StructInfo& b) {
    if (a.fields.size() != b.fields.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.fields.size(); ++i) {
        if (a.fields[i].fieldName != b.fields[i].fieldName || a.fields[i].fieldType != b.fields[i].fieldType) {
            return false;
        }
    }
    return true;
}

bool sameSignature(const FunctionInfo& a, const FunctionInfo& b) {
    if (a.returnType != b.returnType || a.params.size() != b.params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        if (a.params[i].paramName != b.params[i].paramName || a.params[i].paramType != b.params[i].paramType) {
            return false;
        }
    }
    return true;
}

std::string quote(const std::string& s) {
    return "'" + s + "'";
}

} // namespace

// ============================================================================
// Accessors, reset and diagnostics
// ============================================================================

const SymbolTable& SemanticAnalyzer::getSymbolTable() const {
    return table_;
}

const std::string& SemanticAnalyzer::getSourceFilename() const {
    return sourceFilename_;
}

bool SemanticAnalyzer::hasError() const {
    return hasError_;
}

const std::string& SemanticAnalyzer::getErrorCode() const {
    return errorCode_;
}

const std::string& SemanticAnalyzer::getErrorMessage() const {
    return errorMessage_;
}

int SemanticAnalyzer::getErrorLine() const {
    return errorLine_;
}

void SemanticAnalyzer::reset() {
    nodes_.clear();
    blockEndLine_.clear();
    table_ = SymbolTable();
    sourceFilename_.clear();
    analyzedNodeCount_ = -1;
    structByName_.clear();
    funcByName_.clear();
    funcByNode_.clear();
    foldedValue_.clear();
    fn_ = FunctionState();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;
}

bool SemanticAnalyzer::fail(const std::string& code, int line, const std::string& detail, bool decorate) {
    const std::string source = sourceFilename_;
    nodes_.clear();
    blockEndLine_.clear();
    table_ = SymbolTable();
    sourceFilename_.clear();
    analyzedNodeCount_ = -1;
    hasError_ = true;
    errorCode_ = code;
    errorLine_ = line;
    std::ostringstream oss;
    if (decorate && !code.empty()) {
        oss << code;
        if (!source.empty()) {
            oss << " in " << source;
        }
        if (line > 0) {
            oss << " at Line " << line;
        }
        oss << ": ";
    }
    oss << detail;
    errorMessage_ = oss.str();
    return false;
}

const ASTNode& SemanticAnalyzer::node(int id) const {
    return nodes_[static_cast<std::size_t>(id)];
}

NodeTypeAnnotation& SemanticAnalyzer::ann(int id) {
    return table_.nodeTypes[static_cast<std::size_t>(id)];
}

// ============================================================================
// Entry points
// ============================================================================

bool SemanticAnalyzer::analyze(const std::string& inputAstPath,
                               const std::string& inputLexPath,
                               const std::vector<std::string>& importSymPaths) {
    reset();
    try {
        if (!loadInputs(inputAstPath, inputLexPath)) {
            return false;
        }

        // Every node starts with the structural annotation (@NONE, @NONE, -1, -1, 0, -1)
        table_.nodeTypes.resize(nodes_.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            NodeTypeAnnotation& a = table_.nodeTypes[i];
            a.astNodeId = static_cast<int>(i);
            a.evaluatedType = "@NONE";
            a.refKind = "@NONE";
        }

        for (const std::string& symPath : importSymPaths) {
            if (!importSym(symPath)) {
                return false;
            }
        }
        pass1();
        pass2();

        // Self-check against the shared .sym rules before declaring success
        sym_schema::Violation v;
        if (!sym_schema::validateTable(table_, sourceFilename_, static_cast<int>(nodes_.size()), v)) {
            return fail("FATAL_IO_INVARIANT_VIOLATION", 0,
                        "internal error: analyzed symbol table violates the .sym schema: " + v.message);
        }
        analyzedNodeCount_ = static_cast<int>(nodes_.size());
        return true;
    } catch (const SemanticError& e) {
        return fail(e.code, e.line, e.detail);
    } catch (const std::exception& ex) {
        return fail("", 0, std::string("Unexpected runtime exception during semantic analysis: ") + ex.what());
    }
}

bool SemanticAnalyzer::writeSym(const std::string& outputSymPath) {
    if (analyzedNodeCount_ < 0) {
        hasError_ = true;
        errorCode_.clear();
        errorLine_ = 0;
        errorMessage_ = "writeSym() requires a preceding successful analyze()";
        return false;
    }
    SymWriter writer;
    if (!writer.write(outputSymPath, sourceFilename_, table_, analyzedNodeCount_)) {
        hasError_ = true;
        errorCode_ = writer.getErrorCode();
        errorLine_ = 0;
        errorMessage_ = writer.getErrorMessage();
        return false;
    }
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;
    return true;
}

// ============================================================================
// Input loading (.ast via AstReader, .lex via LexReader for 'khtm' lines)
// ============================================================================

bool SemanticAnalyzer::loadInputs(const std::string& astPath, const std::string& lexPath) {
    AstReader astReader;
    if (!astReader.read(astPath)) {
        return fail(astReader.getErrorCode(), astReader.getErrorLine(), astReader.getErrorMessage(), false);
    }
    LexReader lexReader;
    if (!lexReader.read(lexPath)) {
        return fail(lexReader.getErrorCode(), lexReader.getErrorLine(), lexReader.getErrorMessage(), false);
    }
    nodes_ = astReader.getAstNodes();
    sourceFilename_ = astReader.getSourceFilename();

    if (lexReader.getSourceFilename() != sourceFilename_) {
        return fail("FATAL_IO_INVARIANT_VIOLATION", 0,
                    ".lex @SOURCE " + quote(lexReader.getSourceFilename()) + " does not match .ast @SOURCE " +
                    quote(sourceFilename_));
    }

    // Pair every block 'shro' token with its 'khtm'. Struct-definition 'shro' tokens
    // (pattern: 'struct' ID 'shro') are not BLOCK_CST nodes and are skipped.
    const std::vector<Token>& toks = lexReader.getTokens();
    std::vector<int> starts;
    std::vector<int> ends;
    std::vector<int> stack;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].terminal == "shro") {
            const bool isStruct = i >= 2 && toks[i - 2].terminal == "struct" && toks[i - 1].terminal == "ID";
            if (isStruct) {
                stack.push_back(-1);
            } else {
                stack.push_back(static_cast<int>(starts.size()));
                starts.push_back(toks[i].line);
                ends.push_back(-1);
            }
        } else if (toks[i].terminal == "khtm") {
            if (stack.empty()) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", 0, ".lex has an unmatched 'khtm' token");
            }
            const int ordinal = stack.back();
            stack.pop_back();
            if (ordinal >= 0) {
                ends[static_cast<std::size_t>(ordinal)] = toks[i].line;
            }
        }
    }
    if (!stack.empty()) {
        return fail("FATAL_IO_INVARIANT_VIOLATION", 0, ".lex has an unmatched 'shro' token");
    }

    blockEndLine_.assign(nodes_.size(), -1);
    std::size_t ordinal = 0;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].nodeKind != "BLOCK_CST") {
            continue;
        }
        if (ordinal >= starts.size() || starts[ordinal] != nodes_[i].line) {
            return fail("FATAL_IO_INVARIANT_VIOLATION", 0,
                        "the .lex file does not correspond to the .ast (block 'shro' tokens disagree)");
        }
        blockEndLine_[i] = ends[ordinal];
        ++ordinal;
    }
    if (ordinal != starts.size()) {
        return fail("FATAL_IO_INVARIANT_VIOLATION", 0,
                    "the .lex file does not correspond to the .ast (block count differs)");
    }
    return true;
}

// ============================================================================
// Cross-file import of an earlier unit's Sections 1-2 (Section 5.4)
// ============================================================================

bool SemanticAnalyzer::importSym(const std::string& symPath) {
    SymReader reader;
    if (!reader.read(symPath)) {
        return fail(reader.getErrorCode(), reader.getErrorLine(), "cannot import " + quote(symPath) + ": " + reader.getErrorMessage(), false);
    }
    const SymbolTable& other = reader.getSymbolTable();

    for (const StructInfo& s : other.structs) {
        if (s.sourceFile == sourceFilename_) {
            raise("DUPLICATE_GLOBAL_SYMBOL", 0, "struct " + quote(s.structName) + " imported from " + quote(symPath) +
                                                " claims to be defined by this unit " + quote(sourceFilename_));
        }
        const auto it = structByName_.find(s.structName);
        if (it != structByName_.end()) {
            const StructInfo& prev = table_.structs[it->second];
            if (prev.sourceFile == s.sourceFile && sameFields(prev, s)) {
                continue;  // repeated (transitive) load of the same definition is ignored
            }
            raise("DUPLICATE_GLOBAL_SYMBOL", 0, "struct " + quote(s.structName) + " from " + quote(s.sourceFile) +
                                                " conflicts with the one already imported from " + quote(prev.sourceFile));
        }
        StructInfo copy;
        copy.structId = static_cast<int>(table_.structs.size());
        copy.structName = s.structName;
        copy.isExternal = true;
        copy.sourceFile = s.sourceFile;
        copy.lineDeclared = s.lineDeclared;
        for (const FieldInfo& f : s.fields) {
            FieldInfo fc;
            fc.fieldIndex = f.fieldIndex;
            fc.fieldName = f.fieldName;
            fc.fieldType = f.fieldType;
            copy.fields.push_back(fc);
        }
        structByName_[copy.structName] = table_.structs.size();
        table_.structs.push_back(copy);
    }

    for (const FunctionInfo& f : other.functions) {
        if (f.funcName == "main") {
            continue;  // 'main' is never imported (Section 5.4 import rule 1)
        }
        if (f.sourceFile == sourceFilename_) {
            raise("DUPLICATE_GLOBAL_SYMBOL", 0, "function " + quote(f.funcName) + " imported from " + quote(symPath) +
                                                " claims to be defined by this unit " + quote(sourceFilename_));
        }
        const auto it = funcByName_.find(f.funcName);
        if (it != funcByName_.end()) {
            const FunctionInfo& prev = table_.functions[it->second];
            if (prev.sourceFile == f.sourceFile && sameSignature(prev, f)) {
                continue;
            }
            raise("DUPLICATE_GLOBAL_SYMBOL", 0, "function " + quote(f.funcName) + " from " + quote(f.sourceFile) +
                                                " conflicts with the one already imported from " + quote(prev.sourceFile));
        }
        FunctionInfo copy;
        copy.funcId = static_cast<int>(table_.functions.size());
        copy.funcName = f.funcName;
        copy.returnType = f.returnType;
        copy.isExternal = true;
        copy.sourceFile = f.sourceFile;
        copy.lineDeclared = f.lineDeclared;
        for (const ParamInfo& p : f.params) {
            ParamInfo pc;
            pc.paramIndex = p.paramIndex;
            pc.paramName = p.paramName;
            pc.paramType = p.paramType;
            copy.params.push_back(pc);
        }
        funcByName_[copy.funcName] = table_.functions.size();
        table_.functions.push_back(copy);
    }
    return true;
}

// ============================================================================
// Pass 1: register local STRUCT_DEF and FUNC_DEF headers (Section 3.4 Rule 5)
// ============================================================================

void SemanticAnalyzer::pass1() {
    bool seenMain = false;
    for (int top : node(0).childIds) {
        const ASTNode& n = node(top);

        if (n.nodeKind == "STRUCT_DEF") {
            const auto it = structByName_.find(n.lexeme);
            if (it != structByName_.end()) {
                if (table_.structs[it->second].isExternal) {
                    raise("DUPLICATE_GLOBAL_SYMBOL", n.line, "struct " + quote(n.lexeme) +
                          " is already defined by imported unit " + quote(table_.structs[it->second].sourceFile));
                }
                raise("DUPLICATE_DECLARATION", n.line, "struct " + quote(n.lexeme) + " is defined more than once");
            }
            StructInfo s;
            s.structId = static_cast<int>(table_.structs.size());
            s.structName = n.lexeme;
            s.isExternal = false;
            s.sourceFile = sourceFilename_;
            s.lineDeclared = n.line;
            std::set<std::string> fieldNames;
            for (int fid : n.childIds) {
                const ASTNode& fd = node(fid);
                if (!fieldNames.insert(fd.lexeme).second) {
                    raise("DUPLICATE_DECLARATION", fd.line, "field " + quote(fd.lexeme) +
                                                            " is declared more than once in struct " + quote(n.lexeme));
                }
                FieldInfo f;
                f.fieldIndex = static_cast<int>(s.fields.size());
                f.fieldName = fd.lexeme;
                f.fieldType = fd.astOp;
                NodeTypeAnnotation& a = ann(fid);
                a.evaluatedType = fd.astOp;
                a.refKind = "STRUCT_FIELD_DECL";
                a.resolvedId = s.structId;
                a.resolvedSubId = f.fieldIndex;
                s.fields.push_back(f);
            }
            NodeTypeAnnotation& a = ann(top);
            a.evaluatedType = "struct " + n.lexeme;
            a.refKind = "STRUCT_TYPE";
            a.resolvedId = s.structId;
            structByName_[s.structName] = table_.structs.size();
            table_.structs.push_back(s);
            continue;
        }

        // FUNC_DEF
        const std::string& name = n.lexeme;
        std::vector<int> params(n.childIds.begin(), n.childIds.end() - 1);
        if (isReservedFunctionName(name)) {
            raise("RESERVED_NAME", n.line, "function name " + quote(name) + " is reserved for linker-generated symbols");
        }
        if (name == "main") {
            if (seenMain) {
                raise("DUPLICATE_MAIN", n.line, "more than one 'main' is defined in this file");
            }
            seenMain = true;
            if (n.astOp != "int32" || !params.empty()) {
                raise("INVALID_MAIN_SIGNATURE", n.line, "'main' must be exactly 'fn int32 main ( )'");
            }
        }
        const auto it = funcByName_.find(name);
        if (it != funcByName_.end()) {
            if (table_.functions[it->second].isExternal) {
                raise("DUPLICATE_GLOBAL_SYMBOL", n.line, "function " + quote(name) +
                      " is already defined by imported unit " + quote(table_.functions[it->second].sourceFile));
            }
            raise("DUPLICATE_DECLARATION", n.line, "function " + quote(name) + " is defined more than once");
        }
        FunctionInfo f;
        f.funcId = static_cast<int>(table_.functions.size());
        f.funcName = name;
        f.returnType = n.astOp;
        f.isExternal = false;
        f.sourceFile = sourceFilename_;
        f.lineDeclared = n.line;
        std::set<std::string> paramNames;
        for (int pid : params) {
            const ASTNode& pd = node(pid);
            if (!paramNames.insert(pd.lexeme).second) {
                raise("DUPLICATE_DECLARATION", pd.line, "parameter " + quote(pd.lexeme) +
                                                        " is declared more than once in function " + quote(name));
            }
            ParamInfo p;
            p.paramIndex = static_cast<int>(f.params.size());
            p.paramName = pd.lexeme;
            p.paramType = pd.astOp;
            f.params.push_back(p);
        }
        NodeTypeAnnotation& a = ann(top);
        a.evaluatedType = f.returnType;
        a.refKind = "FUNC_REF";
        a.resolvedId = f.funcId;
        funcByName_[name] = table_.functions.size();
        funcByNode_[top] = table_.functions.size();
        table_.functions.push_back(f);
    }
}

// ============================================================================
// Pass 2: function bodies
// ============================================================================

void SemanticAnalyzer::pass2() {
    for (int top : node(0).childIds) {
        if (node(top).nodeKind == "FUNC_DEF") {
            checkFunction(top);
        }
    }
}

void SemanticAnalyzer::checkFunction(int funcNode) {
    const ASTNode& fnNode = node(funcNode);
    const FunctionInfo& info = table_.functions[funcByNode_.at(funcNode)];
    fn_ = FunctionState();
    fn_.name = info.funcName;
    fn_.returnType = info.returnType;
    const std::string rootPath = info.funcName;

    // Parameters are function-root symbols (PARAM), numbered before the locals
    for (std::size_t i = 0; i + 1 < fnNode.childIds.size(); ++i) {
        const int pid = fnNode.childIds[i];
        const ASTNode& pd = node(pid);
        SymbolTableEntry sy;
        sy.symId = static_cast<int>(table_.symbols.size());
        sy.identName = pd.lexeme;
        sy.symKind = "PARAM";
        sy.dataType = pd.astOp;
        sy.enclosingFunc = fn_.name;
        sy.scopePath = rootPath;
        sy.lineDeclared = pd.line;
        fn_.symbolByName[sy.identName] = sy.symId;
        NodeTypeAnnotation& a = ann(pid);
        a.evaluatedType = sy.dataType;
        a.refKind = "LOCAL_SYM";
        a.resolvedId = sy.symId;
        table_.symbols.push_back(sy);
    }

    const int body = fnNode.childIds.back();
    checkBlock(body, rootPath, "@NONE");

    // Guaranteed Return Rule: the top-level SLIST must end with 'wapis'
    const ASTNode& stmtList = node(node(body).childIds[1]);
    if (stmtList.childIds.empty() || node(stmtList.childIds.back()).nodeKind != "RETURN_STMT") {
        const int endLine = blockEndLine_[static_cast<std::size_t>(body)];
        raise("MISSING_RETURN", endLine > 0 ? endLine : fnNode.line,
              "function " + quote(fn_.name) + " must end its top-level statement list with 'wapis'");
    }
}

std::string SemanticAnalyzer::childScopePath(const std::string& parentPath, const std::string& kind) {
    const int k = ++fn_.childCounters[parentPath][kind];
    return parentPath + "." + kind + "_" + std::to_string(k);
}

void SemanticAnalyzer::checkBlock(int blockNode, const std::string& scopePath, const std::string& parentPath) {
    ScopeInfo sc;
    sc.scopeId = static_cast<int>(table_.scopes.size());
    sc.scopePath = scopePath;
    sc.parentScopePath = parentPath;
    sc.enclosingFunc = fn_.name;
    sc.lineStart = node(blockNode).line;
    sc.lineEnd = blockEndLine_[static_cast<std::size_t>(blockNode)];
    table_.scopes.push_back(sc);

    const ASTNode& block = node(blockNode);
    for (int d : node(block.childIds[0]).childIds) {
        declareVariable(d, scopePath);
    }
    for (int s : node(block.childIds[1]).childIds) {
        checkStatement(s, scopePath);
    }
}

void SemanticAnalyzer::declareVariable(int declNode, const std::string& scopePath) {
    const ASTNode& d = node(declNode);
    SymbolTableEntry sy;
    if (d.nodeKind == "STRUCT_VAR_DECL") {
        if (structByName_.find(d.astOp) == structByName_.end()) {
            raise("UNKNOWN_STRUCT", d.line, "unknown struct type " + quote(d.astOp) + " in declaration of " + quote(d.lexeme));
        }
        sy.symKind = "STRUCT_VAR";
        sy.dataType = "struct " + d.astOp;
    } else {
        sy.symKind = "LOCAL_VAR";
        sy.dataType = d.astOp;
    }
    if (fn_.symbolByName.find(d.lexeme) != fn_.symbolByName.end()) {
        raise("DUPLICATE_DECLARATION", d.line, "name " + quote(d.lexeme) + " is already declared in function " +
                                               quote(fn_.name) + " (names are unique function-wide)");
    }
    sy.symId = static_cast<int>(table_.symbols.size());
    sy.identName = d.lexeme;
    sy.enclosingFunc = fn_.name;
    sy.scopePath = scopePath;
    sy.lineDeclared = d.line;
    fn_.symbolByName[sy.identName] = sy.symId;
    NodeTypeAnnotation& a = ann(declNode);
    a.evaluatedType = sy.dataType;
    a.refKind = "LOCAL_SYM";
    a.resolvedId = sy.symId;
    table_.symbols.push_back(sy);
}

void SemanticAnalyzer::checkStatement(int stmtNode, const std::string& scopePath) {
    const ASTNode& s = node(stmtNode);
    const std::string& k = s.nodeKind;

    if (k == "ASSIGN_STMT") {
        const int target = s.childIds[0];
        const int value = s.childIds[1];
        std::string targetType;
        if (node(target).nodeKind == "VAR_REF") {
            const int sid = lookupVariable(target, node(target).lexeme, scopePath);
            const SymbolTableEntry& sy = table_.symbols[static_cast<std::size_t>(sid)];
            if (startsWith(sy.dataType, "struct ")) {
                raise("STRUCT_VALUE_USE", node(target).line, "struct variable " + quote(sy.identName) +
                                                             " cannot be an assignment target (no struct copy)");
            }
            NodeTypeAnnotation& a = ann(target);
            a.evaluatedType = sy.dataType;
            a.refKind = "LOCAL_SYM";
            a.resolvedId = sid;
            targetType = sy.dataType;
        } else {
            targetType = evalFieldAccess(target, scopePath).type;
        }
        const ExprType v = evalExpr(value, scopePath);
        coerceTo(value, v, targetType, "TYPE_MISMATCH", s.line);
        ann(stmtNode).evaluatedType = targetType;
        return;
    }

    if (k == "WHILE_STMT") {
        checkCondition(s.childIds[0], scopePath);
        checkBlock(s.childIds[1], childScopePath(scopePath, "jabtak"), scopePath);
        return;
    }

    if (k == "IF_STMT") {
        checkCondition(s.childIds[0], scopePath);
        checkBlock(s.childIds[1], childScopePath(scopePath, "agar"), scopePath);
        if (s.childIds.size() == 3) {
            checkBlock(s.childIds[2], childScopePath(scopePath, "magar"), scopePath);
        }
        return;
    }

    if (k == "RETURN_STMT") {
        const int payload = s.childIds[0];
        const bool isVoidReturn = node(payload).nodeKind == "VOID_KHALI";
        if (fn_.returnType == "khali") {
            if (!isVoidReturn) {
                raise("RETURN_TYPE_MISMATCH", s.line, "khali function " + quote(fn_.name) + " must return with 'wapis khali'");
            }
            ann(payload).evaluatedType = "khali";
            ann(stmtNode).evaluatedType = "khali";
            return;
        }
        if (isVoidReturn) {
            raise("RETURN_TYPE_MISMATCH", s.line, "function " + quote(fn_.name) + " returns " + fn_.returnType +
                                                  " but 'wapis khali' returns no value");
        }
        const ExprType v = evalExpr(payload, scopePath);
        coerceTo(payload, v, fn_.returnType, "RETURN_TYPE_MISMATCH", s.line);
        ann(stmtNode).evaluatedType = fn_.returnType;
        return;
    }

    if (k == "CALL_STMT") {
        const auto it = funcByName_.find(s.lexeme);
        if (it == funcByName_.end()) {
            raise("UNDECLARED_FUNCTION", s.line, "call to undeclared function " + quote(s.lexeme));
        }
        const FunctionInfo& callee = table_.functions[it->second];
        checkCallArguments(stmtNode, callee, scopePath);
        NodeTypeAnnotation& a = ann(stmtNode);
        a.evaluatedType = callee.returnType;
        a.refKind = "FUNC_REF";
        a.resolvedId = callee.funcId;
        return;
    }

    // Standalone nested block statement
    checkBlock(stmtNode, childScopePath(scopePath, "shro"), scopePath);
}

void SemanticAnalyzer::checkCondition(int condNode, const std::string& scopePath) {
    const ExprType c = evalExpr(condNode, scopePath);
    if (c.type == "BOOL_COND" || isIntegerType(c.type)) {
        return;
    }
    if (c.type == kIntLiteral) {
        adaptLiteral(condNode, "int32");  // Section 3.4 Rule 2.4: standalone literal condition defaults to int32
        return;
    }
    raise("BAD_CONDITION_TYPE", node(condNode).line,
          "condition without a relational operator must have an integer type, found " + c.type);
}

void SemanticAnalyzer::checkCallArguments(int callNode, const FunctionInfo& callee, const std::string& scopePath) {
    const ASTNode& call = node(callNode);
    if (call.childIds.size() != callee.params.size()) {
        raise("TYPE_MISMATCH", call.line, "function " + quote(callee.funcName) + " expects " +
                                          std::to_string(callee.params.size()) + " argument(s), " +
                                          std::to_string(call.childIds.size()) + " given");
    }
    for (std::size_t i = 0; i < call.childIds.size(); ++i) {
        const int arg = call.childIds[i];
        const ExprType v = evalExpr(arg, scopePath);
        coerceTo(arg, v, callee.params[i].paramType, "TYPE_MISMATCH", node(arg).line);
    }
}

// ============================================================================
// Expressions
// ============================================================================

int SemanticAnalyzer::lookupVariable(int refNode, const std::string& name, const std::string& scopePath) {
    const auto it = fn_.symbolByName.find(name);
    if (it == fn_.symbolByName.end()) {
        raise("UNDECLARED_IDENTIFIER", node(refNode).line, "undeclared identifier " + quote(name));
    }
    const SymbolTableEntry& sy = table_.symbols[static_cast<std::size_t>(it->second)];
    const bool visible = scopePath == sy.scopePath || startsWith(scopePath, sy.scopePath + ".");
    if (!visible) {
        raise("UNDECLARED_IDENTIFIER", node(refNode).line, "identifier " + quote(name) + " (declared in scope " +
                                                           quote(sy.scopePath) + ") is not visible in scope " +
                                                           quote(scopePath));
    }
    return it->second;
}

SemanticAnalyzer::ExprType SemanticAnalyzer::evalFieldAccess(int faNode, const std::string& scopePath) {
    const ASTNode& fa = node(faNode);
    const int base = fa.childIds[0];
    const int sid = lookupVariable(base, node(base).lexeme, scopePath);
    const SymbolTableEntry& sy = table_.symbols[static_cast<std::size_t>(sid)];
    if (!startsWith(sy.dataType, "struct ")) {
        raise("TYPE_MISMATCH", fa.line, "field access " + quote(sy.identName + "." + fa.lexeme) +
                                        " on non-struct variable of type " + sy.dataType);
    }
    NodeTypeAnnotation& b = ann(base);
    b.evaluatedType = sy.dataType;
    b.refKind = "LOCAL_SYM";
    b.resolvedId = sid;

    const StructInfo& st = table_.structs[structByName_.at(sy.dataType.substr(7))];
    for (const FieldInfo& f : st.fields) {
        if (f.fieldName == fa.lexeme) {
            NodeTypeAnnotation& a = ann(faNode);
            a.evaluatedType = f.fieldType;
            a.refKind = "FIELD_ACCESS_REF";
            a.resolvedId = sid;
            a.resolvedSubId = f.fieldIndex;
            return ExprType{f.fieldType, 0};
        }
    }
    raise("UNKNOWN_FIELD", fa.line, "struct " + quote(st.structName) + " has no field " + quote(fa.lexeme));
}

SemanticAnalyzer::ExprType SemanticAnalyzer::evalExpr(int exprNode, const std::string& scopePath) {
    const ASTNode& n = node(exprNode);
    const std::string& k = n.nodeKind;

    if (k == "INT_LIT") {
        return ExprType{kIntLiteral, n.litVal};  // annotated once its type is fixed (adaptLiteral)
    }

    if (k == "CHAR_LIT") {
        NodeTypeAnnotation& a = ann(exprNode);
        a.evaluatedType = "char";
        a.isConstExpr = true;
        a.foldedConstVal = n.litVal;
        return ExprType{"char", 0};
    }

    if (k == "VAR_REF") {
        const int sid = lookupVariable(exprNode, n.lexeme, scopePath);
        const SymbolTableEntry& sy = table_.symbols[static_cast<std::size_t>(sid)];
        if (startsWith(sy.dataType, "struct ")) {
            raise("STRUCT_VALUE_USE", n.line, "struct variable " + quote(sy.identName) +
                                              " can only be used as the base of a field access");
        }
        NodeTypeAnnotation& a = ann(exprNode);
        a.evaluatedType = sy.dataType;
        a.refKind = "LOCAL_SYM";
        a.resolvedId = sid;
        return ExprType{sy.dataType, 0};
    }

    if (k == "FIELD_ACCESS") {
        return evalFieldAccess(exprNode, scopePath);
    }

    if (k == "FUNC_CALL_EXPR") {
        const auto it = funcByName_.find(n.lexeme);
        if (it == funcByName_.end()) {
            raise("UNDECLARED_FUNCTION", n.line, "call to undeclared function " + quote(n.lexeme));
        }
        const FunctionInfo& callee = table_.functions[it->second];
        if (callee.returnType == "khali") {
            raise("VOID_VALUE_USE", n.line, "khali function " + quote(callee.funcName) +
                                            " returns no value and can only be called as a statement");
        }
        checkCallArguments(exprNode, callee, scopePath);
        NodeTypeAnnotation& a = ann(exprNode);
        a.evaluatedType = callee.returnType;
        a.refKind = "FUNC_REF";
        a.resolvedId = callee.funcId;
        return ExprType{callee.returnType, 0};
    }

    const int lhs = n.childIds[0];
    const int rhs = n.childIds[1];
    const ExprType l = evalExpr(lhs, scopePath);
    const ExprType r = evalExpr(rhs, scopePath);

    if (k == "REL_EXPR") {
        if (l.type == kIntLiteral && r.type == kIntLiteral) {
            adaptLiteral(lhs, "int32");  // Section 3.4 Rule 2.4 defaulting
            adaptLiteral(rhs, "int32");
        } else if (l.type == kIntLiteral || r.type == kIntLiteral) {
            const bool leftLit = (l.type == kIntLiteral);
            const std::string& partner = leftLit ? r.type : l.type;
            if (!isIntegerType(partner)) {
                raise("TYPE_MISMATCH", n.line, "integer literal cannot be compared with a value of type " + partner);
            }
            adaptLiteral(leftLit ? lhs : rhs, partner);
        } else if (l.type != r.type) {
            raise("TYPE_MISMATCH", n.line, "relational operator " + quote(n.astOp) + " compares " + l.type +
                                           " with " + r.type);
        }
        ann(exprNode).evaluatedType = "BOOL_COND";
        return ExprType{"BOOL_COND", 0};
    }

    // BIN_EXPR
    if (l.type == "char" || r.type == "char") {
        raise("TYPE_MISMATCH", n.line, "arithmetic operator " + quote(n.astOp) + " is not allowed on char");
    }
    if (l.type == kIntLiteral && r.type == kIntLiteral) {
        // Section 3.4 Rule 2.2: fold pure INT_LITERAL sub-expressions in 64-bit signed arithmetic
        long long result = 0;
        const long long a = l.value;
        const long long b = r.value;
        bool overflow = false;
        if (n.astOp == "+") {
            overflow = __builtin_add_overflow(a, b, &result);
        } else if (n.astOp == "-") {
            overflow = __builtin_sub_overflow(a, b, &result);
        } else if (n.astOp == "*") {
            overflow = __builtin_mul_overflow(a, b, &result);
        } else {
            if (b == 0) {
                raise("DIVISION_BY_ZERO", n.line, "constant division by zero");
            }
            if (a == LLONG_MIN && b == -1) {
                overflow = true;
            } else {
                result = a / b;  // C++ integer division truncates toward zero
            }
        }
        if (overflow) {
            raise("CONSTANT_OVERFLOW", n.line, "constant expression " + quote(std::to_string(a) + " " + n.astOp + " " +
                                               std::to_string(b)) + " leaves the 64-bit signed range");
        }
        foldedValue_[exprNode] = result;
        return ExprType{kIntLiteral, result};
    }
    std::string resultType;
    if (l.type == kIntLiteral) {
        adaptLiteral(lhs, r.type);
        resultType = r.type;
    } else if (r.type == kIntLiteral) {
        adaptLiteral(rhs, l.type);
        resultType = l.type;
    } else {
        if (l.type != r.type) {
            raise("TYPE_MISMATCH", n.line, "arithmetic operator " + quote(n.astOp) + " combines " + l.type +
                                           " with " + r.type);
        }
        resultType = l.type;
    }
    ann(exprNode).evaluatedType = resultType;
    return ExprType{resultType, 0};
}

void SemanticAnalyzer::coerceTo(int exprNode, const ExprType& et, const std::string& target,
                                const std::string& mismatchCode, int errorLine) {
    if (et.type == kIntLiteral) {
        if (!isIntegerType(target)) {
            raise(mismatchCode, errorLine, "integer literal cannot adapt to type " + target);
        }
        adaptLiteral(exprNode, target);
        return;
    }
    if (et.type != target) {
        raise(mismatchCode, errorLine, "expected " + target + ", found " + et.type);
    }
}

void SemanticAnalyzer::adaptLiteral(int litNode, const std::string& target) {
    const ASTNode& n = node(litNode);
    const long long value = (n.nodeKind == "INT_LIT") ? n.litVal : foldedValue_.at(litNode);
    long long lo = 0;
    long long hi = 0;
    rangeOf(target, lo, hi);
    if (value < lo || value > hi) {
        raise("LITERAL_OVERFLOW", n.line, "constant " + std::to_string(value) + " is outside the range of " +
                                          target + " [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    annotateLiteralSubtree(litNode, target);
}

void SemanticAnalyzer::annotateLiteralSubtree(int litNode, const std::string& target) {
    const ASTNode& n = node(litNode);
    NodeTypeAnnotation& a = ann(litNode);
    a.evaluatedType = target;
    a.isConstExpr = true;
    a.foldedConstVal = (n.nodeKind == "INT_LIT") ? n.litVal : foldedValue_.at(litNode);
    if (n.nodeKind == "BIN_EXPR") {
        annotateLiteralSubtree(n.childIds[0], target);
        annotateLiteralSubtree(n.childIds[1], target);
    }
}
