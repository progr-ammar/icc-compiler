// Phase 3 Stage 3A: SymReader test suite (Specification v1.4, Sections 4.1, 5.1 & 5.4).
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

const std::string kFmt = "FATAL_IO_FORMAT_MISMATCH";
const std::string kCnt = "FATAL_IO_COUNT_MISMATCH";
const std::string kInv = "FATAL_IO_INVARIANT_VIOLATION";
const std::string kEsc = "FATAL_IO_MALFORMED_ESCAPE";
const std::string kEnd = "FATAL_IO_MISSING_END_FILE";

std::string goldenText(TestContext& ctx) {
    std::string g;
    ctx.checkTrue(loadFileBinary("tests/golden/min_return.sym", g),
                  "tests/golden/min_return.sym readable (run from repo root)");
    return g;
}

// Serialized rich fixture, produced by SymWriter (its layout is checked in test_sym_writer SW2).
std::string richText(TestContext& ctx) {
    const std::string path = "bin/test_sr_rich_src.sym";
    SymWriter w;
    ctx.checkTrue(w.write(path, richSourceName(), richTable()), "rich fixture written (" + w.getErrorMessage() + ")");
    std::string text;
    ctx.checkTrue(loadFileBinary(path, text), "rich fixture readable");
    std::remove(path.c_str());
    return text;
}

void expectReaderError(TestContext& ctx, const std::string& text, const std::string& code,
                       const std::string& label, int expectedNodes = -1) {
    const std::string path = "bin/test_sr_bad.sym";
    ctx.checkTrue(writeRawFileBinary(path, text), label + ": write fixture");
    SymReader r;
    ctx.checkTrue(!r.read(path, expectedNodes), label + ": SymReader must reject");
    ctx.checkTrue(r.hasError(), label + ": hasError true");
    ctx.checkEqual(r.getErrorCode(), code, label + ": errorCode (" + r.getErrorMessage() + ")");
    ctx.checkTrue(!r.getErrorMessage().empty(), label + ": non-empty message");
    ctx.checkTrue(r.getSymbolTable().structs.empty() && r.getSymbolTable().functions.empty() &&
                  r.getSymbolTable().scopes.empty() && r.getSymbolTable().symbols.empty() &&
                  r.getSymbolTable().nodeTypes.empty(), label + ": failed read clears the table");
    ctx.checkTrue(r.getSourceFilename().empty(), label + ": failed read clears the source filename");
    std::remove(path.c_str());
}

// SR1: the Section 5.4 golden file is accepted and decoded field by field
void testSR1_GoldenMinReturn(TestContext& ctx) {
    SymReader r;
    ctx.checkTrue(r.read("tests/golden/min_return.sym", 7), "SR1: golden accepted (" + r.getErrorMessage() + ")");
    ctx.checkEqual(r.getSourceFilename(), std::string("min_return.citu"), "SR1: @SOURCE");
    checkTablesEqual(ctx, r.getSymbolTable(), minReturnTable(), "SR1");
    const SymbolTable& t = r.getSymbolTable();
    if (t.nodeTypes.size() == 7) {
        ctx.checkTrue(t.nodeTypes[6].isConstExpr && t.nodeTypes[6].foldedConstVal == 0,
                      "SR1: INT_LIT node 6 is a constant folded to 0");
        ctx.checkEqual(t.nodeTypes[1].refKind, std::string("FUNC_REF"), "SR1: FUNC_DEF node carries FUNC_REF");
    }
}

// SR2: writer -> reader round trip of every section and value class
void testSR2_RoundTrip(TestContext& ctx) {
    const std::string path = "bin/test_sr2.sym";
    SymWriter w;
    ctx.checkTrue(w.write(path, richSourceName(), richTable(), 10), "SR2: write");
    SymReader r;
    ctx.checkTrue(r.read(path, 10), "SR2: read back (" + r.getErrorMessage() + ")");
    ctx.checkEqual(r.getSourceFilename(), std::string(richSourceName()), "SR2: escaped @SOURCE decoded");
    checkTablesEqual(ctx, r.getSymbolTable(), richTable(), "SR2");
    if (r.getSymbolTable().structs.size() == 2) {
        ctx.checkEqual(r.getSymbolTable().structs[1].sourceFile, std::string(richSourceName()),
                       "SR2: escaped SOURCE_FILE decoded");
    }

    // Re-serializing the reconstructed table reproduces the identical file
    const std::string path2 = "bin/test_sr2_again.sym";
    ctx.checkTrue(w.write(path2, r.getSourceFilename(), r.getSymbolTable()), "SR2: re-write");
    std::string a;
    std::string b;
    ctx.checkTrue(loadFileBinary(path, a) && loadFileBinary(path2, b), "SR2: files readable");
    ctx.checkEqual(b, a, "SR2: write(read(write(T))) is byte-identical");
    std::remove(path.c_str());
    std::remove(path2.c_str());
}

