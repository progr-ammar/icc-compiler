#include "parser.h"

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ast_schema.h"
#include "lex_reader.h"

namespace {

// ============================================================================
// Exact 39 Non-Terminals and 33 Terminals (Specification v1.4, Sections 3.2–3.3)
// ============================================================================

enum class NonTerminal : int {
    PROG = 0,
    STRUCT_LIST,
    STRUCT_DEF,
    FIELD_LIST,
    FIELD_TAIL,
    FIELD,
    FUNC_LIST,
    FUNC_TAIL,
    FUNC_DEF,
    RET_TYPE,
    FUNC_NAME,
    PARAM_LIST,
    PARAM_TAIL,
    PARAM,
    CST,
    DLIST,
    DS,
    TYPE,
    SLIST,
    ST,
    WS,
    IF_STMT,
    ELSE_PART,
    RET_STMT,
    RET_VAL,
    AS,
    AS_TAIL,
    VAR_TAIL,
    COND,
    COND_TAIL,
    REL_OP,
    EX,
    EX_PRIME,
    T,
    T_PRIME,
    F,
    F_TAIL,
    ARG_LIST,
    ARG_TAIL,
    COUNT
};

enum class Terminal : int {
    STRUCT = 0,
    FN,
    MAIN,
    KHALI,
    WAPIS,
    SHRO,
    KHTM,
    JABTAK,
    AGAR,
    MAGAR,
    MEDALO,
    INT16,
    INT32,
    UINT16,
    UINT32,
    CHAR,
    ID,
    LT,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    LESS,
    GREATER,
    EQUAL,
    NOT_EQUAL,
    LESS_EQUAL,
    GREATER_EQUAL,
    LPAREN,
    RPAREN,
    COMMA,
    DOT,
    END_OF_INPUT,
    COUNT
};

constexpr std::size_t kNumNonTerminals = static_cast<std::size_t>(NonTerminal::COUNT);
constexpr std::size_t kNumTerminals = static_cast<std::size_t>(Terminal::COUNT);
constexpr std::size_t kNumProductions = 77;

struct GrammarSymbol {
    bool isTerminal = false;
    int id = 0;

    static GrammarSymbol term(Terminal t) {
        return GrammarSymbol{true, static_cast<int>(t)};
    }

