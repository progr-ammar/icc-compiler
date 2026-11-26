#include "sym_writer.h"

#include "sym_schema.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <sstream>

namespace {

// Single-pass Class A percent-escaping (Specification v1.4, Section 5.1 Rule 3)
std::string escapeClassA(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        switch (c) {
            case '%':  out.append("%25"); break;
            case '|':  out.append("%7C"); break;
            case '\n': out.append("%0A"); break;
            case '\r': out.append("%0D"); break;
            default:   out.push_back(c);  break;
        }
    }
    return out;
}

// Class A field serialization: empty text is written as the @NONE sentinel (Section 5.1 Rule 4)
std::string serializeClassA(const std::string& raw) {
    return raw.empty() ? "@NONE" : escapeClassA(raw);
}

std::string formatOsError(const std::string& action, const std::string& path, int errNum) {
    std::ostringstream oss;
    oss << "OS I/O error: failed to " << action << " '" << path << "'";
    if (errNum != 0) {
        oss << " (" << std::strerror(errNum) << ")";
    }
    return oss.str();
}

std::string countNoun(std::size_t n, const char* singular, const char* plural) {
    return std::to_string(n) + " " + (n == 1 ? singular : plural);
}

const char* sectionName(int section) {
    switch (section) {
        case 1: return "Section 1 @STRUCT_DEF";
        case 2: return "Section 2 @FUNC_DEF";
        case 3: return "Section 3 @SCOPE_DEF";
        case 4: return "Section 4 @SYMBOL";
        case 5: return "Section 5 @NODE_TYPE";
        default: return "SymbolTable";
    }
}

} // namespace

bool SymWriter::hasError() const {
    return hasError_;
}

const std::string& SymWriter::getErrorCode() const {
    return errorCode_;
}

const std::string& SymWriter::getErrorMessage() const {
    return errorMessage_;
}

bool SymWriter::recordError(const std::string& code, const std::string& message) {
    hasError_ = true;
    errorCode_ = code;
    errorMessage_ = message;
    return false;
}