// SR3: tolerated input variations (CRLF, comments, blank lines, detail-row placement)
void testSR3_ToleratedVariations(TestContext& ctx) {
    const std::string golden = goldenText(ctx);

    std::string crlf;
    for (char c : golden) {
        if (c == '\n') {
            crlf += "\r\n";
        } else {
            crlf.push_back(c);
        }
    }
    const std::string path = "bin/test_sr3.sym";
    SymReader r;
    ctx.checkTrue(writeRawFileBinary(path, crlf) && r.read(path, 7), "SR3: CRLF accepted (" + r.getErrorMessage() + ")");
    ctx.checkTrue(writeRawFileBinary(path, "# leading comment\n\n" + golden + "\n# trailing comment\n\n") &&
                  r.read(path), "SR3: comments and blank lines are uncounted (" + r.getErrorMessage() + ")");

    // @STRUCT_FIELD rows may be separated from their @STRUCT_DEF inside Section 1
    std::string rich = richText(ctx);
    rich = mutate(ctx, rich, "@STRUCT_FIELD | Point | 1 | y | int16\n", "", "SR3_move_field");
    rich = mutate(ctx, rich, "@STRUCT_FIELD | Box | 2 | tag | char\n",
                  "@STRUCT_FIELD | Box | 2 | tag | char\n@STRUCT_FIELD | Point | 1 | y | int16\n", "SR3_move_field");
    ctx.checkTrue(writeRawFileBinary(path, rich) && r.read(path),
                  "SR3: detail row later in its own section accepted (" + r.getErrorMessage() + ")");
    if (r.getSymbolTable().structs.size() == 2) {
        ctx.checkEqual(r.getSymbolTable().structs[0].fields.size(), static_cast<std::size_t>(2), "SR3: Point has 2 fields");
    }
    std::remove(path.c_str());
}