    static GrammarSymbol nonTerm(NonTerminal nt) {
        return GrammarSymbol{false, static_cast<int>(nt)};
    }
};

struct Production {
    int ruleNumber = 0;
    NonTerminal lhs = NonTerminal::PROG;
    std::vector<GrammarSymbol> rhs; // empty vector represents epsilon
};

const char* terminalName(Terminal t) {
    static const std::array<const char*, kNumTerminals> kNames = {
        "struct", "fn", "main", "khali", "wapis", "shro", "khtm",
        "jabtak", "agar", "magar", "medalo",
        "int16", "int32", "uint16", "uint32", "char",
        "ID", "LT",
        "+", "-", "*", "/",
        "<", ">", "=", "!=", "<=", ">=",
        "(", ")", ",", ".", "$"
    };
    return kNames[static_cast<std::size_t>(t)];
}

const char* nonTerminalName(NonTerminal nt) {
    static const std::array<const char*, kNumNonTerminals> kNames = {
        "PROG", "STRUCT_LIST", "STRUCT_DEF", "FIELD_LIST", "FIELD_TAIL", "FIELD",
        "FUNC_LIST", "FUNC_TAIL", "FUNC_DEF", "RET_TYPE", "FUNC_NAME",
        "PARAM_LIST", "PARAM_TAIL", "PARAM", "CST", "DLIST", "DS", "TYPE",
        "SLIST", "ST", "WS", "IF_STMT", "ELSE_PART", "RET_STMT", "RET_VAL",
        "AS", "AS_TAIL", "VAR_TAIL", "COND", "COND_TAIL", "REL_OP",
        "EX", "EX'", "T", "T'", "F", "F_TAIL", "ARG_LIST", "ARG_TAIL"
    };
    return kNames[static_cast<std::size_t>(nt)];
}

bool mapStringToTerminal(const std::string& termStr, Terminal& outTerm) {
    static const std::unordered_map<std::string, Terminal> kMap = {
        {"struct", Terminal::STRUCT}, {"fn", Terminal::FN}, {"main", Terminal::MAIN},
        {"khali", Terminal::KHALI}, {"wapis", Terminal::WAPIS}, {"shro", Terminal::SHRO},
        {"khtm", Terminal::KHTM}, {"jabtak", Terminal::JABTAK}, {"agar", Terminal::AGAR},
        {"magar", Terminal::MAGAR}, {"medalo", Terminal::MEDALO},
        {"int16", Terminal::INT16}, {"int32", Terminal::INT32}, {"uint16", Terminal::UINT16},
        {"uint32", Terminal::UINT32}, {"char", Terminal::CHAR},
        {"ID", Terminal::ID}, {"LT", Terminal::LT},
        {"+", Terminal::PLUS}, {"-", Terminal::MINUS}, {"*", Terminal::STAR}, {"/", Terminal::SLASH},
        {"<", Terminal::LESS}, {">", Terminal::GREATER}, {"=", Terminal::EQUAL},
        {"!=", Terminal::NOT_EQUAL}, {"<=", Terminal::LESS_EQUAL}, {">=", Terminal::GREATER_EQUAL},
        {"(", Terminal::LPAREN}, {")", Terminal::RPAREN}, {",", Terminal::COMMA},
        {".", Terminal::DOT}, {"$", Terminal::END_OF_INPUT}
    };
    const auto it = kMap.find(termStr);
    if (it == kMap.end()) {
        return false;
    }
    outTerm = it->second;
    return true;
}

// ============================================================================
// Complete 77 Productions (1-indexed: index 0 unused, indices 1..77)
// ============================================================================

const std::vector<Production>& getProductions() {
    static const std::vector<Production> kProductions = []() {
        using NT = NonTerminal;
        using T = Terminal;
        const auto N = GrammarSymbol::nonTerm;
        const auto Tm = GrammarSymbol::term;

        std::vector<Production> p(kNumProductions + 1);

        // Group 1: Program Root & Struct Definitions (1-8)
        p[1]  = {1,  NT::PROG,        {N(NT::STRUCT_LIST), N(NT::FUNC_LIST)}};
        p[2]  = {2,  NT::STRUCT_LIST, {N(NT::STRUCT_DEF), N(NT::STRUCT_LIST)}};
        p[3]  = {3,  NT::STRUCT_LIST, {}};
        p[4]  = {4,  NT::STRUCT_DEF,  {Tm(T::STRUCT), Tm(T::ID), Tm(T::SHRO), N(NT::FIELD_LIST), Tm(T::KHTM)}};
        p[5]  = {5,  NT::FIELD_LIST,  {N(NT::FIELD), N(NT::FIELD_TAIL)}};
        p[6]  = {6,  NT::FIELD_TAIL,  {N(NT::FIELD), N(NT::FIELD_TAIL)}};
        p[7]  = {7,  NT::FIELD_TAIL,  {}};
        p[8]  = {8,  NT::FIELD,       {N(NT::TYPE), Tm(T::ID)}};

        // Group 2: Function Definitions & Headers (9-16)
        p[9]  = {9,  NT::FUNC_LIST,   {N(NT::FUNC_DEF), N(NT::FUNC_TAIL)}};
        p[10] = {10, NT::FUNC_TAIL,   {N(NT::FUNC_DEF), N(NT::FUNC_TAIL)}};
        p[11] = {11, NT::FUNC_TAIL,   {}};
        p[12] = {12, NT::FUNC_DEF,    {Tm(T::FN), N(NT::RET_TYPE), N(NT::FUNC_NAME), Tm(T::LPAREN), N(NT::PARAM_LIST), Tm(T::RPAREN), N(NT::CST)}};
        p[13] = {13, NT::RET_TYPE,    {N(NT::TYPE)}};
        p[14] = {14, NT::RET_TYPE,    {Tm(T::KHALI)}};
        p[15] = {15, NT::FUNC_NAME,   {Tm(T::ID)}};
        p[16] = {16, NT::FUNC_NAME,   {Tm(T::MAIN)}};

        // Group 3: Function Parameters (17-21)
        p[17] = {17, NT::PARAM_LIST,  {N(NT::PARAM), N(NT::PARAM_TAIL)}};
        p[18] = {18, NT::PARAM_LIST,  {}};
        p[19] = {19, NT::PARAM_TAIL,  {Tm(T::COMMA), N(NT::PARAM), N(NT::PARAM_TAIL)}};
        p[20] = {20, NT::PARAM_TAIL,  {}};
        p[21] = {21, NT::PARAM,       {N(NT::TYPE), Tm(T::ID)}};

        // Group 4: Scopes, Compacted Top Declarations & Types (22-31)
        p[22] = {22, NT::CST,         {Tm(T::SHRO), N(NT::DLIST), N(NT::SLIST), Tm(T::KHTM)}};
        p[23] = {23, NT::DLIST,       {N(NT::DS), N(NT::DLIST)}};
        p[24] = {24, NT::DLIST,       {}};
        p[25] = {25, NT::DS,          {N(NT::TYPE), Tm(T::ID)}};
        p[26] = {26, NT::DS,          {Tm(T::STRUCT), Tm(T::ID), Tm(T::ID)}};
        p[27] = {27, NT::TYPE,        {Tm(T::INT16)}};
        p[28] = {28, NT::TYPE,        {Tm(T::INT32)}};
        p[29] = {29, NT::TYPE,        {Tm(T::UINT16)}};
        p[30] = {30, NT::TYPE,        {Tm(T::UINT32)}};
        p[31] = {31, NT::TYPE,        {Tm(T::CHAR)}};

        // Group 5: Executable Statements & Control Flow (32-50)
        p[32] = {32, NT::SLIST,       {N(NT::ST), N(NT::SLIST)}};
        p[33] = {33, NT::SLIST,       {}};
        p[34] = {34, NT::ST,          {N(NT::WS)}};
        p[35] = {35, NT::ST,          {N(NT::IF_STMT)}};
        p[36] = {36, NT::ST,          {N(NT::AS)}};
        p[37] = {37, NT::ST,          {N(NT::RET_STMT)}};
        p[38] = {38, NT::ST,          {N(NT::CST)}};
        p[39] = {39, NT::WS,          {Tm(T::JABTAK), Tm(T::LPAREN), N(NT::COND), Tm(T::RPAREN), N(NT::CST)}};
        p[40] = {40, NT::IF_STMT,     {Tm(T::AGAR), Tm(T::LPAREN), N(NT::COND), Tm(T::RPAREN), N(NT::CST), N(NT::ELSE_PART)}};
        p[41] = {41, NT::ELSE_PART,   {Tm(T::MAGAR), N(NT::CST)}};
        p[42] = {42, NT::ELSE_PART,   {}};
        p[43] = {43, NT::RET_STMT,    {Tm(T::WAPIS), N(NT::RET_VAL)}};
        p[44] = {44, NT::RET_VAL,     {N(NT::EX)}};
        p[45] = {45, NT::RET_VAL,     {Tm(T::KHALI)}};
        p[46] = {46, NT::AS,          {Tm(T::ID), N(NT::AS_TAIL)}};
        p[47] = {47, NT::AS_TAIL,     {N(NT::VAR_TAIL), Tm(T::MEDALO), N(NT::EX)}};
        p[48] = {48, NT::AS_TAIL,     {Tm(T::LPAREN), N(NT::ARG_LIST), Tm(T::RPAREN)}};
        p[49] = {49, NT::VAR_TAIL,    {Tm(T::DOT), Tm(T::ID)}};
        p[50] = {50, NT::VAR_TAIL,    {}};

        // Group 6: Conditions & Relational Operators (51-59)
        p[51] = {51, NT::COND,        {N(NT::EX), N(NT::COND_TAIL)}};
        p[52] = {52, NT::COND_TAIL,   {N(NT::REL_OP), N(NT::EX)}};
        p[53] = {53, NT::COND_TAIL,   {}};
        p[54] = {54, NT::REL_OP,      {Tm(T::LESS)}};
        p[55] = {55, NT::REL_OP,      {Tm(T::GREATER)}};
        p[56] = {56, NT::REL_OP,      {Tm(T::EQUAL)}};
        p[57] = {57, NT::REL_OP,      {Tm(T::NOT_EQUAL)}};
        p[58] = {58, NT::REL_OP,      {Tm(T::LESS_EQUAL)}};
        p[59] = {59, NT::REL_OP,      {Tm(T::GREATER_EQUAL)}};

        // Group 7: Arithmetic Expressions, Terms, Factors & Function Calls (60-77)
        p[60] = {60, NT::EX,          {N(NT::T), N(NT::EX_PRIME)}};
        p[61] = {61, NT::EX_PRIME,    {Tm(T::PLUS), N(NT::T), N(NT::EX_PRIME)}};
        p[62] = {62, NT::EX_PRIME,    {Tm(T::MINUS), N(NT::T), N(NT::EX_PRIME)}};
        p[63] = {63, NT::EX_PRIME,    {}};
        p[64] = {64, NT::T,           {N(NT::F), N(NT::T_PRIME)}};
        p[65] = {65, NT::T_PRIME,     {Tm(T::STAR), N(NT::F), N(NT::T_PRIME)}};
        p[66] = {66, NT::T_PRIME,     {Tm(T::SLASH), N(NT::F), N(NT::T_PRIME)}};
        p[67] = {67, NT::T_PRIME,     {}};
        p[68] = {68, NT::F,           {Tm(T::ID), N(NT::F_TAIL)}};
        p[69] = {69, NT::F,           {Tm(T::LT)}};
        p[70] = {70, NT::F,           {Tm(T::LPAREN), N(NT::EX), Tm(T::RPAREN)}};
        p[71] = {71, NT::F_TAIL,      {Tm(T::DOT), Tm(T::ID)}};
        p[72] = {72, NT::F_TAIL,      {Tm(T::LPAREN), N(NT::ARG_LIST), Tm(T::RPAREN)}};
        p[73] = {73, NT::F_TAIL,      {}};
        p[74] = {74, NT::ARG_LIST,    {N(NT::EX), N(NT::ARG_TAIL)}};
        p[75] = {75, NT::ARG_LIST,    {}};
        p[76] = {76, NT::ARG_TAIL,    {Tm(T::COMMA), N(NT::EX), N(NT::ARG_TAIL)}};
        p[77] = {77, NT::ARG_TAIL,    {}};

        return p;
    }();
    return kProductions;
}

// ============================================================================
// Exact 39 x 33 LL(1) Parsing Table (186 Populated Cells, 0 Conflicts)
// Matches Specification v1.4, Section 3.3
// ============================================================================

using LL1Table = std::array<std::array<int, kNumTerminals>, kNumNonTerminals>;

const LL1Table& getLL1Table() {
    static const LL1Table kTable = []() {
        using NT = NonTerminal;
        using T = Terminal;

        LL1Table tbl{};
        for (auto& row : tbl) {
            row.fill(0);
        }

        auto setCell = [&](NT nt, T t, int rule) {
            tbl[static_cast<std::size_t>(nt)][static_cast<std::size_t>(t)] = rule;
        };

        auto setCells = [&](NT nt, const std::vector<T>& terms, int rule) {
            for (T t : terms) {
                setCell(nt, t, rule);
            }
        };

        const std::vector<T> kTypeSet = {
            T::INT16, T::INT32, T::UINT16, T::UINT32, T::CHAR
        };
        const std::vector<T> kStSet = {
            T::JABTAK, T::AGAR, T::ID, T::WAPIS, T::SHRO
        };
        const std::vector<T> kRelSet = {
            T::LESS, T::GREATER, T::EQUAL, T::NOT_EQUAL, T::LESS_EQUAL, T::GREATER_EQUAL
        };
        const std::vector<T> kExFollow = {
            T::LESS, T::GREATER, T::EQUAL, T::NOT_EQUAL, T::LESS_EQUAL, T::GREATER_EQUAL,
            T::RPAREN, T::COMMA,
            T::JABTAK, T::AGAR, T::ID, T::WAPIS, T::SHRO, T::KHTM
        };
        const std::vector<T> kExprFirst = {
            T::ID, T::LT, T::LPAREN
        };

        // Row 1: PROG (2 cells)
        setCells(NT::PROG, {T::STRUCT, T::FN}, 1);

        // Row 2: STRUCT_LIST (2 cells)
        setCell(NT::STRUCT_LIST, T::STRUCT, 2);
        setCell(NT::STRUCT_LIST, T::FN, 3);

        // Row 3: STRUCT_DEF (1 cell)
        setCell(NT::STRUCT_DEF, T::STRUCT, 4);

        // Row 4: FIELD_LIST (5 cells)
        setCells(NT::FIELD_LIST, kTypeSet, 5);

        // Row 5: FIELD_TAIL (6 cells)
        setCells(NT::FIELD_TAIL, kTypeSet, 6);
        setCell(NT::FIELD_TAIL, T::KHTM, 7);

        // Row 6: FIELD (5 cells)
        setCells(NT::FIELD, kTypeSet, 8);

        // Row 7: FUNC_LIST (1 cell)
        setCell(NT::FUNC_LIST, T::FN, 9);

        // Row 8: FUNC_TAIL (2 cells)
        setCell(NT::FUNC_TAIL, T::FN, 10);
        setCell(NT::FUNC_TAIL, T::END_OF_INPUT, 11);

        // Row 9: FUNC_DEF (1 cell)
        setCell(NT::FUNC_DEF, T::FN, 12);

        // Row 10: RET_TYPE (6 cells)
        setCells(NT::RET_TYPE, kTypeSet, 13);
        setCell(NT::RET_TYPE, T::KHALI, 14);

        // Row 11: FUNC_NAME (2 cells)
        setCell(NT::FUNC_NAME, T::ID, 15);
        setCell(NT::FUNC_NAME, T::MAIN, 16);

        // Row 12: PARAM_LIST (6 cells)
        setCells(NT::PARAM_LIST, kTypeSet, 17);
        setCell(NT::PARAM_LIST, T::RPAREN, 18);

        // Row 13: PARAM_TAIL (2 cells)
        setCell(NT::PARAM_TAIL, T::COMMA, 19);
        setCell(NT::PARAM_TAIL, T::RPAREN, 20);

        // Row 14: PARAM (5 cells)
        setCells(NT::PARAM, kTypeSet, 21);

        // Row 15: CST (1 cell)
        setCell(NT::CST, T::SHRO, 22);

        // Row 16: DLIST (12 cells)
        setCells(NT::DLIST, kTypeSet, 23);
        setCell(NT::DLIST, T::STRUCT, 23);
        setCells(NT::DLIST, kStSet, 24);
        setCell(NT::DLIST, T::KHTM, 24);

        // Row 17: DS (6 cells)
        setCells(NT::DS, kTypeSet, 25);
        setCell(NT::DS, T::STRUCT, 26);

        // Row 18: TYPE (5 cells)
        setCell(NT::TYPE, T::INT16, 27);
        setCell(NT::TYPE, T::INT32, 28);
        setCell(NT::TYPE, T::UINT16, 29);
        setCell(NT::TYPE, T::UINT32, 30);
        setCell(NT::TYPE, T::CHAR, 31);

        // Row 19: SLIST (6 cells)
        setCells(NT::SLIST, kStSet, 32);
        setCell(NT::SLIST, T::KHTM, 33);

        // Row 20: ST (5 cells)
        setCell(NT::ST, T::JABTAK, 34);
        setCell(NT::ST, T::AGAR, 35);
        setCell(NT::ST, T::ID, 36);
        setCell(NT::ST, T::WAPIS, 37);
        setCell(NT::ST, T::SHRO, 38);

        // Row 21: WS (1 cell)
        setCell(NT::WS, T::JABTAK, 39);

        // Row 22: IF_STMT (1 cell)
        setCell(NT::IF_STMT, T::AGAR, 40);

        // Row 23: ELSE_PART (7 cells)
        setCell(NT::ELSE_PART, T::MAGAR, 41);
        setCells(NT::ELSE_PART, kStSet, 42);
        setCell(NT::ELSE_PART, T::KHTM, 42);

        // Row 24: RET_STMT (1 cell)
        setCell(NT::RET_STMT, T::WAPIS, 43);

        // Row 25: RET_VAL (4 cells)
        setCells(NT::RET_VAL, kExprFirst, 44);
        setCell(NT::RET_VAL, T::KHALI, 45);

        // Row 26: AS (1 cell)
        setCell(NT::AS, T::ID, 46);

        // Row 27: AS_TAIL (3 cells)
        setCells(NT::AS_TAIL, {T::DOT, T::MEDALO}, 47);
        setCell(NT::AS_TAIL, T::LPAREN, 48);

        // Row 28: VAR_TAIL (2 cells)
        setCell(NT::VAR_TAIL, T::DOT, 49);
        setCell(NT::VAR_TAIL, T::MEDALO, 50);

        // Row 29: COND (3 cells)
        setCells(NT::COND, kExprFirst, 51);

        // Row 30: COND_TAIL (7 cells)
        setCells(NT::COND_TAIL, kRelSet, 52);
        setCell(NT::COND_TAIL, T::RPAREN, 53);

        // Row 31: REL_OP (6 cells)
        setCell(NT::REL_OP, T::LESS, 54);
        setCell(NT::REL_OP, T::GREATER, 55);
        setCell(NT::REL_OP, T::EQUAL, 56);
        setCell(NT::REL_OP, T::NOT_EQUAL, 57);
        setCell(NT::REL_OP, T::LESS_EQUAL, 58);
        setCell(NT::REL_OP, T::GREATER_EQUAL, 59);

        // Row 32: EX (3 cells)
        setCells(NT::EX, kExprFirst, 60);

        // Row 33: EX' (16 cells)
        setCell(NT::EX_PRIME, T::PLUS, 61);
        setCell(NT::EX_PRIME, T::MINUS, 62);
        setCells(NT::EX_PRIME, kExFollow, 63);

        // Row 34: T (3 cells)
        setCells(NT::T, kExprFirst, 64);

        // Row 35: T' (18 cells)
        setCell(NT::T_PRIME, T::STAR, 65);
        setCell(NT::T_PRIME, T::SLASH, 66);
        setCells(NT::T_PRIME, {T::PLUS, T::MINUS}, 67);
        setCells(NT::T_PRIME, kExFollow, 67);

        // Row 36: F (3 cells)
        setCell(NT::F, T::ID, 68);
        setCell(NT::F, T::LT, 69);
        setCell(NT::F, T::LPAREN, 70);

        // Row 37: F_TAIL (20 cells)
        setCell(NT::F_TAIL, T::DOT, 71);
        setCell(NT::F_TAIL, T::LPAREN, 72);
        setCells(NT::F_TAIL, {T::STAR, T::SLASH, T::PLUS, T::MINUS}, 73);
        setCells(NT::F_TAIL, kExFollow, 73);

        // Row 38: ARG_LIST (4 cells)
        setCells(NT::ARG_LIST, kExprFirst, 74);
        setCell(NT::ARG_LIST, T::RPAREN, 75);

        // Row 39: ARG_TAIL (2 cells)
        setCell(NT::ARG_TAIL, T::COMMA, 76);
        setCell(NT::ARG_TAIL, T::RPAREN, 77);

        return tbl;
    }();
    return kTable;
}

std::string formatExpectedTerminals(NonTerminal nt) {
    const LL1Table& tbl = getLL1Table();
    const auto& row = tbl[static_cast<std::size_t>(nt)];
    std::ostringstream oss;
    bool first = true;
    for (std::size_t t = 0; t < kNumTerminals; ++t) {
        if (row[t] != 0) {
            if (!first) {
                oss << ", ";
            }
            oss << "'" << terminalName(static_cast<Terminal>(t)) << "'";
            first = false;
        }
    }
    return oss.str();
}

// ============================================================================
// Concrete Parse Tree Representation (Built During LL(1) Stack Loop)
// ============================================================================

struct ParseTreeNode {
    GrammarSymbol sym;
    int ruleId = 0;            // Non-zero for expanded non-terminals
    std::size_t tokenIdx = 0;  // Valid for matched terminal leaves
    std::vector<int> children;
};

// ============================================================================
// Parse Tree -> Left-Associative Pre-Order AST Builder (Section 4.2)
// ============================================================================

struct TempAstNode {
    std::string nodeKind;
    std::string astOp;
    std::string lexeme;
    long long litVal = -1;
    int line = 1;
    std::vector<int> children;
};

class AstBuilder {
public:
    AstBuilder(const std::vector<ParseTreeNode>& ptNodes,
               const std::vector<Token>& tokens)
        : pt_(ptNodes), tokens_(tokens) {}

