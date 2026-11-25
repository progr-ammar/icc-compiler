#ifndef ICC_LEX_READER_H
#define ICC_LEX_READER_H

#include <string>
#include <vector>

#include "token.h"

// Phase 2 Lexical Deserializer (Specification v1.4, Sections 5.1 & 5.2)
// Responsibility: Reads an ICC_LEX v1.0 file from disk, validates all format,
// escaping, header/footer, and structural invariants, and reconstructs an
// independent std::vector<Token> and source filename in memory.
class LexReader {
public:
    LexReader() = default;

    // Reads and validates inputLexPath in ICC_LEX v1.0 format.
    // Clears previous results and diagnostics at entry.
    // Returns true only if the entire file passes all validation rules.
    // On failure, clears partially reconstructed state, records diagnostics, and returns false.
    bool read(const std::string& inputLexPath);

    // Read-only accessors for reconstructed data and diagnostic state
    const std::vector<Token>& getTokens() const;
    const std::string& getSourceFilename() const;
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;
    int getErrorLine() const;

private:
    std::vector<Token> tokens_;
    std::string sourceFilename_;

    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;
    int errorLine_ = 0;

    bool fail(const std::string& code, int fileLine, const std::string& detail);
};

#endif // ICC_LEX_READER_H