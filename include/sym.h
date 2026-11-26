#ifndef ICC_SYM_H
#define ICC_SYM_H

#include <string>
#include <vector>

// Section 4.1 (Specification v1.4), structures 3, 4 and 5:
// Symbol-table and AST type-annotation data structures.
// Created in RAM by the Phase 3 Semantic Analyzer -> serialized to .sym by SymWriter
// -> deserialized from .sym into independent RAM by SymReader (Phase 3 imports, Phase 4, Phase 8).
//
// The Phase 4 fields (byteSize, slotOffset, totalSlotBytes, stackFrameBytes, ebpOffset)
// are part of the specified structures but are NOT stored in .sym (Section 4.1 audit note);
// SymWriter never writes them and SymReader leaves them at their -1 defaults.

// 3. Struct & Field Metadata (Phase 3 .sym logical layout; Phase 4 stack slot layout)
struct FieldInfo {
    int fieldIndex = 0;          // 0 .. FieldCount-1
    std::string fieldName;       // e.g., "width"
    std::string fieldType;       // "int16", "int32", "uint16", "uint32", "char"
    int byteSize = -1;           // Phase 4: True architectural width (1, 2, or 4 bytes)
    int slotOffset = -1;         // Phase 4: 4-byte aligned field offset from struct base (0, 4, 8...)
};

struct StructInfo {
    int structId = 0;                // 0 .. StructCount-1 (within compilation unit's .sym)
    std::string structName;          // Globally unique struct name (e.g., "Box")
    bool isExternal = false;         // True if imported from an earlier compilation unit's .sym
    std::string sourceFile;          // Defining .citu filename
    int lineDeclared = 0;
    std::vector<FieldInfo> fields;
    int totalSlotBytes = -1;         // Phase 4: 4 * fields.size() (ICC 4-byte slot convention)
};

// 4. Function & Parameter Metadata (Phase 3 .sym signature; Phase 4 frame size)
struct ParamInfo {
    int paramIndex = 0;          // 0 .. ParamCount-1
    std::string paramName;       // e.g., "w"
    std::string paramType;       // "int16", "int32", "uint16", "uint32", "char"
};

struct FunctionInfo {
    int funcId = 0;                  // 0 .. FuncCount-1 (within compilation unit's .sym)
    std::string funcName;            // Globally unique function name ("calcArea", "main")
    std::string returnType;          // "int16", "int32", "uint16", "uint32", "char", "khali"
    std::vector<ParamInfo> params;
    bool isExternal = false;         // True if imported from an earlier compilation unit's .sym
    std::string sourceFile;          // Defining .citu filename
    int lineDeclared = 0;
    int stackFrameBytes = -1;        // Phase 4: Total bytes reserved via 'sub esp, N' (locals + temporaries)
};

// 5. Scope Hierarchy, Symbol Entry & AST Node Type Annotation (Phase 3 -> .sym)
struct ScopeInfo {
    int scopeId = 0;                 // 0 .. ScopeCount-1
    std::string scopePath;           // e.g., "main", "main.agar_1", "main.agar_1.jabtak_1"
    std::string parentScopePath;     // Enclosing scope path or "@NONE" for function root scope
    std::string enclosingFunc;       // Owning function name ("main")
    int lineStart = 0;               // Line number of 'shro'
    int lineEnd = 0;                 // Line number of 'khtm'
};

struct SymbolTableEntry {
    int symId = 0;                   // 0 .. SymbolCount-1
    std::string identName;           // Variable or parameter name ("area", "myBox")
    std::string symKind;             // "PARAM", "LOCAL_VAR", "STRUCT_VAR"
    std::string dataType;            // "int16", "int32", "uint16", "uint32", "char", "struct Box"
    std::string enclosingFunc;       // Function owning this symbol
    std::string scopePath;           // Exact ScopeInfo.scopePath where declared
    int lineDeclared = 0;
    int byteSize = -1;               // Phase 4: 1, 2, 4, or totalSlotBytes for struct
    int ebpOffset = -1;              // Phase 4: +8, +12... for PARAM; address of first slot (negative) for LOCAL_VAR/STRUCT_VAR
};

struct NodeTypeAnnotation {
    int astNodeId = 0;               // Matches ASTNode.nodeId in .ast (0 .. TotalNodes-1)
    std::string evaluatedType;       // Concrete resolved type ("int32", "BOOL_COND", "khali", "struct Box", "@NONE")
    std::string refKind;             // "LOCAL_SYM", "FUNC_REF", "STRUCT_FIELD_DECL", "FIELD_ACCESS_REF", "STRUCT_TYPE", or "@NONE"
    int resolvedId = -1;             // symId (LOCAL_SYM / FIELD_ACCESS_REF), funcId (FUNC_REF), structId (STRUCT_TYPE / STRUCT_FIELD_DECL), or -1
    int resolvedSubId = -1;          // fieldIndex (0..F-1) for STRUCT_FIELD_DECL and FIELD_ACCESS_REF; -1 otherwise
    bool isConstExpr = false;        // True if compile-time constant expression
    long long foldedConstVal = -1;   // Folded 64-bit constant value; meaningful only when isConstExpr == true (else -1)
};

struct SymbolTable {
    std::vector<StructInfo> structs;
    std::vector<FunctionInfo> functions;
    std::vector<ScopeInfo> scopes;
    std::vector<SymbolTableEntry> symbols;
    std::vector<NodeTypeAnnotation> nodeTypes;
};

#endif // ICC_SYM_H
