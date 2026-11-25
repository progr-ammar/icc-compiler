#include "ast_reader.h"

#include "ast_schema.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace {

enum class AstParseStage {
    ExpectFormat,
    ExpectSource,
    ExpectTotalNodes,
    ReadRecords,
    AfterEndFile
};

std::string formatOsError(const std::string& action,
                          const std::string& path,
                          int errNum) {
    std::ostringstream oss;
    oss << "OS I/O error: failed to " << action << " '" << path << "'";
    if (errNum != 0) {
        oss << " (" << std::strerror(errNum) << ")";
    }
    return oss.str();
}

std::vector<std::string> splitPipeFramedLine(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (true) {
        const std::size_t barPos = line.find('|', start);
        if (barPos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, barPos - start));
        start = barPos + 1;
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

bool unescapeClassA(const std::string& encoded, std::string& decodedOut) {
    decodedOut.clear();
    decodedOut.reserve(encoded.size());

    std::size_t i = 0;
    while (i < encoded.size()) {
        if (encoded[i] != '%') {
            decodedOut.push_back(encoded[i]);
            ++i;
            continue;
        }

        if (i + 2 >= encoded.size()) {
            return false;
        }

        const char h1 = encoded[i + 1];
        const char h2 = encoded[i + 2];

        if (h1 == '2' && h2 == '5') {
            decodedOut.push_back('%');
        } else if (h1 == '7' && h2 == 'C') {
            decodedOut.push_back('|');
        } else if (h1 == '0' && h2 == 'A') {
            decodedOut.push_back('\n');
        } else if (h1 == '0' && h2 == 'D') {
            decodedOut.push_back('\r');
        } else {
            return false;
        }

        i += 3;
    }

    return true;
}

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
    long long val64 = 0;
    if (!parseStrictInt64(field, val64)) {
        return false;
    }
    if (val64 < static_cast<long long>(std::numeric_limits<int>::min()) ||
        val64 > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    outVal = static_cast<int>(val64);
    return true;
}

bool isClassBBaseChar(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == ':' || c == '+' ||
           c == '%' || c == '[' || c == ']' || c == ' ' || c == '-';
}

bool isValidClassBNodeKindSyntax(const std::string& s) {
    if (s.empty() || s == "@NONE" || s.front() == ' ' || s.back() == ' ') {
        return false;
    }
    for (char c : s) {
        if (!isClassBBaseChar(c)) {
            return false;
        }
    }
    return true;
}

bool isValidClassBAstOpSyntax(const std::string& s) {
    if (s.empty() || s.front() == ' ' || s.back() == ' ') {
        return false;
    }
    if (s == "@NONE" || s == "$" || s == "<" || s == ">" || s == "=" ||
        s == "!=" || s == "<=" || s == ">=" || s == "*" || s == "/" ||
        s == "(" || s == ")" || s == ",") {
        return true;
    }
    for (char c : s) {
        if (!isClassBBaseChar(c)) {
            return false;
        }
    }
    return true;
}


} // namespace

const std::vector<ASTNode>& AstReader::getAstNodes() const {
    return astNodes_;
}

const std::string& AstReader::getSourceFilename() const {
    return sourceFilename_;
}

bool AstReader::hasError() const {
    return hasError_;
}

const std::string& AstReader::getErrorCode() const {
    return errorCode_;
}

const std::string& AstReader::getErrorMessage() const {
    return errorMessage_;
}

int AstReader::getErrorLine() const {
    return errorLine_;
}

