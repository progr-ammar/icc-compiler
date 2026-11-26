// Phase 3 Stage 3A: SymWriter test suite (Specification v1.4, Sections 4.1, 5.1 & 5.4).
// Run from the repository root (reads tests/golden/, writes scratch files under bin/).

#include <cstdio>
#include <string>
#include <vector>

#include "sym.h"
#include "sym_reader.h"
#include "sym_test_support.h"
#include "sym_writer.h"

using namespace symtest;

namespace {

// SW1: the Section 5.4 golden file is reproduced byte-for-byte
void testSW1_GoldenMinReturn(TestContext& ctx) {
    const std::string path = "bin/test_sw1_min_return.sym";
    std::remove(path.c_str());
    SymWriter w;
    ctx.checkTrue(w.write(path, "min_return.citu", minReturnTable(), 7),
                  "SW1: golden table accepted (" + w.getErrorMessage() + ")");
    ctx.checkTrue(!w.hasError(), "SW1: hasError false");

    std::string produced;
    std::string golden;
    ctx.checkTrue(loadFileBinary(path, produced), "SW1: output written");
    ctx.checkTrue(loadFileBinary("tests/golden/min_return.sym", golden),
                  "SW1: tests/golden/min_return.sym readable (run from repo root)");
    ctx.checkEqual(produced, golden, "SW1: min_return.sym byte-for-byte equal to Spec v1.4 Sec 5.4");
    std::remove(path.c_str());
}

// SW2: every section, both IS_EXTERNAL values, escaping and record layout
void testSW2_FullFormatLayout(TestContext& ctx) {
    const std::string path = "bin/test_sw2_rich.sym";
    SymWriter w;
    ctx.checkTrue(w.write(path, richSourceName(), richTable(), 10),
                  "SW2: rich table accepted (" + w.getErrorMessage() + ")");
    std::string out;
    ctx.checkTrue(loadFileBinary(path, out), "SW2: output written");
    std::remove(path.c_str());

    ctx.checkTrue(out.find('\r') == std::string::npos, "SW2: LF-only line endings");
    ctx.checkTrue(out.compare(0, 25, "@FORMAT | ICC_SYM | v1.0\n") == 0, "SW2: @FORMAT is the first line");
    ctx.checkTrue(out.size() >= 10 && out.compare(out.size() - 10, 10, "@END_FILE\n") == 0,
                  "SW2: file ends with @END_FILE");

    // Each expected line is the Section 5.4 schema applied to the fixture values
    const std::vector<std::string> expectedInOrder = {
        "@FORMAT | ICC_SYM | v1.0\n",
        "@SOURCE | multi%7Cunit%251.citu\n",
        "@COUNTS | STRUCTS=2 | FUNCS=3 | SCOPES=4 | SYMBOLS=3 | NODE_TYPES=10\n",
        "@STRUCT_DEF | 0 | Point | 2 | 1 | lib.citu | 1\n",
        "@STRUCT_FIELD | Point | 0 | x | int32\n",
        "@STRUCT_FIELD | Point | 1 | y | int16\n",
        "@STRUCT_DEF | 1 | Box | 3 | 0 | multi%7Cunit%251.citu | 3\n",
        "@STRUCT_FIELD | Box | 0 | width | int32\n",
        "@STRUCT_FIELD | Box | 1 | height | uint16\n",
        "@STRUCT_FIELD | Box | 2 | tag | char\n",
        "@FUNC_DEF | 0 | helper | uint32 | 1 | 1 | lib.citu | 5\n",
        "@FUNC_PARAM | helper | 0 | a | uint32\n",
        "@FUNC_DEF | 1 | noop | khali | 0 | 0 | multi%7Cunit%251.citu | 8\n",
        "@FUNC_DEF | 2 | main | int32 | 0 | 0 | multi%7Cunit%251.citu | 11\n",
        "@SCOPE_DEF | 0 | noop | @NONE | noop | 8 | 10\n",
        "@SCOPE_DEF | 1 | main | @NONE | main | 11 | 20\n",
        "@SCOPE_DEF | 2 | main.agar_1 | main | main | 14 | 18\n",
        "@SCOPE_DEF | 3 | main.agar_1.jabtak_1 | main.agar_1 | main | 15 | 17\n",
        "@SYMBOL | 0 | b | STRUCT_VAR | struct Box | main | main | 12\n",
        "@SYMBOL | 1 | n | LOCAL_VAR | int32 | main | main.agar_1 | 14\n",
        "@SYMBOL | 2 | p | STRUCT_VAR | struct Point | main | main | 13\n",
        "@NODE_TYPE | 0 | @NONE | @NONE | -1 | -1 | 0 | -1\n",
        "@NODE_TYPE | 1 | struct Box | STRUCT_TYPE | 1 | -1 | 0 | -1\n",
        "@NODE_TYPE | 2 | uint16 | STRUCT_FIELD_DECL | 1 | 1 | 0 | -1\n",
        "@NODE_TYPE | 3 | khali | FUNC_REF | 1 | -1 | 0 | -1\n",
        "@NODE_TYPE | 4 | struct Box | LOCAL_SYM | 0 | -1 | 0 | -1\n",
        "@NODE_TYPE | 5 | char | FIELD_ACCESS_REF | 0 | 2 | 0 | -1\n",
        "@NODE_TYPE | 6 | int32 | @NONE | -1 | -1 | 1 | -1\n",
        "@NODE_TYPE | 7 | BOOL_COND | @NONE | -1 | -1 | 0 | -1\n",
        "@NODE_TYPE | 8 | int32 | @NONE | -1 | -1 | 1 | -9223372036854775808\n",
        "@NODE_TYPE | 9 | int16 | FIELD_ACCESS_REF | 2 | 1 | 1 | 9223372036854775807\n",
        "@END_FILE\n"
    };
    std::size_t cursor = 0;
    for (const std::string& line : expectedInOrder) {
        const std::size_t pos = out.find(line, cursor);
        ctx.checkTrue(pos != std::string::npos, "SW2: expected line in order: " + line.substr(0, line.size() - 1));
        if (pos != std::string::npos) {
            cursor = pos + line.size();
        }
    }

    // Detail rows immediately follow their owner, and every non-comment line is a known record
    ctx.checkTrue(out.find("@STRUCT_DEF | 0 | Point | 2 | 1 | lib.citu | 1\n@STRUCT_FIELD | Point | 0 |") !=
                      std::string::npos, "SW2: @STRUCT_FIELD rows follow their @STRUCT_DEF");
    ctx.checkTrue(out.find("@FUNC_DEF | 0 | helper | uint32 | 1 | 1 | lib.citu | 5\n@FUNC_PARAM | helper |") !=
                      std::string::npos, "SW2: @FUNC_PARAM rows follow their @FUNC_DEF");
    std::size_t counted = 0;
    std::size_t start = 0;
    while (start < out.size()) {
        const std::size_t end = out.find('\n', start);
        const std::string line = out.substr(start, end - start);
        start = end + 1;
        ctx.checkTrue(line.find("  ") == std::string::npos, "SW2: no doubled spaces: " + line);
        if (line.rfind("@STRUCT_DEF", 0) == 0 || line.rfind("@FUNC_DEF", 0) == 0 ||
            line.rfind("@SCOPE_DEF", 0) == 0 || line.rfind("@SYMBOL", 0) == 0 ||
            line.rfind("@NODE_TYPE", 0) == 0) {
            ++counted;
        }
    }
    ctx.checkEqual(counted, static_cast<std::size_t>(2 + 3 + 4 + 3 + 10), "SW2: counted records == sum of @COUNTS");
}

// SW3: empty source filename and empty SOURCE_FILE serialize as @NONE (Section 5.1 Rule 4)
void testSW3_NoneSentinels(TestContext& ctx) {
    const std::string path = "bin/test_sw3_none.sym";
    SymbolTable t = minReturnTable();
    t.functions[0].sourceFile = "";
    SymWriter w;
    ctx.checkTrue(w.write(path, "", t), "SW3: empty source accepted (" + w.getErrorMessage() + ")");
    std::string out;
    ctx.checkTrue(loadFileBinary(path, out), "SW3: output written");
    ctx.checkTrue(out.find("@SOURCE | @NONE\n") != std::string::npos, "SW3: @SOURCE | @NONE");
    ctx.checkTrue(out.find("@FUNC_DEF | 0 | main | int32 | 0 | 0 | @NONE | 1\n") != std::string::npos,
                  "SW3: empty SOURCE_FILE written as @NONE");
    SymReader r;
    ctx.checkTrue(r.read(path), "SW3: reads back (" + r.getErrorMessage() + ")");
    ctx.checkEqual(r.getSourceFilename(), std::string(""), "SW3: source decodes to empty");
    std::remove(path.c_str());
}

void expectWriterReject(TestContext& ctx, const SymbolTable& t, const std::string& label,
                        const std::string& source = "min_return.citu", int expectedNodes = -1) {
    const std::string path = "bin/test_sw_reject.sym";
    std::remove(path.c_str());
    SymWriter w;
    ctx.checkTrue(!w.write(path, source, t, expectedNodes), label + ": SymWriter must reject");
    ctx.checkTrue(w.hasError(), label + ": hasError true");
    ctx.checkEqual(w.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), label + ": errorCode");
    ctx.checkTrue(!w.getErrorMessage().empty(), label + ": non-empty message");
    ctx.checkTrue(!fileExists(path), label + ": no output file created");
    std::remove(path.c_str());
}

