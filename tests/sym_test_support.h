#ifndef ICC_SYM_TEST_SUPPORT_H
#define ICC_SYM_TEST_SUPPORT_H

// Shared helpers and SymbolTable fixtures for test_sym_writer.cpp and test_sym_reader.cpp.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "sym.h"

namespace symtest {

struct TestContext {
    bool passed = true;
    std::vector<std::string> messages;

    void fail(const std::string& msg) {
        passed = false;
        messages.push_back(msg);
    }

    void checkTrue(bool condition, const std::string& desc) {
        if (!condition) {
            fail("Expected true: " + desc);
        }
    }

    template <typename T, typename U>
    void checkEqual(const T& actual, const U& expected, const std::string& desc) {
        if (!(actual == expected)) {
            std::ostringstream oss;
            oss << desc << "\n         expected: [" << expected << "]"
                << "\n         actual  : [" << actual << "]";
            fail(oss.str());
        }
    }
};

inline bool loadFileBinary(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

inline bool writeRawFileBinary(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return out.good();
}

inline bool fileExists(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return in.is_open();
}

// The exact in-memory table behind the Section 5.4 golden file "min_return.sym".
inline SymbolTable minReturnTable() {
    SymbolTable t;

    FunctionInfo mainFn;
    mainFn.funcId = 0;
    mainFn.funcName = "main";
    mainFn.returnType = "int32";
    mainFn.isExternal = false;
    mainFn.sourceFile = "min_return.citu";
    mainFn.lineDeclared = 1;
    t.functions.push_back(mainFn);

    ScopeInfo root;
    root.scopeId = 0;
    root.scopePath = "main";
    root.parentScopePath = "@NONE";
    root.enclosingFunc = "main";
    root.lineStart = 1;
    root.lineEnd = 3;
    t.scopes.push_back(root);

    // @NODE_TYPE rows 0..6 exactly as printed in Section 5.4
    struct Row { const char* type; const char* ref; int rid; int sub; bool isConst; long long folded; };
    const Row rows[7] = {
        {"@NONE", "@NONE", -1, -1, false, -1},
        {"int32", "FUNC_REF", 0, -1, false, -1},
        {"@NONE", "@NONE", -1, -1, false, -1},
        {"@NONE", "@NONE", -1, -1, false, -1},
        {"@NONE", "@NONE", -1, -1, false, -1},
        {"int32", "@NONE", -1, -1, false, -1},
        {"int32", "@NONE", -1, -1, true, 0}
    };
    for (int i = 0; i < 7; ++i) {
        NodeTypeAnnotation nt;
        nt.astNodeId = i;
        nt.evaluatedType = rows[i].type;
        nt.refKind = rows[i].ref;
        nt.resolvedId = rows[i].rid;
        nt.resolvedSubId = rows[i].sub;
        nt.isConstExpr = rows[i].isConst;
        nt.foldedConstVal = rows[i].folded;
        t.nodeTypes.push_back(nt);
    }
    return t;
}

// A structurally valid table exercising every section, both IS_EXTERNAL values,
// nested scopes, struct-typed symbols, every REF_KIND, escaped SOURCE_FILE text,
// a legitimate folded value of -1 and 64-bit extremes. It is a format fixture,
// not the output of semantic analysis.
inline const char* richSourceName() {
    return "multi|unit%1.citu";
}

inline SymbolTable richTable() {
    SymbolTable t;
    const std::string src = richSourceName();

    auto addStruct = [&](const char* name, bool ext, const char* file, int line,
                         std::vector<std::pair<const char*, const char*>> fields) {
        StructInfo s;
        s.structId = static_cast<int>(t.structs.size());
        s.structName = name;
        s.isExternal = ext;
        s.sourceFile = file;
        s.lineDeclared = line;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            FieldInfo f;
            f.fieldIndex = static_cast<int>(i);
            f.fieldName = fields[i].first;
            f.fieldType = fields[i].second;
            s.fields.push_back(f);
        }
        t.structs.push_back(s);
    };
    addStruct("Point", true, "lib.citu", 1, {{"x", "int32"}, {"y", "int16"}});
    addStruct("Box", false, src.c_str(), 3, {{"width", "int32"}, {"height", "uint16"}, {"tag", "char"}});

    auto addFunc = [&](const char* name, const char* ret, bool ext, const char* file, int line,
                       std::vector<std::pair<const char*, const char*>> params) {
        FunctionInfo fn;
        fn.funcId = static_cast<int>(t.functions.size());
        fn.funcName = name;
        fn.returnType = ret;
        fn.isExternal = ext;
        fn.sourceFile = file;
        fn.lineDeclared = line;
        for (std::size_t i = 0; i < params.size(); ++i) {
            ParamInfo p;
            p.paramIndex = static_cast<int>(i);
            p.paramName = params[i].first;
            p.paramType = params[i].second;
            fn.params.push_back(p);
        }
        t.functions.push_back(fn);
    };
    addFunc("helper", "uint32", true, "lib.citu", 5, {{"a", "uint32"}});
    addFunc("noop", "khali", false, src.c_str(), 8, {});
    addFunc("main", "int32", false, src.c_str(), 11, {});

    auto addScope = [&](const char* path, const char* parent, const char* fn, int ls, int le) {
        ScopeInfo sc;
        sc.scopeId = static_cast<int>(t.scopes.size());
        sc.scopePath = path;
        sc.parentScopePath = parent;
        sc.enclosingFunc = fn;
        sc.lineStart = ls;
        sc.lineEnd = le;
        t.scopes.push_back(sc);
    };
    addScope("noop", "@NONE", "noop", 8, 10);
    addScope("main", "@NONE", "main", 11, 20);
    addScope("main.agar_1", "main", "main", 14, 18);
    addScope("main.agar_1.jabtak_1", "main.agar_1", "main", 15, 17);

    auto addSym = [&](const char* name, const char* kind, const char* type, const char* fn,
                      const char* scope, int line) {
        SymbolTableEntry sy;
        sy.symId = static_cast<int>(t.symbols.size());
        sy.identName = name;
        sy.symKind = kind;
        sy.dataType = type;
        sy.enclosingFunc = fn;
        sy.scopePath = scope;
        sy.lineDeclared = line;
        t.symbols.push_back(sy);
    };
    addSym("b", "STRUCT_VAR", "struct Box", "main", "main", 12);
    addSym("n", "LOCAL_VAR", "int32", "main", "main.agar_1", 14);
    addSym("p", "STRUCT_VAR", "struct Point", "main", "main", 13);

    auto addNode = [&](const char* type, const char* ref, int rid, int sub, bool isConst, long long folded) {
        NodeTypeAnnotation nt;
        nt.astNodeId = static_cast<int>(t.nodeTypes.size());
        nt.evaluatedType = type;
        nt.refKind = ref;
        nt.resolvedId = rid;
        nt.resolvedSubId = sub;
        nt.isConstExpr = isConst;
        nt.foldedConstVal = folded;
        t.nodeTypes.push_back(nt);
    };
    addNode("@NONE", "@NONE", -1, -1, false, -1);                         // 0
    addNode("struct Box", "STRUCT_TYPE", 1, -1, false, -1);               // 1
    addNode("uint16", "STRUCT_FIELD_DECL", 1, 1, false, -1);              // 2
    addNode("khali", "FUNC_REF", 1, -1, false, -1);                       // 3
    addNode("struct Box", "LOCAL_SYM", 0, -1, false, -1);                 // 4
    addNode("char", "FIELD_ACCESS_REF", 0, 2, false, -1);                 // 5
    addNode("int32", "@NONE", -1, -1, true, -1);                          // 6: legitimate folded -1
    addNode("BOOL_COND", "@NONE", -1, -1, false, -1);                     // 7
    addNode("int32", "@NONE", -1, -1, true, -9223372036854775807LL - 1);  // 8: int64 minimum
    addNode("int16", "FIELD_ACCESS_REF", 2, 1, true, 9223372036854775807LL); // 9: int64 maximum
    return t;
}

// Deep comparison of every serialized field (Phase 4 fields must stay at -1).
inline void checkTablesEqual(TestContext& ctx, const SymbolTable& a, const SymbolTable& b,
                             const std::string& label) {
    ctx.checkEqual(a.structs.size(), b.structs.size(), label + ": struct count");
    for (std::size_t i = 0; i < a.structs.size() && i < b.structs.size(); ++i) {
        const StructInfo& x = a.structs[i];
        const StructInfo& y = b.structs[i];
        const std::string p = label + " struct[" + std::to_string(i) + "]";
        ctx.checkEqual(x.structId, y.structId, p + ".structId");
        ctx.checkEqual(x.structName, y.structName, p + ".structName");
        ctx.checkEqual(x.isExternal, y.isExternal, p + ".isExternal");
        ctx.checkEqual(x.sourceFile, y.sourceFile, p + ".sourceFile");
        ctx.checkEqual(x.lineDeclared, y.lineDeclared, p + ".lineDeclared");
        ctx.checkEqual(x.totalSlotBytes, -1, p + ".totalSlotBytes stays -1");
        ctx.checkEqual(x.fields.size(), y.fields.size(), p + ".fields.size");
        for (std::size_t k = 0; k < x.fields.size() && k < y.fields.size(); ++k) {
            ctx.checkEqual(x.fields[k].fieldIndex, y.fields[k].fieldIndex, p + ".fieldIndex");
            ctx.checkEqual(x.fields[k].fieldName, y.fields[k].fieldName, p + ".fieldName");
            ctx.checkEqual(x.fields[k].fieldType, y.fields[k].fieldType, p + ".fieldType");
            ctx.checkEqual(x.fields[k].byteSize, -1, p + ".byteSize stays -1");
            ctx.checkEqual(x.fields[k].slotOffset, -1, p + ".slotOffset stays -1");
        }
    }
    ctx.checkEqual(a.functions.size(), b.functions.size(), label + ": function count");
    for (std::size_t i = 0; i < a.functions.size() && i < b.functions.size(); ++i) {
        const FunctionInfo& x = a.functions[i];
        const FunctionInfo& y = b.functions[i];
        const std::string p = label + " func[" + std::to_string(i) + "]";
        ctx.checkEqual(x.funcId, y.funcId, p + ".funcId");
        ctx.checkEqual(x.funcName, y.funcName, p + ".funcName");
        ctx.checkEqual(x.returnType, y.returnType, p + ".returnType");
        ctx.checkEqual(x.isExternal, y.isExternal, p + ".isExternal");
        ctx.checkEqual(x.sourceFile, y.sourceFile, p + ".sourceFile");
        ctx.checkEqual(x.lineDeclared, y.lineDeclared, p + ".lineDeclared");
        ctx.checkEqual(x.stackFrameBytes, -1, p + ".stackFrameBytes stays -1");
        ctx.checkEqual(x.params.size(), y.params.size(), p + ".params.size");
        for (std::size_t k = 0; k < x.params.size() && k < y.params.size(); ++k) {
            ctx.checkEqual(x.params[k].paramIndex, y.params[k].paramIndex, p + ".paramIndex");
            ctx.checkEqual(x.params[k].paramName, y.params[k].paramName, p + ".paramName");
            ctx.checkEqual(x.params[k].paramType, y.params[k].paramType, p + ".paramType");
        }
    }
    ctx.checkEqual(a.scopes.size(), b.scopes.size(), label + ": scope count");
    for (std::size_t i = 0; i < a.scopes.size() && i < b.scopes.size(); ++i) {
        const ScopeInfo& x = a.scopes[i];
        const ScopeInfo& y = b.scopes[i];
        const std::string p = label + " scope[" + std::to_string(i) + "]";
        ctx.checkEqual(x.scopeId, y.scopeId, p + ".scopeId");
        ctx.checkEqual(x.scopePath, y.scopePath, p + ".scopePath");
        ctx.checkEqual(x.parentScopePath, y.parentScopePath, p + ".parentScopePath");
        ctx.checkEqual(x.enclosingFunc, y.enclosingFunc, p + ".enclosingFunc");
        ctx.checkEqual(x.lineStart, y.lineStart, p + ".lineStart");
        ctx.checkEqual(x.lineEnd, y.lineEnd, p + ".lineEnd");
    }
    ctx.checkEqual(a.symbols.size(), b.symbols.size(), label + ": symbol count");
    for (std::size_t i = 0; i < a.symbols.size() && i < b.symbols.size(); ++i) {
        const SymbolTableEntry& x = a.symbols[i];
        const SymbolTableEntry& y = b.symbols[i];
        const std::string p = label + " sym[" + std::to_string(i) + "]";
        ctx.checkEqual(x.symId, y.symId, p + ".symId");
        ctx.checkEqual(x.identName, y.identName, p + ".identName");
        ctx.checkEqual(x.symKind, y.symKind, p + ".symKind");
        ctx.checkEqual(x.dataType, y.dataType, p + ".dataType");
        ctx.checkEqual(x.enclosingFunc, y.enclosingFunc, p + ".enclosingFunc");
        ctx.checkEqual(x.scopePath, y.scopePath, p + ".scopePath");
        ctx.checkEqual(x.lineDeclared, y.lineDeclared, p + ".lineDeclared");
        ctx.checkEqual(x.byteSize, -1, p + ".byteSize stays -1");
        ctx.checkEqual(x.ebpOffset, -1, p + ".ebpOffset stays -1");
    }
    ctx.checkEqual(a.nodeTypes.size(), b.nodeTypes.size(), label + ": node type count");
    for (std::size_t i = 0; i < a.nodeTypes.size() && i < b.nodeTypes.size(); ++i) {
        const NodeTypeAnnotation& x = a.nodeTypes[i];
        const NodeTypeAnnotation& y = b.nodeTypes[i];
        const std::string p = label + " node[" + std::to_string(i) + "]";
        ctx.checkEqual(x.astNodeId, y.astNodeId, p + ".astNodeId");
        ctx.checkEqual(x.evaluatedType, y.evaluatedType, p + ".evaluatedType");
        ctx.checkEqual(x.refKind, y.refKind, p + ".refKind");
        ctx.checkEqual(x.resolvedId, y.resolvedId, p + ".resolvedId");
        ctx.checkEqual(x.resolvedSubId, y.resolvedSubId, p + ".resolvedSubId");
        ctx.checkEqual(x.isConstExpr, y.isConstExpr, p + ".isConstExpr");
        ctx.checkEqual(x.foldedConstVal, y.foldedConstVal, p + ".foldedConstVal");
    }
}

// Replaces the first occurrence of `from`; records a failure if it is absent so
// a mutation test can never silently test an unmodified fixture.
inline std::string mutate(TestContext& ctx, const std::string& text, const std::string& from,
                          const std::string& to, const std::string& label) {
    const std::size_t pos = text.find(from);
    if (pos == std::string::npos) {
        ctx.fail(label + ": fixture text to mutate not found: " + from);
        return text;
    }
    std::string out = text;
    out.replace(pos, from.size(), to);
    return out;
}

struct TestCase {
    std::string name;
    void (*fn)(TestContext&);
};

inline int runSuite(const std::string& title, const std::vector<TestCase>& suite) {
    int passed = 0;
    int failed = 0;
    std::cout << "=== " << title << " ===\n\n";
    for (const TestCase& tc : suite) {
        TestContext ctx;
        tc.fn(ctx);
        if (ctx.passed) {
            ++passed;
            std::cout << "[PASS] " << tc.name << std::endl;
        } else {
            ++failed;
            std::cout << "[FAIL] " << tc.name << "\n";
            for (const std::string& m : ctx.messages) {
                std::cout << "       -> " << m << "\n";
            }
            std::cout.flush();
        }
    }
    std::cout << "\n=== Summary: " << passed << " passed, " << failed << " failed, "
              << suite.size() << " total ===\n";
    return failed == 0 ? 0 : 1;
}

} // namespace symtest

#endif // ICC_SYM_TEST_SUPPORT_H