    std::vector<ASTNode> buildPreOrderAst(int parseRootId) {
        const int tempRootId = buildProg(parseRootId);
        std::vector<ASTNode> result;
        result.reserve(arena_.size());
        flattenPreOrder(tempRootId, -1, -1, result);
        return result;
    }

private:
    const std::vector<ParseTreeNode>& pt_;
    const std::vector<Token>& tokens_;
    std::vector<TempAstNode> arena_;

    int makeNode(const std::string& kind,
                 const std::string& op,
                 const std::string& lex,
                 long long litVal,
                 int line,
                 std::vector<int> children = {}) {
        const int id = static_cast<int>(arena_.size());
        TempAstNode node;
        node.nodeKind = kind;
        node.astOp = op;
        node.lexeme = lex;
        node.litVal = litVal;
        node.line = line;
        node.children = std::move(children);
        arena_.push_back(std::move(node));
        return id;
    }

    const Token& tokOf(int ptId) const {
        return tokens_[pt_[static_cast<std::size_t>(ptId)].tokenIdx];
    }

    int childOf(int ptId, std::size_t idx) const {
        return pt_[static_cast<std::size_t>(ptId)].children[idx];
    }

    int ruleOf(int ptId) const {
        return pt_[static_cast<std::size_t>(ptId)].ruleId;
    }