// SW4: the writer refuses tables that would violate Section 5.4 (no file is produced)
void testSW4_WriterRejections(TestContext& ctx) {
    SymbolTable t;

    t = richTable(); t.structs[1].structId = 5;                 expectWriterReject(ctx, t, "SW4_struct_id_gap", richSourceName());
    t = richTable(); t.functions[2].funcId = 0;                 expectWriterReject(ctx, t, "SW4_func_id_dup", richSourceName());
    t = richTable(); t.scopes[3].scopeId = 7;                   expectWriterReject(ctx, t, "SW4_scope_id_gap", richSourceName());
    t = richTable(); t.symbols[0].symId = 1;                    expectWriterReject(ctx, t, "SW4_sym_id_gap", richSourceName());
    t = richTable(); t.nodeTypes[4].astNodeId = 5;              expectWriterReject(ctx, t, "SW4_node_id_gap", richSourceName());
    t = richTable(); t.structs[0].fields[1].fieldIndex = 0;     expectWriterReject(ctx, t, "SW4_field_index", richSourceName());
    t = richTable(); t.functions[0].params[0].paramIndex = 1;   expectWriterReject(ctx, t, "SW4_param_index", richSourceName());
    t = richTable(); t.structs[1].structName = "Point";         expectWriterReject(ctx, t, "SW4_dup_struct_name", richSourceName());
    t = richTable(); t.functions[1].funcName = "main";          expectWriterReject(ctx, t, "SW4_dup_func_name", richSourceName());
    t = richTable(); t.structs[0].structName = "Po|nt";         expectWriterReject(ctx, t, "SW4_pipe_in_class_b", richSourceName());
    t = richTable(); t.functions[2].funcName = " main";         expectWriterReject(ctx, t, "SW4_leading_space", richSourceName());
    t = richTable(); t.structs[1].fields[0].fieldType = "int64"; expectWriterReject(ctx, t, "SW4_bad_field_type", richSourceName());
    t = richTable(); t.functions[0].returnType = "void";        expectWriterReject(ctx, t, "SW4_bad_return_type", richSourceName());
    t = richTable(); t.symbols[1].symKind = "GLOBAL";           expectWriterReject(ctx, t, "SW4_bad_sym_kind", richSourceName());
    t = richTable(); t.symbols[1].dataType = "struct";          expectWriterReject(ctx, t, "SW4_bad_data_type", richSourceName());
    t = richTable(); t.nodeTypes[7].evaluatedType = "INT_LITERAL"; expectWriterReject(ctx, t, "SW4_bad_eval_type", richSourceName());
    t = richTable(); t.nodeTypes[3].refKind = "SYM_REF";        expectWriterReject(ctx, t, "SW4_bad_ref_kind", richSourceName());
    t = richTable(); t.structs[1].sourceFile = "other.citu";    expectWriterReject(ctx, t, "SW4_local_wrong_source", richSourceName());
    t = richTable(); t.functions[0].sourceFile = richSourceName(); expectWriterReject(ctx, t, "SW4_external_own_source", richSourceName());
    t = richTable(); t.scopes[2].parentScopePath = "main.nope"; expectWriterReject(ctx, t, "SW4_missing_parent_scope", richSourceName());
    t = richTable(); t.scopes[2].parentScopePath = "noop";      expectWriterReject(ctx, t, "SW4_parent_other_function", richSourceName());
    t = richTable(); t.scopes[3].scopePath = "main";            expectWriterReject(ctx, t, "SW4_dup_scope_path", richSourceName());
    t = richTable(); t.symbols[1].scopePath = "main.agar_9";    expectWriterReject(ctx, t, "SW4_symbol_unknown_scope", richSourceName());
    t = richTable(); t.symbols[1].enclosingFunc = "ghost";      expectWriterReject(ctx, t, "SW4_symbol_unknown_func", richSourceName());
    t = richTable(); t.symbols[2].identName = "b";              expectWriterReject(ctx, t, "SW4_dup_func_ident", richSourceName());
    t = richTable(); t.nodeTypes[4].resolvedId = 3;             expectWriterReject(ctx, t, "SW4_local_sym_out_of_range", richSourceName());
    t = richTable(); t.nodeTypes[3].resolvedId = 3;             expectWriterReject(ctx, t, "SW4_func_ref_out_of_range", richSourceName());
    t = richTable(); t.nodeTypes[1].resolvedId = 2;             expectWriterReject(ctx, t, "SW4_struct_type_out_of_range", richSourceName());
    t = richTable(); t.nodeTypes[0].resolvedId = 0;             expectWriterReject(ctx, t, "SW4_none_with_id", richSourceName());
    t = richTable(); t.nodeTypes[2].resolvedSubId = 3;          expectWriterReject(ctx, t, "SW4_field_decl_sub_range", richSourceName());
    t = richTable(); t.nodeTypes[5].resolvedSubId = 3;          expectWriterReject(ctx, t, "SW4_field_access_sub_range", richSourceName());
    t = richTable(); t.nodeTypes[5].resolvedId = 1;             expectWriterReject(ctx, t, "SW4_field_access_non_struct", richSourceName());
    t = richTable(); t.nodeTypes[4].resolvedSubId = 0;          expectWriterReject(ctx, t, "SW4_sub_id_not_applicable", richSourceName());
    t = richTable(); t.nodeTypes[7].foldedConstVal = 5;         expectWriterReject(ctx, t, "SW4_folded_without_const", richSourceName());
    t = richTable(); t.symbols[0].lineDeclared = 0;             expectWriterReject(ctx, t, "SW4_zero_line", richSourceName());
    t = richTable(); t.nodeTypes.clear();                       expectWriterReject(ctx, t, "SW4_no_node_types", richSourceName());
    t = richTable(); t.functions[1].sourceFile = "@NONE";       expectWriterReject(ctx, t, "SW4_literal_none_source_file", richSourceName());

    expectWriterReject(ctx, minReturnTable(), "SW4_source_literal_none", "@NONE");
    expectWriterReject(ctx, minReturnTable(), "SW4_node_count_vs_ast", "min_return.citu", 8);
}