// SR4: header, footer, framing and record-shape errors
void testSR4_FormatErrors(TestContext& ctx) {
    const std::string g = goldenText(ctx);

    {
        SymReader r;
        ctx.checkTrue(!r.read("bin/no_such_file_sr4.sym"), "SR4: missing file fails");
        ctx.checkEqual(r.getErrorCode(), std::string(""), "SR4: OS error has no FATAL_IO code");
        ctx.checkTrue(r.getErrorMessage().find("OS I/O error") != std::string::npos, "SR4: OS error message");
    }

    expectReaderError(ctx, "", kFmt, "SR4_empty_file");
    expectReaderError(ctx, std::string("\xEF\xBB\xBF", 3) + g, kFmt, "SR4_bom");
    expectReaderError(ctx, mutate(ctx, g, "ICC_SYM", "ICC_AST", "SR4"), kFmt, "SR4_wrong_tag");
    expectReaderError(ctx, mutate(ctx, g, "| v1.0", "| v1.1", "SR4"), kFmt, "SR4_wrong_version");
    expectReaderError(ctx, mutate(ctx, g, "@SOURCE | min_return.citu\n", "", "SR4"), kFmt, "SR4_missing_source");
    expectReaderError(ctx, mutate(ctx, g, "@SOURCE | min_return.citu", "@SOURCE | ", "SR4"), kFmt, "SR4_empty_source");
    expectReaderError(ctx, mutate(ctx, g, "@COUNTS | STRUCTS=0 | FUNCS=1", "@COUNTS | FUNCS=1 | STRUCTS=0", "SR4"),
                      kFmt, "SR4_counts_wrong_order");
    expectReaderError(ctx, mutate(ctx, g, "STRUCTS=0", "STRUCTS=-1", "SR4"), kFmt, "SR4_counts_negative");
    expectReaderError(ctx, mutate(ctx, g, "STRUCTS=0", "STRUCTS=", "SR4"), kFmt, "SR4_counts_empty_value");
    expectReaderError(ctx, mutate(ctx, g, "NODE_TYPES=7", "NODE_TYPES=7 | EXTRA=0", "SR4"), kFmt, "SR4_counts_extra_item");
    expectReaderError(ctx, mutate(ctx, g, "@COUNTS | STRUCTS=0 | FUNCS=1 | SCOPES=1 | SYMBOLS=0 | NODE_TYPES=7\n", "", "SR4"),
                      kFmt, "SR4_missing_counts");
    expectReaderError(ctx, mutate(ctx, g, "@END_FILE\n", "", "SR4"), kEnd, "SR4_missing_end_file");
    expectReaderError(ctx, g + "@NODE_TYPE | 7 | int32 | @NONE | -1 | -1 | 0 | -1\n", kFmt, "SR4_data_after_end");
    expectReaderError(ctx, mutate(ctx, g, "@SCOPE_DEF | 0", "@SCOPE | 0", "SR4"), kFmt, "SR4_unknown_tag");
    expectReaderError(ctx, mutate(ctx, g, "@SCOPE_DEF | 0 | main | @NONE | main | 1 | 3",
                                  "@SCOPE_DEF | 0 | main | @NONE | main | 1", "SR4"), kFmt, "SR4_too_few_fields");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 6 | int32 | @NONE | -1 | -1 | 1 | 0",
                                  "@NODE_TYPE | 6 | int32 | @NONE | -1 | -1 | 1 | 0 | 0", "SR4"), kFmt, "SR4_too_many_fields");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 5 | int32 | @NONE | -1", "@NODE_TYPE | 5 | int32 | @NONE | x1", "SR4"),
                      kFmt, "SR4_non_numeric");
    expectReaderError(ctx, mutate(ctx, g, "@FUNC_DEF | 0 | main | int32 | 0 | 0 | min_return.citu | 1",
                                  "@FUNC_DEF | 0 | main | int32 | 0 | 0 | min_return.citu | 99999999999", "SR4"),
                      kFmt, "SR4_int_overflow");
    expectReaderError(ctx, mutate(ctx, g, "@FUNC_DEF | 0 | main |", "@FUNC_DEF | 0 | ma,in |", "SR4"), kFmt, "SR4_class_b_comma");
    expectReaderError(ctx, mutate(ctx, g, "| main | int32 | 0 | 0 |", "|  main | int32 | 0 | 0 |", "SR4"),
                      kFmt, "SR4_class_b_extra_padding");

    // Section order: a Section 2 record after Section 3 has started
    expectReaderError(ctx,
                      mutate(ctx, mutate(ctx, g, "@FUNC_DEF | 0 | main | int32 | 0 | 0 | min_return.citu | 1\n", "", "SR4"),
                             "@SCOPE_DEF | 0 | main | @NONE | main | 1 | 3\n",
                             "@SCOPE_DEF | 0 | main | @NONE | main | 1 | 3\n"
                             "@FUNC_DEF | 0 | main | int32 | 0 | 0 | min_return.citu | 1\n", "SR4"),
                      kFmt, "SR4_section_order");
}

// SR5: @COUNTS and FIELD_COUNT / PARAM_COUNT accounting
void testSR5_CountErrors(TestContext& ctx) {
    const std::string g = goldenText(ctx);
    expectReaderError(ctx, mutate(ctx, g, "NODE_TYPES=7", "NODE_TYPES=8", "SR5"), kCnt, "SR5_too_few_node_types");
    expectReaderError(ctx, mutate(ctx, g, "NODE_TYPES=7", "NODE_TYPES=6", "SR5"), kCnt, "SR5_too_many_node_types");
    expectReaderError(ctx, mutate(ctx, g, "FUNCS=1", "FUNCS=2", "SR5"), kCnt, "SR5_too_few_funcs");
    expectReaderError(ctx, mutate(ctx, g, "FUNCS=1", "FUNCS=0", "SR5"), kCnt, "SR5_too_many_funcs");
    expectReaderError(ctx, mutate(ctx, g, "SCOPES=1", "SCOPES=0", "SR5"), kCnt, "SR5_too_many_scopes");
    expectReaderError(ctx, mutate(ctx, g, "STRUCTS=0", "STRUCTS=1", "SR5"), kCnt, "SR5_too_few_structs");
    expectReaderError(ctx, mutate(ctx, g, "SYMBOLS=0", "SYMBOLS=1", "SR5"), kCnt, "SR5_too_few_symbols");

    const std::string rich = richText(ctx);
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_DEF | 0 | Point | 2 |", "@STRUCT_DEF | 0 | Point | 3 |", "SR5"),
                      kCnt, "SR5_missing_field_rows");
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_DEF | 0 | Point | 2 |", "@STRUCT_DEF | 0 | Point | 1 |", "SR5"),
                      kCnt, "SR5_extra_field_rows");
    expectReaderError(ctx, mutate(ctx, rich, "@FUNC_DEF | 0 | helper | uint32 | 1 |", "@FUNC_DEF | 0 | helper | uint32 | 2 |", "SR5"),
                      kCnt, "SR5_missing_param_rows");
    expectReaderError(ctx, mutate(ctx, rich, "@FUNC_DEF | 0 | helper | uint32 | 1 |", "@FUNC_DEF | 0 | helper | uint32 | 0 |", "SR5"),
                      kCnt, "SR5_extra_param_rows");
}