    const Token& typeTokenOf(int typePtId) const {
        // Rules 27..31: TYPE -> single primitive type terminal
        return tokOf(childOf(typePtId, 0));
    }

    // Rule 1: PROG -> STRUCT_LIST FUNC_LIST
    int buildProg(int ptId) {
        std::vector<int> children;
        collectStructList(childOf(ptId, 0), children);
        collectFuncList(childOf(ptId, 1), children);
        const int firstLine = tokens_.empty() ? 1 : tokens_[0].line;
        return makeNode("PROG", "@NONE", "@NONE", -1, firstLine, std::move(children));
    }

    // Rules 2-3: STRUCT_LIST -> STRUCT_DEF STRUCT_LIST | epsilon
    void collectStructList(int ptId, std::vector<int>& out) {
        int cur = ptId;
        while (ruleOf(cur) == 2) {
            out.push_back(buildStructDef(childOf(cur, 0)));
            cur = childOf(cur, 1);
        }
    }

    // Rule 4: STRUCT_DEF -> 'struct' ID 'shro' FIELD_LIST 'khtm'
    int buildStructDef(int ptId) {
        const Token& structTok = tokOf(childOf(ptId, 0));
        const Token& nameTok = tokOf(childOf(ptId, 1));
        std::vector<int> fields;
        collectFieldList(childOf(ptId, 3), fields);
        return makeNode("STRUCT_DEF", "struct", nameTok.lexeme, -1,
                        structTok.line, std::move(fields));
    }

