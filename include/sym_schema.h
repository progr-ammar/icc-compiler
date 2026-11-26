#ifndef ICC_SYM_SCHEMA_H
#define ICC_SYM_SCHEMA_H

#include <string>

#include "sym.h"

// Shared .sym consistency rules (Specification v1.4, Sections 4.1, 5.1 & 5.4).
// Used by SymWriter (before serializing) and SymReader (after deserializing) so
// both sides of the .sym contract enforce exactly the same invariants.
namespace sym_schema {

// Class B field syntax (Section 5.1 Rule 3): ^[A-Za-z0-9_.:+%\[\] -]+$ or "@NONE".
// Additionally rejects a leading or trailing space, which the " | " delimiter
// framing could not round-trip.
bool isClassBField(const std::string& s);

// Location of a validation failure: section 1..5 of the .sym file and the index
// of the counted record (@STRUCT_DEF, @FUNC_DEF, @SCOPE_DEF, @SYMBOL, @NODE_TYPE)
// within that section. section == 0 means a file-level failure.
struct Violation {
    int section = 0;
    int index = -1;
    std::string message;
};

// Validates the complete in-memory table against every intra-file .sym rule.
// sourceFilename is the decoded @SOURCE value ("" when @SOURCE is @NONE).
// expectedTotalNodes >= 0 additionally enforces Section 5.4 Rule 5
// (NODE_TYPES == @TOTAL_NODES of the corresponding .ast); pass -1 to skip.
bool validateTable(const SymbolTable& table,
                   const std::string& sourceFilename,
                   int expectedTotalNodes,
                   Violation& out);

} // namespace sym_schema

#endif // ICC_SYM_SCHEMA_H
