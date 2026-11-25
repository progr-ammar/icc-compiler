#include "ast_schema.h"

#include <cstddef>
#include <unordered_set>

namespace ast_schema {

namespace {

constexpr long long kMaxUint32 = 4294967295LL;

bool isReturnType(const std::string& s) {
    return isPrimitiveType(s) || s == "khali";
}

bool isRelOp(const std::string& op) {
    return op == "<" || op == ">" || op == "=" || op == "!=" || op == "<=" || op == ">=";
}

bool isArithOp(const std::string& op) {
    return op == "+" || op == "-" || op == "*" || op == "/";
}

bool isExprKind(const std::string& kind) {
    return kind == "BIN_EXPR" || kind == "FUNC_CALL_EXPR" || kind == "FIELD_ACCESS" ||
           kind == "VAR_REF" || kind == "INT_LIT" || kind == "CHAR_LIT";
}

bool isCondKind(const std::string& kind) {
    return kind == "REL_EXPR" || isExprKind(kind);
}

bool isStmtKind(const std::string& kind) {
    return kind == "ASSIGN_STMT" || kind == "WHILE_STMT" || kind == "IF_STMT" ||
           kind == "RETURN_STMT" || kind == "CALL_STMT" || kind == "BLOCK_CST";
}

// Parses a non-empty decimal digit string; rejects anything above 4294967295.
bool parseDigitString(const std::string& s, long long& out) {
    if (s.empty()) {
        return false;
    }
    long long acc = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        acc = acc * 10 + static_cast<long long>(c - '0');
        if (acc > kMaxUint32) {
            return false;
        }
    }
    out = acc;
    return true;
}

} // namespace

bool isPrimitiveType(const std::string& s) {
    return s == "int16" || s == "int32" || s == "uint16" || s == "uint32" || s == "char";
}

bool isCituIdentifier(const std::string& s) {
    static const std::unordered_set<std::string> kReservedOrForbidden = {
        "struct", "fn", "main", "khali", "wapis", "shro", "khtm", "jabtak",
        "agar", "magar", "medalo", "int16", "int32", "uint16", "uint32", "char",
        "class", "do"
    };
    if (s.empty()) {
        return false;
    }
    const char first = s.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) {
        return false;
    }
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return kReservedOrForbidden.find(s) == kReservedOrForbidden.end();
}

bool decodeCharLiteral(const std::string& lexeme, long long& outValue) {
    if (lexeme.size() == 3 && lexeme[0] == '\'' && lexeme[2] == '\'') {
        const unsigned char c = static_cast<unsigned char>(lexeme[1]);
        if (c < 32 || c > 126 || c == '\'' || c == '\\') {
            return false;
        }
        outValue = static_cast<long long>(c);
        return true;
    }
    if (lexeme.size() == 4 && lexeme[0] == '\'' && lexeme[1] == '\\' && lexeme[3] == '\'') {
        switch (lexeme[2]) {
            case 'n':  outValue = 10; return true;
            case 't':  outValue = 9;  return true;
            case '0':  outValue = 0;  return true;
            case '\\': outValue = 92; return true;
            case '\'': outValue = 39; return true;
            default:   return false;
        }
    }
    return false;
}