    // Rules 5-7: FIELD_LIST -> FIELD FIELD_TAIL; FIELD_TAIL -> FIELD FIELD_TAIL | epsilon
    void collectFieldList(int ptId, std::vector<int>& out) {
        out.push_back(buildField(childOf(ptId, 0)));
        int tail = childOf(ptId, 1);
        while (ruleOf(tail) == 6) {
            out.push_back(buildField(childOf(tail, 0)));
            tail = childOf(tail, 1);
        }
    }

    // Rule 8: FIELD -> TYPE ID
    int buildField(int ptId) {
        const Token& typeTok = typeTokenOf(childOf(ptId, 0));
        const Token& idTok = tokOf(childOf(ptId, 1));
        return makeNode("FIELD_DECL", typeTok.lexeme, idTok.lexeme, -1, typeTok.line);
    }

    // Rules 9-11: FUNC_LIST -> FUNC_DEF FUNC_TAIL; FUNC_TAIL -> FUNC_DEF FUNC_TAIL | epsilon
    void collectFuncList(int ptId, std::vector<int>& out) {
        out.push_back(buildFuncDef(childOf(ptId, 0)));
        int tail = childOf(ptId, 1);
        while (ruleOf(tail) == 10) {
            out.push_back(buildFuncDef(childOf(tail, 0)));
            tail = childOf(tail, 1);
        }
    }

    // Rule 12: FUNC_DEF -> 'fn' RET_TYPE FUNC_NAME '(' PARAM_LIST ')' CST
    int buildFuncDef(int ptId) {
        const Token& fnTok = tokOf(childOf(ptId, 0));
        const int retTypePt = childOf(ptId, 1);
        const std::string retType = (ruleOf(retTypePt) == 13)
                                        ? typeTokenOf(childOf(retTypePt, 0)).lexeme
                                        : tokOf(childOf(retTypePt, 0)).lexeme;
        const int funcNamePt = childOf(ptId, 2);
        const Token& nameTok = tokOf(childOf(funcNamePt, 0));

        std::vector<int> children;
        collectParamList(childOf(ptId, 4), children);
        children.push_back(buildBlockCst(childOf(ptId, 6)));

        return makeNode("FUNC_DEF", retType, nameTok.lexeme, -1,
                        fnTok.line, std::move(children));
    }

    // Rules 17-21: PARAM_LIST -> PARAM PARAM_TAIL | epsilon
    void collectParamList(int ptId, std::vector<int>& out) {
        if (ruleOf(ptId) == 18) {
            return;
        }
        out.push_back(buildParam(childOf(ptId, 0)));
        int tail = childOf(ptId, 1);
        while (ruleOf(tail) == 19) {
            // Rule 19: ',' PARAM PARAM_TAIL
            out.push_back(buildParam(childOf(tail, 1)));
            tail = childOf(tail, 2);
        }
    }

    // Rule 21: PARAM -> TYPE ID
    int buildParam(int ptId) {
        const Token& typeTok = typeTokenOf(childOf(ptId, 0));
        const Token& idTok = tokOf(childOf(ptId, 1));
        return makeNode("PARAM_DECL", typeTok.lexeme, idTok.lexeme, -1, typeTok.line);
    }

    // Rule 22: CST -> 'shro' DLIST SLIST 'khtm'
    int buildBlockCst(int ptId) {
        const Token& shroTok = tokOf(childOf(ptId, 0));

        std::vector<int> decls;
        collectDList(childOf(ptId, 1), decls);
        const int declListLine = decls.empty() ? shroTok.line
                                               : arena_[static_cast<std::size_t>(decls[0])].line;
        const int declListNode = makeNode("DECL_LIST", "@NONE", "@NONE", -1,
                                          declListLine, std::move(decls));

        std::vector<int> stmts;
        collectSList(childOf(ptId, 2), stmts);
        const int stmtListLine = stmts.empty() ? shroTok.line
                                               : arena_[static_cast<std::size_t>(stmts[0])].line;
        const int stmtListNode = makeNode("STMT_LIST", "@NONE", "@NONE", -1,
                                          stmtListLine, std::move(stmts));

        return makeNode("BLOCK_CST", "@NONE", "@NONE", -1,
                        shroTok.line, {declListNode, stmtListNode});
    }

