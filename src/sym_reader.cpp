#include "sym_reader.h"

#include "sym_schema.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace {

enum class Stage { ExpectFormat, ExpectSource, ExpectCounts, ReadRecords, AfterEndFile };

std::string formatOsError(const std::string& action, const std::string& path, int errNum) {
    std::ostringstream oss;
    oss << "OS I/O error: failed to " << action << " '" << path << "'";
    if (errNum != 0) {
        oss << " (" << std::strerror(errNum) << ")";
    }
    return oss.str();
}

// Section 5.1 Rule 2: split on raw '|', then remove at most one trailing space
// (all fields except the last) and at most one leading space (all except the first).
std::vector<std::string> splitPipeFramedLine(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = line.find('|', start);
        if (bar == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    const std::size_t count = fields.size();
    for (std::size_t i = 0; i < count; ++i) {
        std::string& f = fields[i];
        if (i > 0 && !f.empty() && f.front() == ' ') {
            f.erase(0, 1);
        }
        if (i + 1 < count && !f.empty() && f.back() == ' ') {
            f.pop_back();
        }
    }
    return fields;
}

// Section 5.1 Rule 3: single-pass Class A decoding; only %25 %7C %0A %0D are legal.
bool unescapeClassA(const std::string& encoded, std::string& out) {
    out.clear();
    std::size_t i = 0;
    while (i < encoded.size()) {
        if (encoded[i] != '%') {
            out.push_back(encoded[i++]);
            continue;
        }
        if (i + 2 >= encoded.size()) {
            return false;
        }
        const char h1 = encoded[i + 1];
        const char h2 = encoded[i + 2];
        if (h1 == '2' && h2 == '5') {
            out.push_back('%');
        } else if (h1 == '7' && h2 == 'C') {
            out.push_back('|');
        } else if (h1 == '0' && h2 == 'A') {
            out.push_back('\n');
        } else if (h1 == '0' && h2 == 'D') {
            out.push_back('\r');
        } else {
            return false;
        }
        i += 3;
    }
    return true;
}

// Class N: ^-?[0-9]+$ with overflow protection
bool parseStrictInt64(const std::string& field, long long& outVal) {
    if (field.empty()) {
        return false;
    }
    std::size_t pos = 0;
    bool negative = false;
    if (field[0] == '-') {
        negative = true;
        pos = 1;
        if (pos == field.size()) {
            return false;
        }
    }
    const unsigned long long maxPos =
        static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    const unsigned long long limit = negative ? (maxPos + 1ULL) : maxPos;
    unsigned long long acc = 0;
    for (; pos < field.size(); ++pos) {
        const char c = field[pos];
        if (c < '0' || c > '9') {
            return false;
        }
        const unsigned long long digit = static_cast<unsigned long long>(c - '0');
        if (acc > (limit - digit) / 10ULL) {
            return false;
        }
        acc = acc * 10ULL + digit;
    }
    if (!negative) {
        outVal = static_cast<long long>(acc);
    } else if (acc == maxPos + 1ULL) {
        outVal = std::numeric_limits<long long>::min();
    } else {
        outVal = -static_cast<long long>(acc);
    }
    return true;
}

bool parseStrictInt(const std::string& field, int& outVal) {
    long long v = 0;
    if (!parseStrictInt64(field, v) ||
        v < static_cast<long long>(std::numeric_limits<int>::min()) ||
        v > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    outVal = static_cast<int>(v);
    return true;
}

// Parses one "KEY=<non-negative int>" item of the @COUNTS header.
bool parseCountItem(const std::string& field, const std::string& key, int& outVal) {
    const std::string prefix = key + "=";
    if (field.compare(0, prefix.size(), prefix) != 0 || field.size() == prefix.size()) {
        return false;
    }
    const std::string digits = field.substr(prefix.size());
    if (digits[0] == '-') {
        return false;
    }
    return parseStrictInt(digits, outVal);
}

// Record tag -> (section number, expected field count)
bool recordShape(const std::string& tag, int& section, std::size_t& fieldCount) {
    if (tag == "@STRUCT_DEF")   { section = 1; fieldCount = 7; return true; }
    if (tag == "@STRUCT_FIELD") { section = 1; fieldCount = 5; return true; }
    if (tag == "@FUNC_DEF")     { section = 2; fieldCount = 8; return true; }
    if (tag == "@FUNC_PARAM")   { section = 2; fieldCount = 5; return true; }
    if (tag == "@SCOPE_DEF")    { section = 3; fieldCount = 7; return true; }
    if (tag == "@SYMBOL")       { section = 4; fieldCount = 8; return true; }
    if (tag == "@NODE_TYPE")    { section = 5; fieldCount = 8; return true; }
    return false;
}

} // namespace

const SymbolTable& SymReader::getSymbolTable() const {
    return table_;
}

const std::string& SymReader::getSourceFilename() const {
    return sourceFilename_;
}

bool SymReader::hasError() const {
    return hasError_;
}

const std::string& SymReader::getErrorCode() const {
    return errorCode_;
}

const std::string& SymReader::getErrorMessage() const {
    return errorMessage_;
}

int SymReader::getErrorLine() const {
    return errorLine_;
}

bool SymReader::fail(const std::string& code, int fileLine, const std::string& detail) {
    table_ = SymbolTable();
    sourceFilename_.clear();
    hasError_ = true;
    errorCode_ = code;
    errorLine_ = fileLine;
    std::ostringstream oss;
    if (!code.empty()) {
        oss << code;
        if (fileLine > 0) {
            oss << " (line " << fileLine << ")";
        }
        oss << ": ";
    }
    oss << detail;
    errorMessage_ = oss.str();
    return false;
}

bool SymReader::read(const std::string& inputSymPath, int expectedTotalNodes) {
    table_ = SymbolTable();
    sourceFilename_.clear();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;

    const std::string fmt = "FATAL_IO_FORMAT_MISMATCH";
    const std::string cnt = "FATAL_IO_COUNT_MISMATCH";
    const std::string inv = "FATAL_IO_INVARIANT_VIOLATION";
    const std::string esc = "FATAL_IO_MALFORMED_ESCAPE";

    try {
        errno = 0;
        std::ifstream in(inputSymPath, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            return fail("", 0, formatOsError("open input file", inputSymPath, errno));
        }

        Stage stage = Stage::ExpectFormat;
        int declared[6] = {0, 0, 0, 0, 0, 0};   // index 1..5 = STRUCTS .. NODE_TYPES
        int currentSection = 1;
        int countsLine = 0;
        int lineNum = 0;
        std::vector<int> recordLines[6];          // file line of each counted record, per section
        std::map<std::string, std::size_t> structIdx;
        std::map<std::string, std::size_t> funcIdx;
        std::vector<int> declaredFieldCount;      // FIELD_COUNT per struct
        std::vector<int> declaredParamCount;      // PARAM_COUNT per function

        auto decodeClassA = [&](const std::string& raw, std::string& out, const std::string& what) -> bool {
            if (raw.empty()) {
                return fail(fmt, lineNum, what + " field is empty (use @NONE)");
            }
            if (raw == "@NONE") {
                out.clear();
                return true;
            }
            if (!unescapeClassA(raw, out)) {
                return fail(esc, lineNum, "Malformed percent-escape sequence in " + what);
            }
            return true;
        };
        auto classB = [&](const std::string& value, const std::string& what) -> bool {
            if (!sym_schema::isClassBField(value)) {
                return fail(fmt, lineNum, "Invalid Class B " + what + " syntax: '" + value + "'");
            }
            return true;
        };
        auto intField = [&](const std::string& value, int& out, const std::string& what) -> bool {
            if (!parseStrictInt(value, out)) {
                return fail(fmt, lineNum, "Malformed " + what + " field: '" + value + "'");
            }
            return true;
        };
        auto flagField = [&](const std::string& value, bool& out, const std::string& what) -> bool {
            int v = 0;
            if (!intField(value, v, what)) {
                return false;
            }
            if (v != 0 && v != 1) {
                return fail(inv, lineNum, what + " must be 0 or 1, found " + value);
            }
            out = (v == 1);
            return true;
        };

        std::string rawLine;
        while (std::getline(in, rawLine)) {
            ++lineNum;

            if (lineNum == 1 && rawLine.size() >= 3 &&
                static_cast<unsigned char>(rawLine[0]) == 0xEF &&
                static_cast<unsigned char>(rawLine[1]) == 0xBB &&
                static_cast<unsigned char>(rawLine[2]) == 0xBF) {
                return fail(fmt, 1, "UTF-8 BOM is not allowed; .sym files must be UTF-8 without BOM");
            }
            if (!rawLine.empty() && rawLine.back() == '\r') {
                rawLine.pop_back();
            }
            if (rawLine.empty() || rawLine[0] == '#') {
                continue;  // uncounted empty and comment lines (Section 5.1 Rule 5)
            }

            const std::vector<std::string> f = splitPipeFramedLine(rawLine);

            if (stage == Stage::ExpectFormat) {
                if (f.size() != 3 || f[0] != "@FORMAT" || f[1] != "ICC_SYM" || f[2] != "v1.0") {
                    return fail(fmt, lineNum, "Expected header '@FORMAT | ICC_SYM | v1.0'");
                }
                stage = Stage::ExpectSource;
                continue;
            }
            if (stage == Stage::ExpectSource) {
                if (f.size() != 2 || f[0] != "@SOURCE") {
                    return fail(fmt, lineNum, "Expected header '@SOURCE | <source_filename>'");
                }
                if (!decodeClassA(f[1], sourceFilename_, "@SOURCE")) {
                    return false;
                }
                stage = Stage::ExpectCounts;
                continue;
            }
            if (stage == Stage::ExpectCounts) {
                static const char* const kKeys[5] = {"STRUCTS", "FUNCS", "SCOPES", "SYMBOLS", "NODE_TYPES"};
                if (f.size() != 6 || f[0] != "@COUNTS") {
                    return fail(fmt, lineNum,
                                "Expected header '@COUNTS | STRUCTS=<S> | FUNCS=<F> | SCOPES=<C> | "
                                "SYMBOLS=<L> | NODE_TYPES=<T>'");
                }
                for (int k = 0; k < 5; ++k) {
                    if (!parseCountItem(f[static_cast<std::size_t>(k + 1)], kKeys[k], declared[k + 1])) {
                        return fail(fmt, lineNum, std::string("Malformed @COUNTS item; expected ") +
                                                      kKeys[k] + "=<non-negative integer>, found '" +
                                                      f[static_cast<std::size_t>(k + 1)] + "'");
                    }
                }
                countsLine = lineNum;
                stage = Stage::ReadRecords;
                continue;
            }
            if (stage == Stage::AfterEndFile) {
                return fail(fmt, lineNum, "Unexpected non-comment data after @END_FILE");
            }

            // ---- Stage::ReadRecords ---------------------------------------
            if (f.size() == 1 && f[0] == "@END_FILE") {
                stage = Stage::AfterEndFile;
                continue;
            }

            int section = 0;
            std::size_t expectedFields = 0;
            if (!recordShape(f[0], section, expectedFields)) {
                return fail(fmt, lineNum, "Unknown record tag '" + f[0] + "'");
            }
            if (f.size() != expectedFields) {
                return fail(fmt, lineNum, f[0] + " record must have " + std::to_string(expectedFields) +
                                          " fields, found " + std::to_string(f.size()));
            }
            if (section < currentSection) {
                return fail(fmt, lineNum, f[0] + " record appears after a later section has started");
            }
            currentSection = section;

            const std::string& tag = f[0];
            const bool isCountedRow = (tag != "@STRUCT_FIELD" && tag != "@FUNC_PARAM");
            if (isCountedRow &&
                recordLines[section].size() >= static_cast<std::size_t>(declared[section])) {
                return fail(cnt, lineNum, "More " + tag + " records than declared in @COUNTS (" +
                                          std::to_string(declared[section]) + ")");
            }

            if (tag == "@STRUCT_DEF") {
                StructInfo s;
                int fieldCount = 0;
                if (!intField(f[1], s.structId, "STRUCT_ID") || !classB(f[2], "STRUCT_NAME") ||
                    !intField(f[3], fieldCount, "FIELD_COUNT") ||
                    !flagField(f[4], s.isExternal, "IS_EXTERNAL") ||
                    !decodeClassA(f[5], s.sourceFile, "SOURCE_FILE") ||
                    !intField(f[6], s.lineDeclared, "LINE_DECLARED")) {
                    return false;
                }
                s.structName = f[2];
                const int expectedId = static_cast<int>(table_.structs.size());
                if (s.structId != expectedId) {
                    return fail(inv, lineNum, "Expected STRUCT_ID " + std::to_string(expectedId) +
                                              ", found " + std::to_string(s.structId));
                }
                if (fieldCount < 0) {
                    return fail(inv, lineNum, "FIELD_COUNT must be >= 0");
                }
                if (!structIdx.emplace(s.structName, table_.structs.size()).second) {
                    return fail(inv, lineNum, "Duplicate STRUCT_NAME '" + s.structName + "'");
                }
                declaredFieldCount.push_back(fieldCount);
                table_.structs.push_back(s);
            } else if (tag == "@STRUCT_FIELD") {
                FieldInfo fi;
                if (!classB(f[1], "STRUCT_NAME") || !intField(f[2], fi.fieldIndex, "FIELD_INDEX") ||
                    !classB(f[3], "FIELD_NAME") || !classB(f[4], "FIELD_TYPE")) {
                    return false;
                }
                const auto it = structIdx.find(f[1]);
                if (it == structIdx.end()) {
                    return fail(inv, lineNum, "@STRUCT_FIELD names undefined struct '" + f[1] + "'");
                }
                StructInfo& owner = table_.structs[it->second];
                const int have = static_cast<int>(owner.fields.size());
                if (have >= declaredFieldCount[it->second]) {
                    return fail(cnt, lineNum, "struct '" + owner.structName + "' has more @STRUCT_FIELD rows "
                                              "than its FIELD_COUNT " +
                                              std::to_string(declaredFieldCount[it->second]));
                }
                if (fi.fieldIndex != have) {
                    return fail(inv, lineNum, "struct '" + owner.structName + "' expected FIELD_INDEX " +
                                              std::to_string(have) + ", found " + std::to_string(fi.fieldIndex));
                }
                fi.fieldName = f[3];
                fi.fieldType = f[4];
                owner.fields.push_back(fi);
                continue;  // not a counted row; no line bookkeeping
            } else if (tag == "@FUNC_DEF") {
                FunctionInfo fn;
                int paramCount = 0;
                if (!intField(f[1], fn.funcId, "FUNC_ID") || !classB(f[2], "FUNC_NAME") ||
                    !classB(f[3], "RETURN_TYPE") || !intField(f[4], paramCount, "PARAM_COUNT") ||
                    !flagField(f[5], fn.isExternal, "IS_EXTERNAL") ||
                    !decodeClassA(f[6], fn.sourceFile, "SOURCE_FILE") ||
                    !intField(f[7], fn.lineDeclared, "LINE_DECLARED")) {
                    return false;
                }
                fn.funcName = f[2];
                fn.returnType = f[3];
                const int expectedId = static_cast<int>(table_.functions.size());
                if (fn.funcId != expectedId) {
                    return fail(inv, lineNum, "Expected FUNC_ID " + std::to_string(expectedId) +
                                              ", found " + std::to_string(fn.funcId));
                }
                if (paramCount < 0) {
                    return fail(inv, lineNum, "PARAM_COUNT must be >= 0");
                }
                if (!funcIdx.emplace(fn.funcName, table_.functions.size()).second) {
                    return fail(inv, lineNum, "Duplicate FUNC_NAME '" + fn.funcName + "'");
                }
                declaredParamCount.push_back(paramCount);
                table_.functions.push_back(fn);
            } else if (tag == "@FUNC_PARAM") {
                ParamInfo pi;
                if (!classB(f[1], "FUNC_NAME") || !intField(f[2], pi.paramIndex, "PARAM_INDEX") ||
                    !classB(f[3], "PARAM_NAME") || !classB(f[4], "PARAM_TYPE")) {
                    return false;
                }
                const auto it = funcIdx.find(f[1]);
                if (it == funcIdx.end()) {
                    return fail(inv, lineNum, "@FUNC_PARAM names undefined function '" + f[1] + "'");
                }
                FunctionInfo& owner = table_.functions[it->second];
                const int have = static_cast<int>(owner.params.size());
                if (have >= declaredParamCount[it->second]) {
                    return fail(cnt, lineNum, "function '" + owner.funcName + "' has more @FUNC_PARAM rows "
                                              "than its PARAM_COUNT " +
                                              std::to_string(declaredParamCount[it->second]));
                }
                if (pi.paramIndex != have) {
                    return fail(inv, lineNum, "function '" + owner.funcName + "' expected PARAM_INDEX " +
                                              std::to_string(have) + ", found " + std::to_string(pi.paramIndex));
                }
                pi.paramName = f[3];
                pi.paramType = f[4];
                owner.params.push_back(pi);
                continue;
            } else if (tag == "@SCOPE_DEF") {
                ScopeInfo sc;
                if (!intField(f[1], sc.scopeId, "SCOPE_ID") || !classB(f[2], "SCOPE_PATH") ||
                    !classB(f[3], "PARENT_SCOPE_PATH") || !classB(f[4], "ENCLOSING_FUNC") ||
                    !intField(f[5], sc.lineStart, "LINE_START") || !intField(f[6], sc.lineEnd, "LINE_END")) {
                    return false;
                }
                sc.scopePath = f[2];
                sc.parentScopePath = f[3];
                sc.enclosingFunc = f[4];
                const int expectedId = static_cast<int>(table_.scopes.size());
                if (sc.scopeId != expectedId) {
                    return fail(inv, lineNum, "Expected SCOPE_ID " + std::to_string(expectedId) +
                                              ", found " + std::to_string(sc.scopeId));
                }
                table_.scopes.push_back(sc);
            } else if (tag == "@SYMBOL") {
                SymbolTableEntry sy;
                if (!intField(f[1], sy.symId, "SYM_ID") || !classB(f[2], "IDENT_NAME") ||
                    !classB(f[3], "SYM_KIND") || !classB(f[4], "DATA_TYPE") ||
                    !classB(f[5], "ENCLOSING_FUNC") || !classB(f[6], "SCOPE_PATH") ||
                    !intField(f[7], sy.lineDeclared, "LINE_DECLARED")) {
                    return false;
                }
                sy.identName = f[2];
                sy.symKind = f[3];
                sy.dataType = f[4];
                sy.enclosingFunc = f[5];
                sy.scopePath = f[6];
                const int expectedId = static_cast<int>(table_.symbols.size());
                if (sy.symId != expectedId) {
                    return fail(inv, lineNum, "Expected SYM_ID " + std::to_string(expectedId) +
                                              ", found " + std::to_string(sy.symId));
                }
                table_.symbols.push_back(sy);
            } else {  // @NODE_TYPE
                NodeTypeAnnotation nt;
                if (!intField(f[1], nt.astNodeId, "AST_NODE_ID") || !classB(f[2], "EVALUATED_TYPE") ||
                    !classB(f[3], "REF_KIND") || !intField(f[4], nt.resolvedId, "RESOLVED_ID") ||
                    !intField(f[5], nt.resolvedSubId, "RESOLVED_SUB_ID") ||
                    !flagField(f[6], nt.isConstExpr, "IS_CONST_EXPR")) {
                    return false;
                }
                if (!parseStrictInt64(f[7], nt.foldedConstVal)) {
                    return fail(fmt, lineNum, "Malformed FOLDED_CONST_VAL field: '" + f[7] + "'");
                }
                nt.evaluatedType = f[2];
                nt.refKind = f[3];
                const int expectedId = static_cast<int>(table_.nodeTypes.size());
                if (nt.astNodeId != expectedId) {
                    return fail(inv, lineNum, "Expected AST_NODE_ID " + std::to_string(expectedId) +
                                              ", found " + std::to_string(nt.astNodeId));
                }
                table_.nodeTypes.push_back(nt);
            }
            recordLines[section].push_back(lineNum);
        }

        if (in.bad()) {
            return fail("", lineNum, formatOsError("read input file", inputSymPath, errno));
        }
        if (stage == Stage::ExpectFormat || stage == Stage::ExpectSource || stage == Stage::ExpectCounts) {
            return fail(fmt, lineNum, "Incomplete or missing required .sym header block");
        }
        if (stage != Stage::AfterEndFile) {
            return fail("FATAL_IO_MISSING_END_FILE", lineNum, "Reached end of file without encountering '@END_FILE'");
        }

        // Rule 1: every section's counted records match @COUNTS
        static const char* const kSectionTags[6] = {"", "@STRUCT_DEF", "@FUNC_DEF", "@SCOPE_DEF",
                                                    "@SYMBOL", "@NODE_TYPE"};
        for (int s = 1; s <= 5; ++s) {
            if (recordLines[s].size() != static_cast<std::size_t>(declared[s])) {
                return fail(cnt, countsLine, "Expected " + std::to_string(declared[s]) + " " + kSectionTags[s] +
                                             " record(s), found " + std::to_string(recordLines[s].size()));
            }
        }
        // Rule 2: every struct/function has exactly FIELD_COUNT / PARAM_COUNT detail rows
        for (std::size_t i = 0; i < table_.structs.size(); ++i) {
            if (static_cast<int>(table_.structs[i].fields.size()) != declaredFieldCount[i]) {
                return fail(cnt, recordLines[1][i], "struct '" + table_.structs[i].structName + "' declares FIELD_COUNT " +
                                                    std::to_string(declaredFieldCount[i]) + " but has " +
                                                    std::to_string(table_.structs[i].fields.size()) + " @STRUCT_FIELD row(s)");
            }
        }
        for (std::size_t i = 0; i < table_.functions.size(); ++i) {
            if (static_cast<int>(table_.functions[i].params.size()) != declaredParamCount[i]) {
                return fail(cnt, recordLines[2][i], "function '" + table_.functions[i].funcName + "' declares PARAM_COUNT " +
                                                    std::to_string(declaredParamCount[i]) + " but has " +
                                                    std::to_string(table_.functions[i].params.size()) + " @FUNC_PARAM row(s)");
            }
        }

        // Rules 2-5 (cross-references, domains, uniqueness) via the shared schema
        sym_schema::Violation v;
        if (!sym_schema::validateTable(table_, sourceFilename_, expectedTotalNodes, v)) {
            int line = countsLine;
            if (v.section >= 1 && v.section <= 5 && v.index >= 0 &&
                static_cast<std::size_t>(v.index) < recordLines[v.section].size()) {
                line = recordLines[v.section][static_cast<std::size_t>(v.index)];
            }
            return fail(inv, line, v.message);
        }
        return true;
    } catch (const std::exception& ex) {
        return fail("", 0, std::string("Unexpected runtime exception while reading '") + inputSymPath + "': " + ex.what());
    } catch (...) {
        return fail("", 0, "Unknown runtime exception while reading '" + inputSymPath + "'");
    }
}
