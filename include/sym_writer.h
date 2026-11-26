#ifndef ICC_SYM_WRITER_H
#define ICC_SYM_WRITER_H

#include <string>

#include "sym.h"

// Phase 3 Symbol-Table Serializer (Specification v1.4, Sections 4.1, 5.1 & 5.4)
// Responsibility: Validates a SymbolTable against the shared .sym invariants and
// serializes it to an ICC_SYM v1.0 file (five sections) in binary LF mode.
class SymWriter {
public:
    SymWriter() = default;

    // Serializes table to outputSymPath in ICC_SYM v1.0 format.
    // expectedTotalNodes >= 0 also enforces NODE_TYPES == @TOTAL_NODES of the .ast
    // (Section 5.4 Rule 5); pass -1 to skip that cross-file check.
    // Returns true on success. On failure records diagnostics, writes no file
    // for validation failures, and returns false.
    bool write(const std::string& outputSymPath,
               const std::string& sourceFilename,
               const SymbolTable& table,
               int expectedTotalNodes = -1);

    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;

private:
    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;

    bool recordError(const std::string& code, const std::string& message);
};

#endif // ICC_SYM_WRITER_H