    // Rules 23-26: DLIST -> DS DLIST | epsilon
    void collectDList(int ptId, std::vector<int>& out) {
        int cur = ptId;
        while (ruleOf(cur) == 23) {
            const int dsPt = childOf(cur, 0);
            if (ruleOf(dsPt) == 25) {
                // Rule 25: TYPE ID
                const Token& typeTok = typeTokenOf(childOf(dsPt, 0));
                const Token& idTok = tokOf(childOf(dsPt, 1));
                out.push_back(makeNode("VAR_DECL", typeTok.lexeme, idTok.lexeme, -1, typeTok.line));
            } else {
                // Rule 26: 'struct' ID ID
                const Token& structTok = tokOf(childOf(dsPt, 0));
                const Token& typeIdTok = tokOf(childOf(dsPt, 1));
                const Token& varIdTok = tokOf(childOf(dsPt, 2));
                out.push_back(makeNode("STRUCT_VAR_DECL", typeIdTok.lexeme, varIdTok.lexeme, -1,
                                       structTok.line));
            }
            cur = childOf(cur, 1);
        }
    }

    // Rules 32-33: SLIST -> ST SLIST | epsilon
    void collectSList(int ptId, std::vector<int>& out) {
        int cur = ptId;
        while (ruleOf(cur) == 32) {
            out.push_back(buildStmt(childOf(cur, 0)));
            cur = childOf(cur, 1);
        }
    }

    // Rules 34-38: ST -> WS | IF_STMT | AS | RET_STMT | CST
    int buildStmt(int ptId) {
        const int r = ruleOf(ptId);
        const int childPt = childOf(ptId, 0);
        switch (r) {
            case 34: return buildWhileStmt(childPt);
            case 35: return buildIfStmt(childPt);
            case 36: return buildAssignOrCallStmt(childPt);
            case 37: return buildReturnStmt(childPt);
            case 38: return buildBlockCst(childPt);
            default: return -1;
        }
    }

    // Rule 39: WS -> 'jabtak' '(' COND ')' CST
    int buildWhileStmt(int ptId) {
        const Token& jabtakTok = tokOf(childOf(ptId, 0));
        const int condNode = buildCond(childOf(ptId, 2));
        const int bodyNode = buildBlockCst(childOf(ptId, 4));
        return makeNode("WHILE_STMT", "jabtak", "jabtak", -1,
                        jabtakTok.line, {condNode, bodyNode});
    }

    // Rules 40-42: IF_STMT -> 'agar' '(' COND ')' CST ELSE_PART
    int buildIfStmt(int ptId) {
        const Token& agarTok = tokOf(childOf(ptId, 0));
        const int condNode = buildCond(childOf(ptId, 2));
        const int thenNode = buildBlockCst(childOf(ptId, 4));
        const int elsePartPt = childOf(ptId, 5);

        std::vector<int> children = {condNode, thenNode};
        if (ruleOf(elsePartPt) == 41) {
            // Rule 41: 'magar' CST
            children.push_back(buildBlockCst(childOf(elsePartPt, 1)));
        }

        return makeNode("IF_STMT", "agar", "agar", -1,
                        agarTok.line, std::move(children));
    }

    // Rules 43-45: RET_STMT -> 'wapis' RET_VAL
    int buildReturnStmt(int ptId) {
        const Token& wapisTok = tokOf(childOf(ptId, 0));
        const int retValPt = childOf(ptId, 1);
        int payloadNode = -1;
        if (ruleOf(retValPt) == 44) {
            payloadNode = buildExpr(childOf(retValPt, 0));
        } else {
            // Rule 45: 'khali'
            const Token& khaliTok = tokOf(childOf(retValPt, 0));
            payloadNode = makeNode("VOID_KHALI", "khali", "khali", -1, khaliTok.line);
        }
        return makeNode("RETURN_STMT", "wapis", "wapis", -1,
                        wapisTok.line, {payloadNode});
    }

    // Rules 46-50: AS -> ID AS_TAIL
    int buildAssignOrCallStmt(int ptId) {
        const Token& idTok = tokOf(childOf(ptId, 0));
        const int asTailPt = childOf(ptId, 1);

        if (ruleOf(asTailPt) == 47) {
            // Rule 47: VAR_TAIL 'medalo' EX
            const int varTailPt = childOf(asTailPt, 0);
            const Token& medaloTok = tokOf(childOf(asTailPt, 1));
            const int rhsExpr = buildExpr(childOf(asTailPt, 2));

            int lhsTarget = makeNode("VAR_REF", "@NONE", idTok.lexeme, -1, idTok.line);
            if (ruleOf(varTailPt) == 49) {
                // Rule 49: '.' ID
                const Token& dotTok = tokOf(childOf(varTailPt, 0));
                const Token& fieldTok = tokOf(childOf(varTailPt, 1));
                lhsTarget = makeNode("FIELD_ACCESS", ".", fieldTok.lexeme, -1,
                                     dotTok.line, {lhsTarget});
            }

            return makeNode("ASSIGN_STMT", "medalo", "medalo", -1,
                            medaloTok.line, {lhsTarget, rhsExpr});
        }

        // Rule 48: '(' ARG_LIST ')'
        std::vector<int> args;
        collectArgList(childOf(asTailPt, 1), args);
        return makeNode("CALL_STMT", "call", idTok.lexeme, -1,
                        idTok.line, std::move(args));
    }

    // Rules 51-59: COND -> EX COND_TAIL
    int buildCond(int ptId) {
        const int leftExpr = buildExpr(childOf(ptId, 0));
        const int condTailPt = childOf(ptId, 1);
        if (ruleOf(condTailPt) == 53) {
            return leftExpr;
        }
        // Rule 52: REL_OP EX
        const int relOpPt = childOf(condTailPt, 0);
        const Token& opTok = tokOf(childOf(relOpPt, 0));
        const int rightExpr = buildExpr(childOf(condTailPt, 1));
        return makeNode("REL_EXPR", opTok.lexeme, opTok.lexeme, -1,
                        opTok.line, {leftExpr, rightExpr});
    }