// SR6: Class A escaping in @SOURCE and SOURCE_FILE
void testSR6_EscapeErrors(TestContext& ctx) {
    const std::string g = goldenText(ctx);
    expectReaderError(ctx, mutate(ctx, g, "@SOURCE | min_return.citu", "@SOURCE | min%7creturn.citu", "SR6"),
                      kEsc, "SR6_lowercase_hex_in_source");
    expectReaderError(ctx, mutate(ctx, g, "@SOURCE | min_return.citu", "@SOURCE | min_return%", "SR6"),
                      kEsc, "SR6_truncated_escape_in_source");
    expectReaderError(ctx, mutate(ctx, g, "| 0 | 0 | min_return.citu | 1", "| 0 | 0 | min%41return.citu | 1", "SR6"),
                      kEsc, "SR6_unknown_escape_in_source_file");
}

// SR7: Section 5.4 validation-protocol invariants (Rules 1-5)
void testSR7_Invariants(TestContext& ctx) {
    const std::string g = goldenText(ctx);
    const std::string rich = richText(ctx);

    // Rule 1: contiguous IDs in every section
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_DEF | 1 | Box", "@STRUCT_DEF | 2 | Box", "SR7"), kInv, "SR7_struct_id");
    expectReaderError(ctx, mutate(ctx, rich, "@FUNC_DEF | 1 | noop", "@FUNC_DEF | 0 | noop", "SR7"), kInv, "SR7_func_id");
    expectReaderError(ctx, mutate(ctx, rich, "@SCOPE_DEF | 2 |", "@SCOPE_DEF | 3 |", "SR7"), kInv, "SR7_scope_id");
    expectReaderError(ctx, mutate(ctx, rich, "@SYMBOL | 1 |", "@SYMBOL | 0 |", "SR7"), kInv, "SR7_sym_id");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 3 |", "@NODE_TYPE | 4 |", "SR7"), kInv, "SR7_node_id");

    // Rule 2: detail rows name a defined owner and run 0..COUNT-1 in order
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_FIELD | Point | 0 |", "@STRUCT_FIELD | Pointe | 0 |", "SR7"),
                      kInv, "SR7_field_undefined_struct");
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_FIELD | Box | 1 | height", "@STRUCT_FIELD | Box | 2 | height", "SR7"),
                      kInv, "SR7_field_index_order");
    expectReaderError(ctx, mutate(ctx, rich, "@FUNC_PARAM | helper |", "@FUNC_PARAM | helpr |", "SR7"),
                      kInv, "SR7_param_undefined_function");
    expectReaderError(ctx, mutate(ctx, rich, "@FUNC_PARAM | helper | 0 |", "@FUNC_PARAM | helper | 1 |", "SR7"),
                      kInv, "SR7_param_index_order");
    expectReaderError(ctx, mutate(ctx, rich, "@STRUCT_DEF | 1 | Box", "@STRUCT_DEF | 1 | Point", "SR7"),
                      kInv, "SR7_duplicate_struct_name");

    // Rule 3: IS_EXTERNAL domain and SOURCE_FILE rule
    expectReaderError(ctx, mutate(ctx, g, "| int32 | 0 | 0 | min_return.citu", "| int32 | 0 | 2 | min_return.citu", "SR7"),
                      kInv, "SR7_is_external_2");
    expectReaderError(ctx, mutate(ctx, g, "| 0 | 0 | min_return.citu | 1", "| 0 | 0 | other.citu | 1", "SR7"),
                      kInv, "SR7_local_foreign_source");
    expectReaderError(ctx, mutate(ctx, g, "| 0 | 0 | min_return.citu | 1", "| 0 | 1 | min_return.citu | 1", "SR7"),
                      kInv, "SR7_external_own_source");

    // Rule 4: scope parents, symbol scope/function existence, (func, ident) uniqueness
    expectReaderError(ctx, mutate(ctx, rich, "| main.agar_1 | main | main |", "| main.agar_1 | mian | main |", "SR7"),
                      kInv, "SR7_missing_parent_scope");
    expectReaderError(ctx, mutate(ctx, rich, "| main.agar_1 | main | main |", "| main.agar_1 | noop | main |", "SR7"),
                      kInv, "SR7_parent_scope_other_function");
    expectReaderError(ctx, mutate(ctx, rich, "| n | LOCAL_VAR | int32 | main | main.agar_1 |",
                                  "| n | LOCAL_VAR | int32 | main | main.magar_1 |", "SR7"),
                      kInv, "SR7_symbol_unknown_scope");
    expectReaderError(ctx, mutate(ctx, rich, "| n | LOCAL_VAR | int32 | main |", "| n | LOCAL_VAR | int32 | mainx |", "SR7"),
                      kInv, "SR7_symbol_unknown_function");
    expectReaderError(ctx, mutate(ctx, rich, "@SYMBOL | 2 | p |", "@SYMBOL | 2 | b |", "SR7"),
                      kInv, "SR7_duplicate_func_ident");

    // Rule 5: NODE_TYPES vs @TOTAL_NODES, and RESOLVED_ID per REF_KIND
    expectReaderError(ctx, g, kInv, "SR7_node_types_vs_total_nodes", 8);
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 1 | int32 | FUNC_REF | 0", "@NODE_TYPE | 1 | int32 | FUNC_REF | 1", "SR7"),
                      kInv, "SR7_func_ref_out_of_range");
    expectReaderError(ctx, mutate(ctx, rich, "| struct Box | LOCAL_SYM | 0 |", "| struct Box | LOCAL_SYM | 3 |", "SR7"),
                      kInv, "SR7_local_sym_out_of_range");
    expectReaderError(ctx, mutate(ctx, rich, "| struct Box | STRUCT_TYPE | 1 |", "| struct Box | STRUCT_TYPE | 2 |", "SR7"),
                      kInv, "SR7_struct_type_out_of_range");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 0 | @NONE | @NONE | -1", "@NODE_TYPE | 0 | @NONE | @NONE | 0", "SR7"),
                      kInv, "SR7_none_with_resolved_id");
    expectReaderError(ctx, mutate(ctx, rich, "| uint16 | STRUCT_FIELD_DECL | 1 | 1 |", "| uint16 | STRUCT_FIELD_DECL | 1 | 3 |", "SR7"),
                      kInv, "SR7_field_decl_sub_id");
    expectReaderError(ctx, mutate(ctx, rich, "| char | FIELD_ACCESS_REF | 0 | 2 |", "| char | FIELD_ACCESS_REF | 1 | 0 |", "SR7"),
                      kInv, "SR7_field_access_non_struct_symbol");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 1 | int32 | FUNC_REF | 0 | -1", "@NODE_TYPE | 1 | int32 | FUNC_REF | 0 | 0", "SR7"),
                      kInv, "SR7_sub_id_not_applicable");

    // Section 4.1 value domains and the IS_CONST_EXPR / FOLDED_CONST_VAL convention
    expectReaderError(ctx, mutate(ctx, g, "| 1 | 0\n@END_FILE", "| 2 | 0\n@END_FILE", "SR7"), kInv, "SR7_is_const_2");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 5 | int32 | @NONE | -1 | -1 | 0 | -1",
                                  "@NODE_TYPE | 5 | int32 | @NONE | -1 | -1 | 0 | 42", "SR7"),
                      kInv, "SR7_folded_without_const");
    expectReaderError(ctx, mutate(ctx, g, "@NODE_TYPE | 5 | int32 |", "@NODE_TYPE | 5 | INT_LITERAL |", "SR7"),
                      kInv, "SR7_bad_evaluated_type");
    expectReaderError(ctx, mutate(ctx, g, "| int32 | FUNC_REF |", "| int32 | FUNCTION_REF |", "SR7"), kInv, "SR7_bad_ref_kind");
    expectReaderError(ctx, mutate(ctx, g, "| main | int32 | 0 |", "| main | int64 | 0 |", "SR7"), kInv, "SR7_bad_return_type");
    expectReaderError(ctx, mutate(ctx, rich, "| x | int32\n", "| x | int8\n", "SR7"), kInv, "SR7_bad_field_type");
    expectReaderError(ctx, mutate(ctx, rich, "| b | STRUCT_VAR |", "| b | GLOBAL_VAR |", "SR7"), kInv, "SR7_bad_sym_kind");
    expectReaderError(ctx, mutate(ctx, rich, "| STRUCT_VAR | struct Box |", "| STRUCT_VAR | structBox |", "SR7"),
                      kInv, "SR7_bad_data_type");
    expectReaderError(ctx, mutate(ctx, g, "| main | @NONE | main | 1 | 3", "| main | @NONE | main | 0 | 3", "SR7"),
                      kInv, "SR7_zero_line");
    expectReaderError(ctx,
                      mutate(ctx, mutate(ctx, g, "NODE_TYPES=7", "NODE_TYPES=0", "SR7"),
                             "@NODE_TYPE | 0 | @NONE | @NONE | -1 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 1 | int32 | FUNC_REF | 0 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 2 | @NONE | @NONE | -1 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 3 | @NONE | @NONE | -1 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 4 | @NONE | @NONE | -1 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 5 | int32 | @NONE | -1 | -1 | 0 | -1\n"
                             "@NODE_TYPE | 6 | int32 | @NONE | -1 | -1 | 1 | 0\n", "", "SR7"),
                      kInv, "SR7_zero_node_types");
}