bool AstReader::fail(const std::string& code, int fileLine, const std::string& detail) {
    astNodes_.clear();
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

bool AstReader::read(const std::string& inputAstPath) {
    astNodes_.clear();
    sourceFilename_.clear();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;

    try {
        errno = 0;
        std::ifstream in(inputAstPath, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            return fail("", 0, formatOsError("open input file", inputAstPath, errno));
        }

        AstParseStage stage = AstParseStage::ExpectFormat;
        int expectedTotalNodes = 0;
        int fileLineNum = 0;
        std::vector<int> recordFileLines;

        std::string rawLine;
        while (std::getline(in, rawLine)) {
            ++fileLineNum;

            // Reject UTF-8 BOM (Section 5.1 Rule 1)
            if (fileLineNum == 1 && rawLine.size() >= 3 &&
                static_cast<unsigned char>(rawLine[0]) == 0xEF &&
                static_cast<unsigned char>(rawLine[1]) == 0xBB &&
                static_cast<unsigned char>(rawLine[2]) == 0xBF) {
                return fail("FATAL_IO_FORMAT_MISMATCH", 1,
                            "UTF-8 BOM (0xEF 0xBB 0xBF) is not allowed; .ast files must be UTF-8 without BOM");
            }

            // Strip single trailing CR for CRLF portability
            if (!rawLine.empty() && rawLine.back() == '\r') {
                rawLine.pop_back();
            }

            // Skip uncounted empty and '#' comment lines
            if (rawLine.empty() || rawLine[0] == '#') {
                continue;
            }

            const std::vector<std::string> fields = splitPipeFramedLine(rawLine);

            if (stage == AstParseStage::ExpectFormat) {
                if (fields.size() != 3 ||
                    fields[0] != "@FORMAT" ||
                    fields[1] != "ICC_AST" ||
                    fields[2] != "v1.0") {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@FORMAT | ICC_AST | v1.0'");
                }
                stage = AstParseStage::ExpectSource;
                continue;
            }

            if (stage == AstParseStage::ExpectSource) {
                if (fields.size() != 2 || fields[0] != "@SOURCE" || fields[1].empty()) {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@SOURCE | <escaped_source_or_@NONE>'");
                }
                if (fields[1] == "@NONE") {
                    sourceFilename_.clear();
                } else {
                    std::string decodedSource;
                    if (!unescapeClassA(fields[1], decodedSource)) {
                        return fail("FATAL_IO_MALFORMED_ESCAPE", fileLineNum,
                                    "Malformed percent-escape sequence in @SOURCE header");
                    }
                    sourceFilename_ = std::move(decodedSource);
                }
                stage = AstParseStage::ExpectTotalNodes;
                continue;
            }

            if (stage == AstParseStage::ExpectTotalNodes) {
                if (fields.size() != 2 || fields[0] != "@TOTAL_NODES") {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Expected header '@TOTAL_NODES | <N>'");
                }
                long long parsedTotal = 0;
                if (!parseStrictInt64(fields[1], parsedTotal)) {
                    return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                                "Malformed integer in @TOTAL_NODES header: '" + fields[1] + "'");
                }
                if (parsedTotal < 1 ||
                    parsedTotal > static_cast<long long>(std::numeric_limits<int>::max())) {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "@TOTAL_NODES must be at least 1 and fit within int range");
                }
                expectedTotalNodes = static_cast<int>(parsedTotal);
                astNodes_.reserve(static_cast<std::size_t>(expectedTotalNodes));
                recordFileLines.reserve(static_cast<std::size_t>(expectedTotalNodes));
                stage = AstParseStage::ReadRecords;
                continue;
            }

            if (stage == AstParseStage::AfterEndFile) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Unexpected non-comment data after @END_FILE");
            }

            // Stage == AstParseStage::ReadRecords
            if (fields.size() == 1 && fields[0] == "@END_FILE") {
                stage = AstParseStage::AfterEndFile;
                continue;
            }

            if (fields.size() != 8) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Expected 8-field AST record or '@END_FILE', found " +
                            std::to_string(fields.size()) + " field(s)");
            }

            if (astNodes_.size() >= static_cast<std::size_t>(expectedTotalNodes)) {
                return fail("FATAL_IO_COUNT_MISMATCH", fileLineNum,
                            "Found more AST records than declared in @TOTAL_NODES (" +
                            std::to_string(expectedTotalNodes) + ")");
            }

            int nodeId = 0;
            int parentId = 0;
            int childIndex = 0;
            long long litVal = 0;
            int lineNum = 0;

            if (!parseStrictInt(fields[0], nodeId)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed NODE_ID field: '" + fields[0] + "'");
            }
            if (!parseStrictInt(fields[1], parentId)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed PARENT_ID field: '" + fields[1] + "'");
            }
            if (!parseStrictInt(fields[2], childIndex)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed CHILD_INDEX field: '" + fields[2] + "'");
            }
            if (!parseStrictInt64(fields[6], litVal)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed LIT_VAL field: '" + fields[6] + "'");
            }
            if (!parseStrictInt(fields[7], lineNum)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Malformed LINE_NUM field: '" + fields[7] + "'");
            }

            const std::string& nodeKind = fields[3];
            const std::string& astOp = fields[4];

            if (!isValidClassBNodeKindSyntax(nodeKind)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Invalid Class B NODE_KIND syntax: '" + nodeKind + "'");
            }
            if (!isValidClassBAstOpSyntax(astOp)) {
                return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                            "Invalid Class B AST_OP syntax: '" + astOp + "'");
            }

            std::string decodedLexeme;
            if (!unescapeClassA(fields[5], decodedLexeme)) {
                return fail("FATAL_IO_MALFORMED_ESCAPE", fileLineNum,
                            "Malformed percent-escape sequence in LEXEME field");
            }
            if (decodedLexeme.empty()) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "AST LEXEME field cannot be empty");
            }

            // Invariant 2: Contiguous unique NODE_ID from 0 .. N - 1
            const int expectedId = static_cast<int>(astNodes_.size());
            if (nodeId != expectedId) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "Expected NODE_ID " + std::to_string(expectedId) +
                            ", found " + std::to_string(nodeId));
            }

            if (lineNum < 1) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                            "AST LINE_NUM must be >= 1");
            }

            // Invariants 3, 4 & 5: Unique root 0, parent-before-child, contiguous childIndex
            if (nodeId == 0) {
                if (parentId != -1 || childIndex != -1 || nodeKind != "PROG") {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "Root node 0 must have PARENT_ID=-1, CHILD_INDEX=-1, NODE_KIND='PROG'");
                }
            } else {
                if (parentId < 0 || parentId >= nodeId) {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "Non-root node " + std::to_string(nodeId) +
                                " violates parent-before-child invariant (PARENT_ID=" +
                                std::to_string(parentId) + ")");
                }
                ASTNode& parent = astNodes_[static_cast<std::size_t>(parentId)];
                if (childIndex != static_cast<int>(parent.childIds.size())) {
                    return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                                "Expected CHILD_INDEX " + std::to_string(parent.childIds.size()) +
                                " under parent " + std::to_string(parentId) +
                                ", found " + std::to_string(childIndex));
                }
                parent.childIds.push_back(nodeId);
            }

            ASTNode node;
            node.nodeId = nodeId;
            node.parentId = parentId;
            node.childIndex = childIndex;
            node.nodeKind = nodeKind;
            node.astOp = astOp;
            node.lexeme = std::move(decodedLexeme);
            node.litVal = litVal;
            node.line = lineNum;

            astNodes_.push_back(std::move(node));
            recordFileLines.push_back(fileLineNum);
        }

        if (in.bad()) {
            return fail("", fileLineNum, formatOsError("read input file", inputAstPath, errno));
        }

        if (stage == AstParseStage::ExpectFormat ||
            stage == AstParseStage::ExpectSource ||
            stage == AstParseStage::ExpectTotalNodes) {
            return fail("FATAL_IO_FORMAT_MISMATCH", fileLineNum,
                        "Incomplete or missing required .ast header block");
        }

        if (stage != AstParseStage::AfterEndFile) {
            return fail("FATAL_IO_MISSING_END_FILE", fileLineNum,
                        "Reached end of file without encountering '@END_FILE'");
        }

        // Invariant 1: Exact record count
        if (astNodes_.size() != static_cast<std::size_t>(expectedTotalNodes)) {
            return fail("FATAL_IO_COUNT_MISMATCH", fileLineNum,
                        "Expected " + std::to_string(expectedTotalNodes) +
                        " AST node(s), found " + std::to_string(astNodes_.size()));
        }

        // Invariant 5 (Part 2): Full Section 4.2-B schema validation for every node
        for (std::size_t i = 0; i < astNodes_.size(); ++i) {
            std::string schemaErr;
            if (!ast_schema::validateNode(astNodes_[i], astNodes_, astNodes_[i].childIds, schemaErr)) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", recordFileLines[i],
                            "Node " + std::to_string(i) + ": " + schemaErr);
            }
        }

        // Invariant 6: Complete DFS reachability from root node 0 (explicit verification)
        std::vector<bool> visited(astNodes_.size(), false);
        std::vector<int> dfsStack = {0};
        std::size_t visitedCount = 0;
        while (!dfsStack.empty()) {
            const int cur = dfsStack.back();
            dfsStack.pop_back();
            if (visited[static_cast<std::size_t>(cur)]) {
                return fail("FATAL_IO_INVARIANT_VIOLATION", recordFileLines[static_cast<std::size_t>(cur)],
                            "Cycle detected in AST during root reachability DFS");
            }
            visited[static_cast<std::size_t>(cur)] = true;
            ++visitedCount;
            const auto& kids = astNodes_[static_cast<std::size_t>(cur)].childIds;
            for (std::size_t i = kids.size(); i > 0; --i) {
                dfsStack.push_back(kids[i - 1]);
            }
        }
        if (visitedCount != astNodes_.size()) {
            return fail("FATAL_IO_INVARIANT_VIOLATION", fileLineNum,
                        "Not all AST nodes are reachable from root node 0");
        }

        return true;
    } catch (const std::exception& ex) {
        return fail("", 0, std::string("Unexpected runtime exception while reading '") +
                               inputAstPath + "': " + ex.what());
    } catch (...) {
        return fail("", 0, "Unknown runtime exception while reading '" + inputAstPath + "'");
    }
}