    // Rules 60-63: EX -> T EX' (left-associative iterative fold: a - b - c => (a - b) - c)
    int buildExpr(int ptId) {
        int acc = buildTerm(childOf(ptId, 0));
        int cur = childOf(ptId, 1);
        while (ruleOf(cur) == 61 || ruleOf(cur) == 62) {
            const Token& opTok = tokOf(childOf(cur, 0));
            const int rhs = buildTerm(childOf(cur, 1));
            acc = makeNode("BIN_EXPR", opTok.lexeme, opTok.lexeme, -1,
                           opTok.line, {acc, rhs});
            cur = childOf(cur, 2);
        }
        return acc;
    }

    // Rules 64-67: T -> F T' (left-associative iterative fold: a / b * c => (a / b) * c)
    int buildTerm(int ptId) {
        int acc = buildFactor(childOf(ptId, 0));
        int cur = childOf(ptId, 1);
        while (ruleOf(cur) == 65 || ruleOf(cur) == 66) {
            const Token& opTok = tokOf(childOf(cur, 0));
            const int rhs = buildFactor(childOf(cur, 1));
            acc = makeNode("BIN_EXPR", opTok.lexeme, opTok.lexeme, -1,
                           opTok.line, {acc, rhs});
            cur = childOf(cur, 2);
        }
        return acc;
    }

    // Rules 68-73: F -> ID F_TAIL | LT | '(' EX ')'
    int buildFactor(int ptId) {
        const int r = ruleOf(ptId);
        if (r == 68) {
            const Token& idTok = tokOf(childOf(ptId, 0));
            const int fTailPt = childOf(ptId, 1);
            const int tailRule = ruleOf(fTailPt);

            if (tailRule == 71) {
                // Rule 71: '.' ID
                const Token& dotTok = tokOf(childOf(fTailPt, 0));
                const Token& fieldTok = tokOf(childOf(fTailPt, 1));
                const int baseRef = makeNode("VAR_REF", "@NONE", idTok.lexeme, -1, idTok.line);
                return makeNode("FIELD_ACCESS", ".", fieldTok.lexeme, -1,
                                dotTok.line, {baseRef});
            }
            if (tailRule == 72) {
                // Rule 72: '(' ARG_LIST ')'
                std::vector<int> args;
                collectArgList(childOf(fTailPt, 1), args);
                return makeNode("FUNC_CALL_EXPR", "call", idTok.lexeme, -1,
                                idTok.line, std::move(args));
            }
            // Rule 73: epsilon
            return makeNode("VAR_REF", "@NONE", idTok.lexeme, -1, idTok.line);
        }

        if (r == 69) {
            const Token& litTok = tokOf(childOf(ptId, 0));
            if (litTok.category == "INT_LITERAL") {
                return makeNode("INT_LIT", "@NONE", litTok.lexeme, litTok.parsedIntVal, litTok.line);
            }
            return makeNode("CHAR_LIT", "@NONE", litTok.lexeme, litTok.parsedIntVal, litTok.line);
        }

        // Rule 70: '(' EX ')'
        return buildExpr(childOf(ptId, 1));
    }

    // Rules 74-77: ARG_LIST -> EX ARG_TAIL | epsilon
    void collectArgList(int ptId, std::vector<int>& out) {
        if (ruleOf(ptId) == 75) {
            return;
        }
        out.push_back(buildExpr(childOf(ptId, 0)));
        int tail = childOf(ptId, 1);
        while (ruleOf(tail) == 76) {
            // Rule 76: ',' EX ARG_TAIL
            out.push_back(buildExpr(childOf(tail, 1)));
            tail = childOf(tail, 2);
        }
    }

    // Top-down, left-to-right pre-order flattening into final ASTNode vector
    void flattenPreOrder(int tempId,
                         int parentNodeId,
                         int childIdx,
                         std::vector<ASTNode>& out) {
        const TempAstNode& src = arena_[static_cast<std::size_t>(tempId)];
        const int myId = static_cast<int>(out.size());

        ASTNode node;
        node.nodeId = myId;
        node.parentId = parentNodeId;
        node.childIndex = childIdx;
        node.nodeKind = src.nodeKind;
        node.astOp = src.astOp;
        node.lexeme = src.lexeme;
        node.litVal = src.litVal;
        node.line = src.line;

        out.push_back(std::move(node));

        if (parentNodeId != -1) {
            out[static_cast<std::size_t>(parentNodeId)].childIds.push_back(myId);
        }

        for (std::size_t i = 0; i < src.children.size(); ++i) {
            flattenPreOrder(src.children[i], myId, static_cast<int>(i), out);
        }
    }
};

} // namespace

// ============================================================================
// Parser Public Methods
// ============================================================================

const std::vector<ASTNode>& Parser::getAstNodes() const {
    return astNodes_;
}

const std::string& Parser::getSourceFilename() const {
    return sourceFilename_;
}

bool Parser::hasError() const {
    return hasError_;
}

const std::string& Parser::getErrorCode() const {
    return errorCode_;
}

const std::string& Parser::getErrorMessage() const {
    return errorMessage_;
}

int Parser::getErrorLine() const {
    return errorLine_;
}

int Parser::getErrorCol() const {
    return errorCol_;
}

std::size_t Parser::getProductionCount() {
    return kNumProductions;
}

std::size_t Parser::getNonTerminalCount() {
    return kNumNonTerminals;
}

std::size_t Parser::getTerminalCount() {
    return kNumTerminals;
}

std::size_t Parser::getPopulatedTableCellCount() {
    const LL1Table& tbl = getLL1Table();
    std::size_t count = 0;
    for (const auto& row : tbl) {
        for (int rule : row) {
            if (rule != 0) {
                ++count;
            }
        }
    }
    return count;
}

int Parser::getTableRule(const std::string& nonTerminal, const std::string& terminal) {
    Terminal t = Terminal::END_OF_INPUT;
    if (!mapStringToTerminal(terminal, t)) {
        return -1;
    }
    for (std::size_t nt = 0; nt < kNumNonTerminals; ++nt) {
        if (nonTerminal == nonTerminalName(static_cast<NonTerminal>(nt))) {
            return getLL1Table()[nt][static_cast<std::size_t>(t)];
        }
    }
    return -1;
}

