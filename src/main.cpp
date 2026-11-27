// icc — ITU Compiler Collection command-line driver (Phases 2 and 3).
//
// Implements the file-decoupled front end of Specification v1.4, Section 1.1:
//
//   <prog>.citu --Lexer--> LexWriter --> <prog>.lex
//   <prog>.lex  --LexReader--> Parser --> AstWriter --> <prog>.ast
//   <prog>.ast (+ <prog>.lex, + earlier units' .sym)
//       --AstReader / LexReader / SymReader--> SemanticAnalyzer --SymWriter--> <prog>.sym
//
// No stage receives another stage's in-memory data: each one is handed only file
// paths and reconstructs its input with the matching Reader.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ast_reader.h"
#include "ast_writer.h"
#include "lex_writer.h"
#include "lexer.h"
#include "parser.h"
#include "semantic_analyzer.h"

namespace {

const char* const kVersion =
    "icc 0.3.0 (Phases 2-3: Lexer, LL(1) Parser, AST, Semantic Analyzer; Citu spec v1.4)";

enum ExitCode {
    kExitOk = 0,
    kExitCompileError = 1,  // lexical, syntax, semantic or intermediate-file validation error
    kExitUsage = 2,         // bad command line
    kExitIo = 3             // cannot read source / write intermediate file
};

enum class StopAfter { Lex, Parse, Sema };

struct Options {
    std::vector<std::string> inputs;
    std::string outDir;              // empty => alongside each source file
    StopAfter stopAfter = StopAfter::Sema;
    bool quiet = false;
};

void printUsage(std::ostream& os) {
    os << "Usage: icc [options] <file.citu> [<file.citu> ...]\n"
          "\n"
          "Runs the front end on each source file, in command-line order:\n"
          "  <file>.citu -> <file>.lex (Lexer + LexWriter)\n"
          "  <file>.lex  -> <file>.ast (LexReader + LL(1) Parser + AstWriter)\n"
          "  <file>.ast  -> <file>.sym (AstReader + Semantic Analyzer + SymWriter)\n"
          "Files on one command line form one program: each file imports Sections 1-2\n"
          "of the .sym files of the files before it, so providers must come first.\n"
          "If a file fails semantic analysis, the files after it are not analyzed.\n"
          "\n"
          "Options:\n"
          "  -d, --out-dir <dir>     Write .lex/.ast/.sym into <dir> (must exist).\n"
          "                          Default: the directory of each source file.\n"
          "  --stop-after <stage>    'lex', 'parse', or 'sema' (default).\n"
          "  -q, --quiet             Print diagnostics only.\n"
          "  -h, --help              Show this help.\n"
          "  --version               Show version information.\n"
          "\n"
          "Exit status: 0 success, 1 lexical/syntax/semantic/format error, 2 usage error,\n"
          "3 I/O error. Code generation, optimizers and '-o app.exe' are not part of\n"
          "this build.\n";
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Splits "dir/sub/prog.citu" into directory "dir/sub/" (with trailing separator,
// or empty) and base name "prog.citu". Accepts both '/' and '\\'.
void splitPath(const std::string& path, std::string& dirOut, std::string& baseOut) {
    const std::size_t sep = path.find_last_of("/\\");
    if (sep == std::string::npos) {
        dirOut.clear();
        baseOut = path;
    } else {
        dirOut = path.substr(0, sep + 1);
        baseOut = path.substr(sep + 1);
    }
}

std::string joinDir(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        return false;
    }
    out = ss.str();
    return true;
}

bool fileExists(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return in.is_open();
}

// Output paths for one input file
struct UnitPaths {
    std::string source;
    std::string baseName;   // "prog.citu"
    std::string lex;
    std::string ast;
    std::string sym;
};

UnitPaths unitPathsFor(const std::string& sourcePath, const Options& opts) {
    UnitPaths p;
    std::string srcDir;
    splitPath(sourcePath, srcDir, p.baseName);
    const std::string stem = p.baseName.substr(0, p.baseName.size() - std::string(".citu").size());
    const std::string outDir = opts.outDir.empty() ? srcDir : opts.outDir;
    p.source = sourcePath;
    p.lex = joinDir(outDir, stem + ".lex");
    p.ast = joinDir(outDir, stem + ".ast");
    p.sym = joinDir(outDir, stem + ".sym");
    return p;
}

// Returns kExitOk on success, otherwise the exit code describing the failure.
int parseArguments(int argc, char** argv, Options& opts, bool& exitEarly) {
    exitEarly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(std::cout);
            exitEarly = true;
            return kExitOk;
        }
        if (arg == "--version") {
            std::cout << kVersion << "\n";
            exitEarly = true;
            return kExitOk;
        }
        if (arg == "-q" || arg == "--quiet") {
            opts.quiet = true;
            continue;
        }
        if (arg == "-d" || arg == "--out-dir") {
            if (i + 1 >= argc) {
                std::cerr << "icc: error: " << arg << " requires a directory argument\n";
                return kExitUsage;
            }
            opts.outDir = argv[++i];
            continue;
        }
        if (arg == "--stop-after") {
            if (i + 1 >= argc) {
                std::cerr << "icc: error: --stop-after requires 'lex', 'parse' or 'sema'\n";
                return kExitUsage;
            }
            const std::string stage = argv[++i];
            if (stage == "lex") {
                opts.stopAfter = StopAfter::Lex;
            } else if (stage == "parse") {
                opts.stopAfter = StopAfter::Parse;
            } else if (stage == "sema") {
                opts.stopAfter = StopAfter::Sema;
            } else {
                std::cerr << "icc: error: unknown stage '" << stage
                          << "' (expected 'lex', 'parse' or 'sema')\n";
                return kExitUsage;
            }
            continue;
        }
        if (arg == "-o") {
            std::cerr << "icc: error: '-o' (executable output) belongs to Phase 8 and is not "
                         "implemented in this build\n";
            return kExitUsage;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "icc: error: unknown option '" << arg << "'\n";
            return kExitUsage;
        }
        opts.inputs.push_back(arg);
    }