bool SymWriter::write(const std::string& outputSymPath,
                      const std::string& sourceFilename,
                      const SymbolTable& table,
                      int expectedTotalNodes) {
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();

    try {
        // The literal text "@NONE" in a Class A field would be read back as "empty"
        if (sourceFilename == "@NONE") {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               "Source filename literal '@NONE' collides with the v1.4 missing-value sentinel");
        }
        for (const StructInfo& s : table.structs) {
            if (s.sourceFile == "@NONE") {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   "struct '" + s.structName + "' SOURCE_FILE literal '@NONE' collides with the sentinel");
            }
        }
        for (const FunctionInfo& f : table.functions) {
            if (f.sourceFile == "@NONE") {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   "function '" + f.funcName + "' SOURCE_FILE literal '@NONE' collides with the sentinel");
            }
        }

        sym_schema::Violation v;
        if (!sym_schema::validateTable(table, sourceFilename, expectedTotalNodes, v)) {
            std::string where = sectionName(v.section);
            if (v.index >= 0) {
                where += "[" + std::to_string(v.index) + "]";
            }
            return recordError("FATAL_IO_INVARIANT_VIOLATION", where + ": " + v.message);
        }

        errno = 0;
        std::ofstream out(outputSymPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return recordError("", formatOsError("open output file", outputSymPath, errno));
        }

        const std::size_t numNodes = table.nodeTypes.size();

        // Header block (Sections 5.1 Rule 6 & 5.4)
        out << "@FORMAT | ICC_SYM | v1.0\n";
        out << "@SOURCE | " << serializeClassA(sourceFilename) << '\n';
        out << "@COUNTS | STRUCTS=" << table.structs.size()
            << " | FUNCS=" << table.functions.size()
            << " | SCOPES=" << table.scopes.size()
            << " | SYMBOLS=" << table.symbols.size()
            << " | NODE_TYPES=" << numNodes << '\n';

        // Section 1: Struct Blueprints (each @STRUCT_DEF followed by its @STRUCT_FIELD rows)
        out << "# --- SECTION 1: STRUCT BLUEPRINTS ("
            << countNoun(table.structs.size(), "Struct", "Structs") << ") ---\n";
        for (const StructInfo& s : table.structs) {
            out << "@STRUCT_DEF | " << s.structId << " | " << s.structName << " | "
                << s.fields.size() << " | " << (s.isExternal ? 1 : 0) << " | "
                << serializeClassA(s.sourceFile) << " | " << s.lineDeclared << '\n';
            for (const FieldInfo& f : s.fields) {
                out << "@STRUCT_FIELD | " << s.structName << " | " << f.fieldIndex << " | "
                    << f.fieldName << " | " << f.fieldType << '\n';
            }
        }

        // Section 2: Function Signatures (each @FUNC_DEF followed by its @FUNC_PARAM rows)
        out << "# --- SECTION 2: FUNCTION SIGNATURES ("
            << countNoun(table.functions.size(), "Function", "Functions") << ") ---\n";
        for (const FunctionInfo& fn : table.functions) {
            out << "@FUNC_DEF | " << fn.funcId << " | " << fn.funcName << " | " << fn.returnType << " | "
                << fn.params.size() << " | " << (fn.isExternal ? 1 : 0) << " | "
                << serializeClassA(fn.sourceFile) << " | " << fn.lineDeclared << '\n';
            for (const ParamInfo& p : fn.params) {
                out << "@FUNC_PARAM | " << fn.funcName << " | " << p.paramIndex << " | "
                    << p.paramName << " | " << p.paramType << '\n';
            }
        }

        // Section 3: Lexical Scope Hierarchy
        out << "# --- SECTION 3: LEXICAL SCOPE HIERARCHY ("
            << countNoun(table.scopes.size(), "Scope", "Scopes") << ") ---\n";
        for (const ScopeInfo& sc : table.scopes) {
            out << "@SCOPE_DEF | " << sc.scopeId << " | " << sc.scopePath << " | "
                << sc.parentScopePath << " | " << sc.enclosingFunc << " | "
                << sc.lineStart << " | " << sc.lineEnd << '\n';
        }

        // Section 4: Function-Scoped Symbols
        out << "# --- SECTION 4: FUNCTION-SCOPED SYMBOLS ("
            << countNoun(table.symbols.size(), "Symbol", "Symbols") << ") ---\n";
        for (const SymbolTableEntry& sy : table.symbols) {
            out << "@SYMBOL | " << sy.symId << " | " << sy.identName << " | " << sy.symKind << " | "
                << sy.dataType << " | " << sy.enclosingFunc << " | " << sy.scopePath << " | "
                << sy.lineDeclared << '\n';
        }

        // Section 5: AST Node Annotations
        out << "# --- SECTION 5: AST NODE SEMANTIC ANNOTATIONS (All "
            << countNoun(numNodes, "Node", "Nodes") << " 0.." << (numNodes - 1) << ") ---\n";
        for (const NodeTypeAnnotation& nt : table.nodeTypes) {
            out << "@NODE_TYPE | " << nt.astNodeId << " | " << nt.evaluatedType << " | "
                << nt.refKind << " | " << nt.resolvedId << " | " << nt.resolvedSubId << " | "
                << (nt.isConstExpr ? 1 : 0) << " | " << nt.foldedConstVal << '\n';
        }

        out << "@END_FILE\n";

        errno = 0;
        out.flush();
        if (!out.good()) {
            const int flushErr = errno;
            out.close();
            return recordError("", formatOsError("write/flush output file", outputSymPath, flushErr));
        }
        errno = 0;
        out.close();
        if (out.fail()) {
            return recordError("", formatOsError("close output file", outputSymPath, errno));
        }
        return true;
    } catch (const std::exception& ex) {
        return recordError("", std::string("Unexpected runtime exception while writing '") +
                                   outputSymPath + "': " + ex.what());
    } catch (...) {
        return recordError("", "Unknown runtime exception while writing '" + outputSymPath + "'");
    }
}