// SR8: reuse of one SymReader instance across success and failure
void testSR8_Reuse(TestContext& ctx) {
    const std::string good = "bin/test_sr8_good.sym";
    const std::string bad = "bin/test_sr8_bad.sym";
    const std::string rich = "bin/test_sr8_rich.sym";
    std::string g = goldenText(ctx);
    ctx.checkTrue(writeRawFileBinary(good, g), "SR8: write good");
    ctx.checkTrue(writeRawFileBinary(bad, mutate(ctx, g, "FUNCS=1", "FUNCS=2", "SR8")), "SR8: write bad");
    SymWriter w;
    ctx.checkTrue(w.write(rich, richSourceName(), richTable()), "SR8: write rich");

    SymReader r;
    ctx.checkTrue(r.read(rich), "SR8: rich ok");
    ctx.checkEqual(r.getSymbolTable().structs.size(), static_cast<std::size_t>(2), "SR8: rich structs");
    ctx.checkTrue(r.read(good, 7), "SR8: good after rich");
    ctx.checkEqual(r.getSymbolTable().structs.size(), static_cast<std::size_t>(0), "SR8: no stale structs from previous read");
    ctx.checkEqual(r.getSymbolTable().functions.size(), static_cast<std::size_t>(1), "SR8: one function");
    ctx.checkTrue(!r.read(bad), "SR8: bad fails");
    ctx.checkEqual(r.getErrorCode(), kCnt, "SR8: count mismatch code");
    ctx.checkTrue(r.getErrorLine() > 0, "SR8: error line reported");
    ctx.checkTrue(!r.read(good, 8), "SR8: good fails against wrong @TOTAL_NODES");
    ctx.checkTrue(r.read(good, 7), "SR8: recovery read");
    ctx.checkTrue(!r.hasError(), "SR8: hasError reset");
    ctx.checkEqual(r.getErrorCode(), std::string(""), "SR8: errorCode reset");
    ctx.checkEqual(r.getErrorMessage(), std::string(""), "SR8: errorMessage reset");
    ctx.checkEqual(r.getErrorLine(), 0, "SR8: errorLine reset");
    ctx.checkEqual(r.getSourceFilename(), std::string("min_return.citu"), "SR8: source restored");
    checkTablesEqual(ctx, r.getSymbolTable(), minReturnTable(), "SR8");

    std::remove(good.c_str());
    std::remove(bad.c_str());
    std::remove(rich.c_str());
}

} // namespace

int main() {
    return runSuite("ICC Phase 3 (Stage 3A) SymReader Test Suite", {
        {"SR1_Golden_Min_Return_Accepted", testSR1_GoldenMinReturn},
        {"SR2_Writer_Reader_Round_Trip", testSR2_RoundTrip},
        {"SR3_Tolerated_Variations", testSR3_ToleratedVariations},
        {"SR4_Header_Footer_And_Framing_Errors", testSR4_FormatErrors},
        {"SR5_Count_Accounting_Errors", testSR5_CountErrors},
        {"SR6_Class_A_Escape_Errors", testSR6_EscapeErrors},
        {"SR7_Validation_Protocol_Invariants", testSR7_Invariants},
        {"SR8_Instance_Reuse", testSR8_Reuse}
    });
}
