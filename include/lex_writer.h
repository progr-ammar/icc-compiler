#ifndef ICC_LEX_WRITER_H
#define ICC_LEX_WRITER_H

#include <string>
#include <vector>

#include "token.h"

// Phase 2 Lexical Serializer (Specification v1.4, Sections 5.1 & 5.2)
// Responsibility: Validates input preconditions and serializes a std::vector<Token>
// into an ICC_LEX v1.0 file on disk using binary LF output and Class A percent-escaping.
class LexWriter {
public:
    LexWriter() = default;

    // Serializes tokens to outputLexPath in ICC_LEX v1.0 format.
    // Returns true on success.
    // Returns false and records diagnostic state on precondition or OS I/O failure.
    bool write(const std::string& outputLexPath,
               const std::string& sourceFilename,
               const std::vector<Token>& tokens);

    // Read-only diagnostic accessors
    bool hasError() const;
    const std::string& getErrorCode() const;
    const std::string& getErrorMessage() const;

private:
    bool hasError_ = false;
    std::string errorCode_;
    std::string errorMessage_;

    bool validateInput(const std::string& sourceFilename,
                       const std::vector<Token>& tokens);
    bool recordError(const std::string& code, const std::string& message);
};

#endif // ICC_LEX_WRITER_H