// SW5: OS failure leaves an empty error code; the same instance then recovers
void testSW5_IoFailureAndReuse(TestContext& ctx) {
    SymWriter w;
    ctx.checkTrue(!w.write("bin/no_such_dir_sw5/x.sym", "min_return.citu", minReturnTable()),
                  "SW5: write into missing directory fails");
    ctx.checkTrue(w.hasError(), "SW5: hasError true");
    ctx.checkEqual(w.getErrorCode(), std::string(""), "SW5: OS errors carry no FATAL_IO code");
    ctx.checkTrue(w.getErrorMessage().find("OS I/O error") != std::string::npos, "SW5: OS error message");

    SymbolTable bad = minReturnTable();
    bad.nodeTypes[1].resolvedId = 9;
    ctx.checkTrue(!w.write("bin/test_sw5.sym", "min_return.citu", bad), "SW5: invalid table rejected");
    ctx.checkEqual(w.getErrorCode(), std::string("FATAL_IO_INVARIANT_VIOLATION"), "SW5: invariant code");

    const std::string path = "bin/test_sw5.sym";
    ctx.checkTrue(w.write(path, "min_return.citu", minReturnTable(), 7), "SW5: recovery write succeeds");
    ctx.checkTrue(!w.hasError(), "SW5: hasError reset");
    ctx.checkEqual(w.getErrorCode(), std::string(""), "SW5: errorCode reset");
    ctx.checkEqual(w.getErrorMessage(), std::string(""), "SW5: errorMessage reset");
    std::remove(path.c_str());
}