bool Parser::reportError(const std::string& code,
                         int line,
                         int col,
                         const std::string& detail) {
    astNodes_.clear();
    hasError_ = true;
    errorCode_ = code;
    errorLine_ = line;
    errorCol_ = col;

    std::ostringstream oss;
    if (!code.empty()) {
        oss << code;
    }
    if (!sourceFilename_.empty()) {
        oss << " in " << sourceFilename_;
    }
    if (line > 0) {
        oss << " at Line " << line;
        if (col > 0) {
            oss << ", Col " << col;
        }
    }
    oss << ": " << detail;
    errorMessage_ = oss.str();

    return false;
}

bool Parser::parseFromLexFile(const std::string& inputLexPath) {
    astNodes_.clear();
    sourceFilename_.clear();
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;
    errorCol_ = 0;

    LexReader reader;
    if (!reader.read(inputLexPath)) {
        hasError_ = true;
        errorCode_ = reader.getErrorCode();
        errorMessage_ = reader.getErrorMessage();
        errorLine_ = reader.getErrorLine();
        errorCol_ = 0;
        return false;
    }

    return parseTokens(reader.getTokens(), reader.getSourceFilename());
}

bool Parser::parseTokens(const std::vector<Token>& tokens,
                         const std::string& sourceFilename) {
    astNodes_.clear();
    sourceFilename_ = sourceFilename;
    hasError_ = false;
    errorCode_.clear();
    errorMessage_.clear();
    errorLine_ = 0;
    errorCol_ = 0;

    if (tokens.empty()) {
        return reportError("SYNTAX_ERROR", 1, 1,
                           "Empty token stream; expected at least one function definition");
    }

    // Token-stream preconditions. LexReader already guarantees these for the
    // file-based entry point; they are re-checked here so that parseTokens()
    // can never build an AST from an inconsistent token vector.
    for (const Token& tok : tokens) {
        if (tok.terminal == "LT" &&
            tok.category != "INT_LITERAL" && tok.category != "CHAR_LITERAL") {
            return reportError("FATAL_IO_INVARIANT_VIOLATION", tok.line, tok.col,
                               "LT token must have category INT_LITERAL or CHAR_LITERAL, found '" +
                               tok.category + "'");
        }
        if (tok.terminal == "ID" && !ast_schema::isCituIdentifier(tok.lexeme)) {
            return reportError("FATAL_IO_INVARIANT_VIOLATION", tok.line, tok.col,
                               "ID token lexeme '" + tok.lexeme + "' is not a valid Citu identifier");
        }
    }

    const std::vector<Production>& productions = getProductions();
    const LL1Table& table = getLL1Table();

    // Build concrete parse tree via table-driven LL(1) stack algorithm
    struct StackEntry {
        GrammarSymbol sym;
        int parseNodeId;
    };

    std::vector<ParseTreeNode> parseTree;
    parseTree.reserve(tokens.size() * 4);

    ParseTreeNode rootNode;
    rootNode.sym = GrammarSymbol::nonTerm(NonTerminal::PROG);
    parseTree.push_back(rootNode);

    std::vector<StackEntry> parseStack;
    parseStack.push_back({GrammarSymbol::term(Terminal::END_OF_INPUT), -1});
    parseStack.push_back({GrammarSymbol::nonTerm(NonTerminal::PROG), 0});

    std::size_t cursor = 0;

    while (!parseStack.empty()) {
        if (cursor >= tokens.size()) {
            const Token& last = tokens.back();
            return reportError("SYNTAX_ERROR", last.line, last.col,
                               "Unexpected end of token stream before completing parse");
        }

        const Token& currentTok = tokens[cursor];
        Terminal lookahead = Terminal::END_OF_INPUT;
        if (!mapStringToTerminal(currentTok.terminal, lookahead)) {
            return reportError("SYNTAX_ERROR", currentTok.line, currentTok.col,
                               "Unrecognized terminal '" + currentTok.terminal +
                               "' (lexeme '" + currentTok.lexeme + "')");
        }

        const StackEntry top = parseStack.back();
        parseStack.pop_back();

        if (top.sym.isTerminal) {
            const Terminal expectedTerm = static_cast<Terminal>(top.sym.id);
            if (expectedTerm == lookahead) {
                if (top.parseNodeId != -1) {
                    parseTree[static_cast<std::size_t>(top.parseNodeId)].tokenIdx = cursor;
                }
                ++cursor;
            } else {
                return reportError("SYNTAX_ERROR", currentTok.line, currentTok.col,
                                   std::string("Expected '") + terminalName(expectedTerm) +
                                   "', found '" + currentTok.lexeme + "' (" + currentTok.terminal + ")");
            }
        } else {
            const NonTerminal nt = static_cast<NonTerminal>(top.sym.id);
            const int ruleNum = table[static_cast<std::size_t>(nt)][static_cast<std::size_t>(lookahead)];

            if (ruleNum == 0) {
                return reportError("SYNTAX_ERROR", currentTok.line, currentTok.col,
                                   std::string("Unexpected '") + currentTok.lexeme + "' (" +
                                   currentTok.terminal + ") while parsing " + nonTerminalName(nt) +
                                   "; expected one of: " + formatExpectedTerminals(nt));
            }

            const Production& prod = productions[static_cast<std::size_t>(ruleNum)];
            parseTree[static_cast<std::size_t>(top.parseNodeId)].ruleId = ruleNum;

            // Create child parse-tree nodes left-to-right
            std::vector<int> childIds;
            childIds.reserve(prod.rhs.size());
            for (const GrammarSymbol& rhsSym : prod.rhs) {
                const int childId = static_cast<int>(parseTree.size());
                ParseTreeNode childNode;
                childNode.sym = rhsSym;
                parseTree.push_back(childNode);
                childIds.push_back(childId);
            }

            parseTree[static_cast<std::size_t>(top.parseNodeId)].children = childIds;

            // Push RHS symbols onto the LL(1) stack in reverse (right-to-left) order
            for (std::size_t i = prod.rhs.size(); i > 0; --i) {
                parseStack.push_back({prod.rhs[i - 1], childIds[i - 1]});
            }
        }
    }

    if (cursor != tokens.size()) {
        const Token& extraTok = tokens[cursor];
        return reportError("SYNTAX_ERROR", extraTok.line, extraTok.col,
                           "Unexpected trailing token '" + extraTok.lexeme + "' after end of program");
    }

    // Fold concrete parse tree into the clean left-associative pre-order AST (Section 4.2)
    AstBuilder builder(parseTree, tokens);
    astNodes_ = builder.buildPreOrderAst(0);
    return true;
}