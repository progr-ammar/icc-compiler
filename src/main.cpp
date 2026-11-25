// icc — ITU Compiler Collection, Phase 2 command-line driver.
//
// Implements the file-decoupled front end of Specification v1.4, Section 1.1:
//
//   <prog>.citu --Lexer--> LexWriter --> <prog>.lex
//   <prog>.lex  --LexReader--> Parser --> AstWriter --> <prog>.ast
//
// The Parser never receives the Lexer's in-memory token vector: it is handed
// only the path of the .lex file and reconstructs the tokens with LexReader.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast_reader.h"
#include "ast_writer.h"
#include "lex_writer.h"
#include "lexer.h"
#include "parser.h"

namespace {

const char* const kVersion = "icc 0.2.0 (Phase 2: Lexer + LL(1) Parser + AST; Citu spec v1.4)";

enum ExitCode {
    kExitOk = 0,
    kExitCompileError = 1,  // lexical, syntax or intermediate-file validation error
    kExitUsage = 2,         // bad command line
    kExitIo = 3             // cannot read source / write intermediate file
};

enum class StopAfter { Lex, Parse };

struct Options {
    std::vector<std::string> inputs;
    std::string outDir;              // empty => alongside each source file
    StopAfter stopAfter = StopAfter::Parse;
    bool quiet = false;
};

void printUsage(std::ostream& os) {
    os << "Usage: icc [options] <file.citu> [<file.citu> ...]\n"
          "\n"
          "Runs the Phase 2 front end on each source file:\n"
          "  <file>.citu -> <file>.lex (Lexer + LexWriter)\n"
          "  <file>.lex  -> <file>.ast (LexReader + LL(1) Parser + AstWriter)\n"
          "\n"
          "Options:\n"
          "  -d, --out-dir <dir>     Write .lex/.ast into <dir> (must exist).\n"
          "                          Default: the directory of each source file.\n"
          "  --stop-after <stage>    'lex' (write .lex only) or 'parse' (default).\n"
          "  -q, --quiet             Print diagnostics only.\n"
          "  -h, --help              Show this help.\n"
          "  --version               Show version information.\n"
          "\n"
          "Exit status: 0 success, 1 lexical/syntax/format error, 2 usage error, 3 I/O error.\n"
          "Later phases (semantic analysis, code generation, '-o app.exe') are not part of\n"
          "this Phase 2 build.\n";
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
                std::cerr << "icc: error: --stop-after requires 'lex' or 'parse'\n";
                return kExitUsage;
            }
            const std::string stage = argv[++i];
            if (stage == "lex") {
                opts.stopAfter = StopAfter::Lex;
            } else if (stage == "parse") {
                opts.stopAfter = StopAfter::Parse;
            } else {
                std::cerr << "icc: error: unknown stage '" << stage << "' (expected 'lex' or 'parse')\n";
                return kExitUsage;
            }
            continue;
        }
        if (arg == "-o") {
            std::cerr << "icc: error: '-o' (executable output) belongs to Phase 8 and is not "
                         "implemented in this Phase 2 build\n";
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
    for (const std::string& in : opts.inputs) {
        if (!endsWith(in, ".citu")) {
            std::cerr << "icc: error: input '" << in << "' does not have the .citu extension\n";
            return kExitUsage;
        }
    }
    return kExitOk;
}

// Runs the Phase 2 pipeline for one source file.
int compileOne(const std::string& sourcePath, const Options& opts) {
    std::string srcDir;
    std::string srcBase;
    splitPath(sourcePath, srcDir, srcBase);
    const std::string stem = srcBase.substr(0, srcBase.size() - std::string(".citu").size());
    const std::string outDir = opts.outDir.empty() ? srcDir : opts.outDir;
    const std::string lexPath = joinDir(outDir, stem + ".lex");
    const std::string astPath = joinDir(outDir, stem + ".ast");

    // Remove stale outputs of a previous run so a failed stage can never leave
    // an older, unrelated .lex/.ast behind that looks like this run's output.
    std::remove(lexPath.c_str());
    std::remove(astPath.c_str());

    // ---- Stage 1: Lexer -> LexWriter -> <stem>.lex -------------------------
    std::string source;
    if (!readWholeFile(sourcePath, source)) {
        std::cerr << "icc: error: cannot read source file '" << sourcePath << "'\n";
        return kExitIo;
    }

    Lexer lexer(source, srcBase);
    if (!lexer.scanTokens()) {
        std::cerr << lexer.getErrorMessage() << "\n";
        return kExitCompileError;
    }

    LexWriter lexWriter;
    if (!lexWriter.write(lexPath, srcBase, lexer.getTokens())) {
        std::cerr << "icc: error: " << lexWriter.getErrorMessage() << "\n";
        return lexWriter.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }
    const std::size_t tokenCount = lexer.getTokens().size();

    if (opts.stopAfter == StopAfter::Lex) {
        if (!opts.quiet) {
            std::cout << sourcePath << ": " << tokenCount << " tokens -> " << lexPath << "\n";
        }
        return kExitOk;
    }

    // ---- Stage 2: <stem>.lex -> LexReader -> Parser -> AstWriter -> <stem>.ast
    Parser parser;
    if (!parser.parseFromLexFile(lexPath)) {
        std::cerr << parser.getErrorMessage() << "\n";
        return parser.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }

    AstWriter astWriter;
    if (!astWriter.write(astPath, parser.getSourceFilename(), parser.getAstNodes())) {
        std::cerr << "icc: error: " << astWriter.getErrorMessage() << "\n";
        return astWriter.getErrorCode().empty() ? kExitIo : kExitCompileError;
    }

    // ---- Self-check: the .ast just written must pass the AstReader protocol
    AstReader verifier;
    if (!verifier.read(astPath) || verifier.getAstNodes().size() != parser.getAstNodes().size()) {
        std::cerr << "icc: internal error: written .ast failed re-validation: "
                  << verifier.getErrorMessage() << "\n";
        return kExitCompileError;
    }

    if (!opts.quiet) {
        std::cout << sourcePath << ": " << tokenCount << " tokens -> " << lexPath << "; "
                  << parser.getAstNodes().size() << " AST nodes -> " << astPath << "\n";
    }
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
    for (const std::string& input : opts.inputs) {
        const int status = compileOne(input, opts);
        if (status > worst) {
            worst = status;
        }
    }
    return worst;
}