    if (opts.inputs.empty()) {
        std::cerr << "icc: error: no input files\n";
        printUsage(std::cerr);
        return kExitUsage;
    }
    std::set<std::string> outputs;
    for (const std::string& in : opts.inputs) {
        if (!endsWith(in, ".citu")) {
            std::cerr << "icc: error: input '" << in << "' does not have the .citu extension\n";
            return kExitUsage;
        }
        if (!outputs.insert(unitPathsFor(in, opts).ast).second) {
            std::cerr << "icc: error: input '" << in
                      << "' would overwrite the outputs of an earlier input with the same name\n";
            return kExitUsage;
        }
    }
    return kExitOk;
}

// Stages 1-2 for one source file: .citu -> .lex -> .ast (or .lex only).
int runFrontEnd(const UnitPaths& p, const Options& opts, std::size_t& tokenCount, std::size_t& nodeCount) {
    // Remove stale .lex/.ast of a previous run so a failed stage can never leave
    // an older, unrelated .lex/.ast behind that looks like this run's output.
    std::remove(p.lex.c_str());
    std::remove(p.ast.c_str());

    std::string source;
    if (!readWholeFile(p.source, source)) {
        std::cerr << "icc: error: cannot read source file '" << p.source << "'\n";
        return kExitIo;
    }

    Lexer lexer(source, p.baseName);
    if (!lexer.scanTokens()) {
        std::cerr << lexer.getErrorMessage() << "\n";
        return kExitCompileError;
    }
    LexWriter lexWriter;
    if (!lexWriter.write(p.lex, p.baseName, lexer.getTokens())) {
        std::cerr << "icc: error: " << lexWriter.getErrorMessage() << "\n";
        return lexWriter.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }
    tokenCount = lexer.getTokens().size();
    if (opts.stopAfter == StopAfter::Lex) {
        return kExitOk;
    }

    Parser parser;
    if (!parser.parseFromLexFile(p.lex)) {
        std::cerr << parser.getErrorMessage() << "\n";
        return parser.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }
    AstWriter astWriter;
    if (!astWriter.write(p.ast, parser.getSourceFilename(), parser.getAstNodes())) {
        std::cerr << "icc: error: " << astWriter.getErrorMessage() << "\n";
        return astWriter.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }
    AstReader verifier;
    if (!verifier.read(p.ast) || verifier.getAstNodes().size() != parser.getAstNodes().size()) {
        std::cerr << "icc: internal error: written .ast failed re-validation: "
                  << verifier.getErrorMessage() << "\n";
        return kExitCompileError;
    }
    nodeCount = parser.getAstNodes().size();
    return kExitOk;
}

