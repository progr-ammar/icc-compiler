#ifndef ICC_SYM_READER_H
#define ICC_SYM_READER_H

#include <string>

#include "sym.h"

// Phase 3 Symbol-Table Deserializer (Specification v1.4, Sections 4.1, 5.1 & 5.4)
// Responsibility: Reads an ICC_SYM v1.0 file from disk, enforces the header/footer
// contract, @COUNTS accounting, field syntax and escaping, and every SymReader
// validation rule of Section 5.4, and reconstructs an independent SymbolTable.
class SymReader {
public:
    SymReader() = default;

    // Reads and validates inputSymPath. Clears previous state at entry.
    // expectedTotalNodes >= 0 also enforces Section 5.4 Rule 5
    // (NODE_TYPES == @TOTAL_NODES of the corresponding .ast); pass -1 when the
    // .ast is not available (e.g. Sections 1-2 imports of an earlier unit).
    // Returns true iff every check passes; on failure the table is cleared.
    bool read(const std::string& inputSymPath, int expectedTotalNodes = -1);

    const SymbolTable& getSymbolTable() const;
    const std::string& getSourceFilename() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;

private:
    SymbolTable table_;
    std::string sourceFilename_;

    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;

    bool fail(const std::string& code, int fileLine, const std::string& detail);
};

#endif // ICC_SYM_READER_H
