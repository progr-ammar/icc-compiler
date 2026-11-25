#include "ast_writer.h"

#include "ast_schema.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

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


} // namespace

bool AstWriter::hasError() const {
    return hasError_;
}

const std::string& AstWriter::getErrorCode() const {
    return errorCode_;
}

const std::string& AstWriter::getErrorMessage() const {
    return errorMessage_;
}

bool AstWriter::recordError(const std::string& code, const std::string& message) {
    hasError_ = true;
    errorCode_ = code;
    errorMessage_ = message;
    return false;
}

bool AstWriter::validateInput(const std::string& sourceFilename,
                              const std::vector<ASTNode>& nodes) {
    if (sourceFilename == "@NONE") {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "Source filename literal '@NONE' collides with the v1.4 missing-value sentinel");
    }

    if (nodes.empty()) {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "Cannot serialize an empty AST; root PROG node is required");
    }

    if (nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return recordError("FATAL_IO_INVARIANT_VIOLATION",
                           "AST node count exceeds maximum representable int ID");
    }

    std::vector<std::vector<int>> rebuiltChildren(nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const ASTNode& n = nodes[i];
        const std::string lbl = "ASTNode[" + std::to_string(i) + "]";

        if (n.nodeId != static_cast<int>(i)) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               lbl + " has non-contiguous nodeId " + std::to_string(n.nodeId));
        }

        if (n.line < 1) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               lbl + " has invalid line number " + std::to_string(n.line));
        }

        if (i == 0) {
            if (n.parentId != -1 || n.childIndex != -1 || n.nodeKind != "PROG") {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   "Root node 0 must have parentId=-1, childIndex=-1, nodeKind='PROG'");
            }
        } else {
            if (n.parentId < 0 || n.parentId >= static_cast<int>(i)) {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   lbl + " violates parent-before-child invariant (parentId=" +
                                   std::to_string(n.parentId) + ")");
            }
            auto& parentKids = rebuiltChildren[static_cast<std::size_t>(n.parentId)];
            if (n.childIndex != static_cast<int>(parentKids.size())) {
                return recordError("FATAL_IO_INVARIANT_VIOLATION",
                                   lbl + " has non-contiguous childIndex " + std::to_string(n.childIndex));
            }
            parentKids.push_back(static_cast<int>(i));
        }
    }

    // In-memory childIds, when supplied, must agree with the parentId/childIndex links
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].childIds.empty() && nodes[i].childIds != rebuiltChildren[i]) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               "ASTNode[" + std::to_string(i) +
                               "] childIds disagree with the children's parentId/childIndex links");
        }
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        std::string schemaErr;
        if (!ast_schema::validateNode(nodes[i], nodes, rebuiltChildren[i], schemaErr)) {
            return recordError("FATAL_IO_INVARIANT_VIOLATION",
                               "ASTNode[" + std::to_string(i) + "]: " + schemaErr);
        }
    }

    return true;
}

bool AstWriter::write(const std::string& outputAstPath,
                      const std::string& sourceFilename,
                      const std::vector<ASTNode>& nodes) {
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();

    try {
        if (!validateInput(sourceFilename, nodes)) {
            return false;
        }

        errno = 0;
        std::ofstream out(outputAstPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return recordError("", formatOsError("open output file", outputAstPath, errno));
        }

        const std::string serializedSource =
            sourceFilename.empty() ? "@NONE" : escapeClassA(sourceFilename);

        // Header block (Specification v1.4, Sections 5.1 & 5.3)
        out << "@FORMAT | ICC_AST | v1.0\n";
        out << "@SOURCE | " << serializedSource << '\n';
        out << "@TOTAL_NODES | " << nodes.size() << '\n';
        out << "# NODE_ID | PARENT_ID | CHILD_INDEX | NODE_KIND | AST_OP | LEXEME | LIT_VAL | LINE_NUM\n";

        // Counted 8-field pre-order AST records joined by exact " | " delimiter framing
        for (const ASTNode& node : nodes) {
            const std::string serializedLexeme =
                node.lexeme.empty() ? "@NONE" : escapeClassA(node.lexeme);
            out << node.nodeId << " | "
                << node.parentId << " | "
                << node.childIndex << " | "
                << node.nodeKind << " | "
                << node.astOp << " | "
                << serializedLexeme << " | "
                << node.litVal << " | "
                << node.line << '\n';
        }

        out << "@END_FILE\n";

        errno = 0;
        out.flush();
        if (!out.good()) {
            const int flushErr = errno;
            out.close();
            return recordError("", formatOsError("write/flush output file", outputAstPath, flushErr));
        }

        errno = 0;
        out.close();
        if (out.fail()) {
            return recordError("", formatOsError("close output file", outputAstPath, errno));
        }

        return true;
    } catch (const std::exception& ex) {
        return recordError("", std::string("Unexpected runtime exception while writing '") +
                                   outputAstPath + "': " + ex.what());
    } catch (...) {
        return recordError("", "Unknown runtime exception while writing '" + outputAstPath + "'");
    }
}