void noteKeptStaleSym(const UnitPaths& p) {
    if (fileExists(p.sym)) {
        std::cerr << "icc: note: existing '" << p.sym << "' from an earlier run was left unchanged; "
                     "it does not correspond to the current '" << p.source << "'\n";
    }
}

// Stage 3: .ast (+ .lex, + earlier .sym) -> .sym.
// The .sym is first written to '<stem>.sym.tmp' and only renamed over '<stem>.sym'
// after SymWriter succeeds, so a failed analysis or write never leaves a new or
// partial .sym behind, and never deletes a pre-existing one.
int runSemantic(const UnitPaths& p, const std::vector<std::string>& earlierSyms, std::size_t& symbolCount) {
    SemanticAnalyzer analyzer;
    if (!analyzer.analyze(p.ast, p.lex, earlierSyms)) {
        std::cerr << analyzer.getErrorMessage() << "\n";
        noteKeptStaleSym(p);
        return analyzer.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }

    const std::string tmp = p.sym + ".tmp";
    std::remove(tmp.c_str());
    if (!analyzer.writeSym(tmp)) {
        std::remove(tmp.c_str());
        std::cerr << "icc: error: " << analyzer.getErrorMessage() << "\n";
        noteKeptStaleSym(p);
        return analyzer.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }
    std::remove(p.sym.c_str());
    if (std::rename(tmp.c_str(), p.sym.c_str()) != 0) {
        std::remove(tmp.c_str());
        std::cerr << "icc: error: cannot move '" << tmp << "' to '" << p.sym << "'\n";
        return kExitIo;
    }
    symbolCount = analyzer.getSymbolTable().symbols.size();
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    bool exitEarly = false;
    const int argStatus = parseArguments(argc, argv, opts, exitEarly);
    if (argStatus != kExitOk || exitEarly) {
        return argStatus;
    }

    int worst = kExitOk;
    std::vector<std::string> earlierSyms;   // .sym files produced so far, in command-line order
    bool semanticChainBroken = false;

    for (const std::string& input : opts.inputs) {
        const UnitPaths p = unitPathsFor(input, opts);

        if (semanticChainBroken) {
            std::cerr << "icc: note: '" << input << "' was not compiled because an earlier file "
                         "on the command line failed (later files import earlier .sym files)\n";
            continue;
        }

        std::size_t tokens = 0;
        std::size_t nodes = 0;
        int status = runFrontEnd(p, opts, tokens, nodes);
        std::size_t symbols = 0;
        if (status == kExitOk && opts.stopAfter == StopAfter::Sema) {
            status = runSemantic(p, earlierSyms, symbols);
        } else if (status != kExitOk && opts.stopAfter == StopAfter::Sema) {
            noteKeptStaleSym(p);
        }

        if (status != kExitOk) {
            if (status > worst) {
                worst = status;
            }
            if (opts.stopAfter == StopAfter::Sema) {
                semanticChainBroken = true;
            }
            continue;
        }

        if (opts.stopAfter == StopAfter::Sema) {
            earlierSyms.push_back(p.sym);
        }
        if (!opts.quiet) {
            std::cout << input << ": " << tokens << " tokens -> " << p.lex;
            if (opts.stopAfter != StopAfter::Lex) {
                std::cout << "; " << nodes << " AST nodes -> " << p.ast;
            }
            if (opts.stopAfter == StopAfter::Sema) {
                std::cout << "; " << symbols << " symbols -> " << p.sym;
            }
            std::cout << "\n";
        }
    }
    return worst;
}
