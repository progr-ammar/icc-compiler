#include "sym_schema.h"

#include <cstddef>
#include <map>
#include <set>
#include <utility>

namespace sym_schema {

namespace {

bool isPrimitiveType(const std::string& s) {
    return s == "int16" || s == "int32" || s == "uint16" || s == "uint32" || s == "char";
}

// A required name field: Class B syntax and not the @NONE sentinel.
bool isNameField(const std::string& s) {
    return s != "@NONE" && isClassBField(s);
}

// "struct <Name>" with a non-empty, space-free Class B name; returns the name.
bool parseStructType(const std::string& s, std::string& nameOut) {
    const std::string prefix = "struct ";
    if (s.size() <= prefix.size() || s.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string name = s.substr(prefix.size());
    if (!isNameField(name) || name.find(' ') != std::string::npos) {
        return false;
    }
    nameOut = name;
    return true;
}

// DATA_TYPE domain (Section 4.1): primitive type or "struct <Name>"
bool isDataType(const std::string& s) {
    std::string ignored;
    return isPrimitiveType(s) || parseStructType(s, ignored);
}

// EVALUATED_TYPE domain (Section 4.1): concrete type, BOOL_COND, khali, struct, or @NONE
bool isEvaluatedType(const std::string& s) {
    return s == "@NONE" || s == "BOOL_COND" || s == "khali" || isDataType(s);
}

bool isRefKind(const std::string& s) {
    return s == "LOCAL_SYM" || s == "FUNC_REF" || s == "STRUCT_FIELD_DECL" ||
           s == "FIELD_ACCESS_REF" || s == "STRUCT_TYPE" || s == "@NONE";
}

bool fail(Violation& out, int section, int index, const std::string& msg) {
    out.section = section;
    out.index = index;
    out.message = msg;
    return false;
}

} // namespace

bool isClassBField(const std::string& s) {
    if (s == "@NONE") {
        return true;
    }
    if (s.empty() || s.front() == ' ' || s.back() == ' ') {
        return false;
    }
    for (char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' ||
                        c == '+' || c == '%' || c == '[' || c == ']' || c == ' ' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool validateTable(const SymbolTable& t,
                   const std::string& sourceFilename,
                   int expectedTotalNodes,
                   Violation& out) {
    out = Violation();

    // Shared Rule 3 check: IS_EXTERNAL == 0  <=>  SOURCE_FILE equals @SOURCE
    auto sourceRuleOk = [&](bool isExternal, const std::string& sourceFile) {
        return isExternal ? (sourceFile != sourceFilename) : (sourceFile == sourceFilename);
    };

    // ---- Section 1: Struct Blueprints ------------------------------------
    std::map<std::string, std::size_t> structByName;
    for (std::size_t i = 0; i < t.structs.size(); ++i) {
        const StructInfo& s = t.structs[i];
        const int id = static_cast<int>(i);
        if (s.structId != id) {
            return fail(out, 1, id, "STRUCT_ID " + std::to_string(s.structId) +
                                    " is not contiguous (expected " + std::to_string(id) + ")");
        }
        if (!isNameField(s.structName)) {
            return fail(out, 1, id, "STRUCT_NAME '" + s.structName + "' is not a valid Class B name");
        }
        if (!structByName.emplace(s.structName, i).second) {
            return fail(out, 1, id, "duplicate STRUCT_NAME '" + s.structName + "'");
        }
        if (!sourceRuleOk(s.isExternal, s.sourceFile)) {
            return fail(out, 1, id, "SOURCE_FILE must equal @SOURCE exactly when IS_EXTERNAL == 0");
        }
        if (s.lineDeclared < 1) {
            return fail(out, 1, id, "LINE_DECLARED must be >= 1");
        }
        for (std::size_t f = 0; f < s.fields.size(); ++f) {
            const FieldInfo& fi = s.fields[f];
            if (fi.fieldIndex != static_cast<int>(f)) {
                return fail(out, 1, id, "struct '" + s.structName + "' FIELD_INDEX " +
                                        std::to_string(fi.fieldIndex) + " out of order (expected " +
                                        std::to_string(f) + ")");
            }
            if (!isNameField(fi.fieldName)) {
                return fail(out, 1, id, "FIELD_NAME '" + fi.fieldName + "' is not a valid Class B name");
            }
            if (!isPrimitiveType(fi.fieldType)) {
                return fail(out, 1, id, "FIELD_TYPE '" + fi.fieldType + "' is not a primitive type");
            }
        }
    }

    // ---- Section 2: Function Signatures ----------------------------------
    std::set<std::string> funcNames;
    for (std::size_t i = 0; i < t.functions.size(); ++i) {
        const FunctionInfo& fn = t.functions[i];
        const int id = static_cast<int>(i);
        if (fn.funcId != id) {
            return fail(out, 2, id, "FUNC_ID " + std::to_string(fn.funcId) +
                                    " is not contiguous (expected " + std::to_string(id) + ")");
        }
        if (!isNameField(fn.funcName)) {
            return fail(out, 2, id, "FUNC_NAME '" + fn.funcName + "' is not a valid Class B name");
        }
        if (!funcNames.insert(fn.funcName).second) {
            return fail(out, 2, id, "duplicate FUNC_NAME '" + fn.funcName + "'");
        }
        if (!isPrimitiveType(fn.returnType) && fn.returnType != "khali") {
            return fail(out, 2, id, "RETURN_TYPE '" + fn.returnType + "' is not a primitive type or khali");
        }
        if (!sourceRuleOk(fn.isExternal, fn.sourceFile)) {
            return fail(out, 2, id, "SOURCE_FILE must equal @SOURCE exactly when IS_EXTERNAL == 0");
        }
        if (fn.lineDeclared < 1) {
            return fail(out, 2, id, "LINE_DECLARED must be >= 1");
        }
        for (std::size_t p = 0; p < fn.params.size(); ++p) {
            const ParamInfo& pi = fn.params[p];
            if (pi.paramIndex != static_cast<int>(p)) {
                return fail(out, 2, id, "function '" + fn.funcName + "' PARAM_INDEX " +
                                        std::to_string(pi.paramIndex) + " out of order (expected " +
                                        std::to_string(p) + ")");
            }
            if (!isNameField(pi.paramName)) {
                return fail(out, 2, id, "PARAM_NAME '" + pi.paramName + "' is not a valid Class B name");
            }
            if (!isPrimitiveType(pi.paramType)) {
                return fail(out, 2, id, "PARAM_TYPE '" + pi.paramType + "' is not a primitive type");
            }
        }
    }

    // ---- Section 3: Lexical Scope Hierarchy -------------------------------
    std::map<std::string, std::size_t> scopeByPath;
    for (std::size_t i = 0; i < t.scopes.size(); ++i) {
        const ScopeInfo& sc = t.scopes[i];
        const int id = static_cast<int>(i);
        if (sc.scopeId != id) {
            return fail(out, 3, id, "SCOPE_ID " + std::to_string(sc.scopeId) +
                                    " is not contiguous (expected " + std::to_string(id) + ")");
        }
        if (!isNameField(sc.scopePath) || !isNameField(sc.enclosingFunc) ||
            !isClassBField(sc.parentScopePath)) {
            return fail(out, 3, id, "SCOPE_PATH / PARENT_SCOPE_PATH / ENCLOSING_FUNC violate Class B syntax");
        }
        if (!scopeByPath.emplace(sc.scopePath, i).second) {
            return fail(out, 3, id, "duplicate SCOPE_PATH '" + sc.scopePath + "'");
        }
        if (sc.lineStart < 1 || sc.lineEnd < 1) {
            return fail(out, 3, id, "LINE_START and LINE_END must be >= 1");
        }
    }
    // Rule 4: PARENT_SCOPE_PATH is @NONE or an existing SCOPE_PATH of the same function
    for (std::size_t i = 0; i < t.scopes.size(); ++i) {
        const ScopeInfo& sc = t.scopes[i];
        if (sc.parentScopePath == "@NONE") {
            continue;
        }
        const auto it = scopeByPath.find(sc.parentScopePath);
        if (it == scopeByPath.end()) {
            return fail(out, 3, static_cast<int>(i),
                        "PARENT_SCOPE_PATH '" + sc.parentScopePath + "' is not a defined SCOPE_PATH");
        }
        if (t.scopes[it->second].enclosingFunc != sc.enclosingFunc) {
            return fail(out, 3, static_cast<int>(i),
                        "PARENT_SCOPE_PATH '" + sc.parentScopePath + "' belongs to a different function");
        }
        if (it->second == i) {
            return fail(out, 3, static_cast<int>(i), "scope is its own parent");
        }
    }

    // ---- Section 4: Function-Scoped Symbols --------------------------------
    std::set<std::pair<std::string, std::string>> funcIdentPairs;
    for (std::size_t i = 0; i < t.symbols.size(); ++i) {
        const SymbolTableEntry& sy = t.symbols[i];
        const int id = static_cast<int>(i);
        if (sy.symId != id) {
            return fail(out, 4, id, "SYM_ID " + std::to_string(sy.symId) +
                                    " is not contiguous (expected " + std::to_string(id) + ")");
        }
        if (!isNameField(sy.identName) || !isNameField(sy.enclosingFunc) || !isNameField(sy.scopePath)) {
            return fail(out, 4, id, "IDENT_NAME / ENCLOSING_FUNC / SCOPE_PATH violate Class B syntax");
        }
        if (sy.symKind != "PARAM" && sy.symKind != "LOCAL_VAR" && sy.symKind != "STRUCT_VAR") {
            return fail(out, 4, id, "SYM_KIND '" + sy.symKind + "' must be PARAM, LOCAL_VAR or STRUCT_VAR");
        }
        if (!isDataType(sy.dataType)) {
            return fail(out, 4, id, "DATA_TYPE '" + sy.dataType + "' is not a primitive or 'struct <Name>' type");
        }
        if (scopeByPath.find(sy.scopePath) == scopeByPath.end()) {
            return fail(out, 4, id, "SCOPE_PATH '" + sy.scopePath + "' is not a defined scope");
        }
        if (funcNames.find(sy.enclosingFunc) == funcNames.end()) {
            return fail(out, 4, id, "ENCLOSING_FUNC '" + sy.enclosingFunc + "' is not a defined function");
        }
        if (!funcIdentPairs.emplace(sy.enclosingFunc, sy.identName).second) {
            return fail(out, 4, id, "(ENCLOSING_FUNC, IDENT_NAME) = (" + sy.enclosingFunc + ", " +
                                    sy.identName + ") is not unique");
        }
        if (sy.lineDeclared < 1) {
            return fail(out, 4, id, "LINE_DECLARED must be >= 1");
        }
    }

    // ---- Section 5: AST Node Annotations -----------------------------------
    if (t.nodeTypes.empty()) {
        return fail(out, 5, -1, "NODE_TYPES must be >= 1 (it equals @TOTAL_NODES of the .ast)");
    }
    if (expectedTotalNodes >= 0 && t.nodeTypes.size() != static_cast<std::size_t>(expectedTotalNodes)) {
        return fail(out, 5, -1, "NODE_TYPES " + std::to_string(t.nodeTypes.size()) +
                                " does not equal @TOTAL_NODES " + std::to_string(expectedTotalNodes) +
                                " of the corresponding .ast");
    }
    const int numStructs = static_cast<int>(t.structs.size());
    const int numFuncs = static_cast<int>(t.functions.size());
    const int numSyms = static_cast<int>(t.symbols.size());
    for (std::size_t i = 0; i < t.nodeTypes.size(); ++i) {
        const NodeTypeAnnotation& nt = t.nodeTypes[i];
        const int id = static_cast<int>(i);
        if (nt.astNodeId != id) {
            return fail(out, 5, id, "AST_NODE_ID " + std::to_string(nt.astNodeId) +
                                    " is not contiguous (expected " + std::to_string(id) + ")");
        }
        if (!isEvaluatedType(nt.evaluatedType)) {
            return fail(out, 5, id, "EVALUATED_TYPE '" + nt.evaluatedType + "' is not a valid type");
        }
        if (!isRefKind(nt.refKind)) {
            return fail(out, 5, id, "REF_KIND '" + nt.refKind + "' is not a valid reference kind");
        }

        const std::string& rk = nt.refKind;
        int tableSize = 0;
        if (rk == "LOCAL_SYM" || rk == "FIELD_ACCESS_REF") {
            tableSize = numSyms;
        } else if (rk == "FUNC_REF") {
            tableSize = numFuncs;
        } else if (rk == "STRUCT_TYPE" || rk == "STRUCT_FIELD_DECL") {
            tableSize = numStructs;
        }
        if (rk == "@NONE") {
            if (nt.resolvedId != -1) {
                return fail(out, 5, id, "RESOLVED_ID must be -1 when REF_KIND is @NONE");
            }
        } else if (nt.resolvedId < 0 || nt.resolvedId >= tableSize) {
            return fail(out, 5, id, "RESOLVED_ID " + std::to_string(nt.resolvedId) +
                                    " is not a valid ID of the table named by REF_KIND " + rk);
        }

        // RESOLVED_SUB_ID: fieldIndex 0..F-1 for STRUCT_FIELD_DECL / FIELD_ACCESS_REF, else -1
        if (rk == "STRUCT_FIELD_DECL" || rk == "FIELD_ACCESS_REF") {
            int fieldCount = -1;
            if (rk == "STRUCT_FIELD_DECL") {
                fieldCount = static_cast<int>(t.structs[static_cast<std::size_t>(nt.resolvedId)].fields.size());
            } else {
                std::string structName;
                const SymbolTableEntry& base = t.symbols[static_cast<std::size_t>(nt.resolvedId)];
                const auto it = parseStructType(base.dataType, structName)
                                    ? structByName.find(structName)
                                    : structByName.end();
                if (it == structByName.end()) {
                    return fail(out, 5, id, "FIELD_ACCESS_REF symbol '" + base.identName +
                                            "' does not have a struct type defined in Section 1");
                }
                fieldCount = static_cast<int>(t.structs[it->second].fields.size());
            }
            if (nt.resolvedSubId < 0 || nt.resolvedSubId >= fieldCount) {
                return fail(out, 5, id, "RESOLVED_SUB_ID " + std::to_string(nt.resolvedSubId) +
                                        " is not a field index 0.." + std::to_string(fieldCount - 1));
            }
        } else if (nt.resolvedSubId != -1) {
            return fail(out, 5, id, "RESOLVED_SUB_ID must be -1 for REF_KIND " + rk);
        }

        if (!nt.isConstExpr && nt.foldedConstVal != -1) {
            return fail(out, 5, id, "FOLDED_CONST_VAL must be -1 when IS_CONST_EXPR == 0");
        }
    }

    return true;
}

} // namespace sym_schema