// SW6: overwriting an existing .sym replaces it completely (truncate, no stale tail)
void testSW6_TruncatesExistingFile(TestContext& ctx) {
    const std::string path = "bin/test_sw6.sym";
    SymWriter w;
    ctx.checkTrue(w.write(path, richSourceName(), richTable()), "SW6: write large file");
    ctx.checkTrue(w.write(path, "min_return.citu", minReturnTable()), "SW6: overwrite with small file");
    std::string produced;
    std::string golden;
    ctx.checkTrue(loadFileBinary(path, produced) && loadFileBinary("tests/golden/min_return.sym", golden),
                  "SW6: files readable");
    ctx.checkEqual(produced, golden, "SW6: overwritten file equals golden exactly");
    std::remove(path.c_str());
}

} // namespace

int main() {
    return runSuite("ICC Phase 3 (Stage 3A) SymWriter Test Suite", {
        {"SW1_Golden_Min_Return_Byte_Exact", testSW1_GoldenMinReturn},
        {"SW2_Full_Format_Layout_And_Escaping", testSW2_FullFormatLayout},
        {"SW3_None_Sentinels", testSW3_NoneSentinels},
        {"SW4_Writer_Rejects_Invalid_Tables", testSW4_WriterRejections},
        {"SW5_IO_Failure_And_Reuse", testSW5_IoFailureAndReuse},
        {"SW6_Truncates_Existing_File", testSW6_TruncatesExistingFile}
    });
}