bool validateNode(const ASTNode& node,
                  const std::vector<ASTNode>& nodes,
                  const std::vector<int>& children,
                  std::string& errOut) {
    const std::string& k = node.nodeKind;
    const std::string& op = node.astOp;
    const std::string& lex = node.lexeme;
    const bool noLit = (node.litVal == -1LL);

    auto childKind = [&](std::size_t idx) -> const std::string& {
        return nodes[static_cast<std::size_t>(children[idx])].nodeKind;
    };
    auto failWith = [&](const std::string& msg) {
        errOut = k + ": " + msg;
        return false;
    };

    if (k == "PROG") {
        if (op != "@NONE" || lex != "@NONE" || !noLit) {
            return failWith("must have AST_OP='@NONE', LEXEME='@NONE', LIT_VAL=-1");
        }
        bool seenFunc = false;
        for (std::size_t i = 0; i < children.size(); ++i) {
            const std::string& ck = childKind(i);
            if (ck == "STRUCT_DEF") {
                if (seenFunc) {
                    return failWith("STRUCT_DEF child appears after a FUNC_DEF child");
                }
            } else if (ck == "FUNC_DEF") {
                seenFunc = true;
            } else {
                return failWith("invalid child NODE_KIND '" + ck + "'");
            }
        }
        if (!seenFunc) {
            return failWith("must contain at least one FUNC_DEF child");
        }
        return true;
    }

    if (k == "STRUCT_DEF") {
        if (op != "struct" || !isCituIdentifier(lex) || !noLit) {
            return failWith("requires AST_OP='struct', identifier LEXEME, LIT_VAL=-1");
        }
        if (children.empty()) {
            return failWith("must have at least one FIELD_DECL child");
        }
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (childKind(i) != "FIELD_DECL") {
                return failWith("children must all be FIELD_DECL");
            }
        }
        return true;
    }

    if (k == "FIELD_DECL" || k == "PARAM_DECL" || k == "VAR_DECL") {
        if (!isPrimitiveType(op) || !isCituIdentifier(lex) || !noLit || !children.empty()) {
            return failWith("requires primitive-type AST_OP, identifier LEXEME, LIT_VAL=-1, no children");
        }
        return true;
    }

    if (k == "FUNC_DEF") {
        if (!isReturnType(op) || !(isCituIdentifier(lex) || lex == "main") || !noLit) {
            return failWith("requires return-type AST_OP, identifier or 'main' LEXEME, LIT_VAL=-1");
        }
        if (children.empty()) {
            return failWith("must have a BLOCK_CST child");
        }
        for (std::size_t i = 0; i + 1 < children.size(); ++i) {
            if (childKind(i) != "PARAM_DECL") {
                return failWith("leading children must be PARAM_DECL");
            }
        }
        if (childKind(children.size() - 1) != "BLOCK_CST") {
            return failWith("final child must be BLOCK_CST");
        }
        return true;
    }

    if (k == "BLOCK_CST") {
        if (op != "@NONE" || lex != "@NONE" || !noLit ||
            children.size() != 2 || childKind(0) != "DECL_LIST" || childKind(1) != "STMT_LIST") {
            return failWith("must have attributes @NONE/@NONE/-1 and exactly [DECL_LIST, STMT_LIST] children");
        }
        return true;
    }

    if (k == "DECL_LIST") {
        if (op != "@NONE" || lex != "@NONE" || !noLit) {
            return failWith("attributes must be @NONE/@NONE/-1");
        }
        for (std::size_t i = 0; i < children.size(); ++i) {
            const std::string& ck = childKind(i);
            if (ck != "VAR_DECL" && ck != "STRUCT_VAR_DECL") {
                return failWith("children must be VAR_DECL or STRUCT_VAR_DECL");
            }
        }
        return true;
    }

    if (k == "STMT_LIST") {
        if (op != "@NONE" || lex != "@NONE" || !noLit) {
            return failWith("attributes must be @NONE/@NONE/-1");
        }
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (!isStmtKind(childKind(i))) {
                return failWith("non-statement child '" + childKind(i) + "'");
            }
        }
        return true;
    }

    if (k == "STRUCT_VAR_DECL") {
        if (!isCituIdentifier(op) || !isCituIdentifier(lex) || !noLit || !children.empty()) {
            return failWith("requires struct-type identifier AST_OP, identifier LEXEME, LIT_VAL=-1, no children");
        }
        return true;
    }

    if (k == "ASSIGN_STMT") {
        if (op != "medalo" || lex != "medalo" || !noLit || children.size() != 2 ||
            (childKind(0) != "VAR_REF" && childKind(0) != "FIELD_ACCESS") || !isExprKind(childKind(1))) {
            return failWith("requires medalo/medalo/-1 and [VAR_REF|FIELD_ACCESS, <EXPR>] children");
        }
        return true;
    }

    if (k == "WHILE_STMT") {
        if (op != "jabtak" || lex != "jabtak" || !noLit || children.size() != 2 ||
            !isCondKind(childKind(0)) || childKind(1) != "BLOCK_CST") {
            return failWith("requires jabtak/jabtak/-1 and [condition, BLOCK_CST] children");
        }
        return true;
    }

    if (k == "IF_STMT") {
        if (op != "agar" || lex != "agar" || !noLit ||
            (children.size() != 2 && children.size() != 3) ||
            !isCondKind(childKind(0)) || childKind(1) != "BLOCK_CST" ||
            (children.size() == 3 && childKind(2) != "BLOCK_CST")) {
            return failWith("requires agar/agar/-1 and [condition, BLOCK_CST(, BLOCK_CST)] children");
        }
        return true;
    }

    if (k == "RETURN_STMT") {
        if (op != "wapis" || lex != "wapis" || !noLit || children.size() != 1 ||
            (!isExprKind(childKind(0)) && childKind(0) != "VOID_KHALI")) {
            return failWith("requires wapis/wapis/-1 and exactly one <EXPR> or VOID_KHALI child");
        }
        return true;
    }

    if (k == "CALL_STMT" || k == "FUNC_CALL_EXPR") {
        if (op != "call" || !isCituIdentifier(lex) || !noLit) {
            return failWith("requires AST_OP='call', identifier callee LEXEME, LIT_VAL=-1");
        }
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (!isExprKind(childKind(i))) {
                return failWith("argument children must be expressions");
            }
        }
        return true;
    }

    if (k == "REL_EXPR" || k == "BIN_EXPR") {
        const bool opOk = (k == "REL_EXPR") ? isRelOp(op) : isArithOp(op);
        if (!opOk || lex != op || !noLit || children.size() != 2 ||
            !isExprKind(childKind(0)) || !isExprKind(childKind(1))) {
            return failWith("requires operator AST_OP == LEXEME, LIT_VAL=-1 and two <EXPR> children");
        }
        return true;
    }

    if (k == "FIELD_ACCESS") {
        if (op != "." || !isCituIdentifier(lex) || !noLit ||
            children.size() != 1 || childKind(0) != "VAR_REF") {
            return failWith("requires AST_OP='.', identifier field LEXEME, LIT_VAL=-1 and one VAR_REF child");
        }
        return true;
    }

    if (k == "VAR_REF") {
        if (op != "@NONE" || !isCituIdentifier(lex) || !noLit || !children.empty()) {
            return failWith("requires AST_OP='@NONE', identifier LEXEME, LIT_VAL=-1, no children");
        }
        return true;
    }

    if (k == "INT_LIT") {
        long long value = -1;
        if (op != "@NONE" || !children.empty() || !parseDigitString(lex, value) || value != node.litVal) {
            return failWith("requires AST_OP='@NONE', digit LEXEME whose value equals LIT_VAL (0..4294967295), no children");
        }
        return true;
    }

    if (k == "CHAR_LIT") {
        long long value = -1;
        if (op != "@NONE" || !children.empty() || !decodeCharLiteral(lex, value) || value != node.litVal) {
            return failWith("requires AST_OP='@NONE', valid quoted literal LEXEME whose ASCII code equals LIT_VAL, no children");
        }
        return true;
    }

    if (k == "VOID_KHALI") {
        if (op != "khali" || lex != "khali" || !noLit || !children.empty()) {
            return failWith("requires khali/khali/-1 and no children");
        }
        return true;
    }

    errOut = "Unknown NODE_KIND '" + k + "'";
    return false;
}

} // namespace ast_schema
