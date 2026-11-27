# ICC (ITU Compiler Collection) — Phases 2-3 build
#
#   make              build bin/icc (the CLI)
#   make tests        build all unit-test executables into build/
#   make test         build and run all unit-test suites
#   make integration  run bin/icc on the samples and verify the .lex/.ast/.sym it writes
#   make check        test + integration
#   make clean        remove build/ and generated binaries
#
# Works with GNU make from Git Bash / MSYS (`make`) and with MinGW's
# `mingw32-make` when a POSIX sh is on PATH. Run from the repository root.

CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2
CPPFLAGS := -Iinclude -MMD -MP

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

BUILD   := build
OBJDIR  := $(BUILD)/obj
BINDIR  := bin
ICC     := $(BINDIR)/icc$(EXE)
OUTDIR  := $(BUILD)/integration

# Library modules (everything except the CLI entry point)
#   Phase 2: lexer, .lex I/O, LL(1) parser, .ast I/O
#   Phase 3 Stage 3A: symbol-table schema and .sym I/O
#   Phase 3 Stage 3B: semantic analyzer
LIB_SRCS := src/lexer.cpp src/lex_writer.cpp src/lex_reader.cpp \
            src/ast_schema.cpp src/parser.cpp src/ast_writer.cpp src/ast_reader.cpp \
            src/sym_schema.cpp src/sym_writer.cpp src/sym_reader.cpp \
            src/semantic_analyzer.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(OBJDIR)/%.o)
MAIN_OBJ := $(OBJDIR)/main.o

UNIT_TESTS := test_lexer test_lex_writer test_lex_reader test_parser \
              test_sym_writer test_sym_reader test_semantic
UNIT_BINS  := $(UNIT_TESTS:%=$(BUILD)/%$(EXE))
INTEG_BIN  := $(BUILD)/test_integration$(EXE)

SAMPLES  := examples/prog1_geometry.citu examples/prog2_payroll.citu examples/prog3_grades.citu
FIXTURES := tests/fixtures/min_return.citu tests/fixtures/computeHalf.citu
MULTI    := tests/fixtures/multi_lib.citu tests/fixtures/multi_app.citu
NEGDIR   := $(OUTDIR)/negative
STOPDIR  := $(OUTDIR)/stop_after

.PHONY: all tests test integration check clean

# Keep test object files (otherwise make deletes them as intermediates)
.SECONDARY: $(UNIT_TESTS:%=$(OBJDIR)/%.o) $(OBJDIR)/test_integration.o

all: $(ICC)

$(ICC): $(LIB_OBJS) $(MAIN_OBJ) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/test_%.o: tests/test_%.cpp | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_%$(EXE): $(OBJDIR)/test_%.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

tests: $(UNIT_BINS) $(INTEG_BIN)

# Unit suites read examples/ and tests/ relative to the repo root; Phase 2/3A suites
# write scratch files under bin/, test_semantic under build/test_semantic/.
test: $(UNIT_BINS) | $(BINDIR)
	@status=0; for t in $(UNIT_BINS); do echo ""; echo ">>> $$t"; ./$$t || status=1; done; \
	echo ""; if [ $$status -eq 0 ]; then echo "ALL UNIT SUITES PASSED"; else echo "UNIT SUITE FAILURES"; fi; \
	exit $$status

integration: $(ICC) $(INTEG_BIN)
	@rm -rf $(OUTDIR) && mkdir -p $(OUTDIR) $(NEGDIR) $(STOPDIR)
	@echo ">>> icc: one invocation per independent program"
	@for f in $(SAMPLES) $(FIXTURES); do ./$(ICC) -d $(OUTDIR) $$f || exit 1; done
	@echo ">>> icc: one two-file program (multi_app imports multi_lib.sym)"
	./$(ICC) -d $(OUTDIR) $(MULTI)
	./$(INTEG_BIN) $(OUTDIR)
	@echo ""; echo ">>> CLI negative checks"
	@./$(ICC) -q -d $(NEGDIR) tests/fixtures/bad_lexical.citu; s=$$?; \
	  if [ $$s -eq 1 ] && [ ! -f $(NEGDIR)/bad_lexical.lex ]; then echo "  OK: lexical error -> exit 1, no .lex"; \
	  else echo "  FAIL: bad_lexical exit=$$s"; exit 1; fi
	@./$(ICC) -q -d $(NEGDIR) tests/fixtures/bad_syntax.citu; s=$$?; \
	  if [ $$s -eq 1 ] && [ -f $(NEGDIR)/bad_syntax.lex ] && [ ! -f $(NEGDIR)/bad_syntax.ast ] && \
	  [ ! -f $(NEGDIR)/bad_syntax.sym ]; then echo "  OK: syntax error -> exit 1, .lex kept, no .ast/.sym"; \
	  else echo "  FAIL: bad_syntax exit=$$s"; exit 1; fi
	@./$(ICC) -q -d $(NEGDIR) tests/fixtures/bad_semantic.citu 2>$(NEGDIR)/sema1.err; s=$$?; \
	  if [ $$s -eq 1 ] && grep -q "SEMANTIC_ERROR_UNDECLARED_IDENTIFIER in bad_semantic.citu at Line 3" $(NEGDIR)/sema1.err && \
	  [ -f $(NEGDIR)/bad_semantic.lex ] && [ -f $(NEGDIR)/bad_semantic.ast ] && [ ! -f $(NEGDIR)/bad_semantic.sym ] && \
	  [ ! -f $(NEGDIR)/bad_semantic.sym.tmp ]; then echo "  OK: semantic error -> exit 1, diagnostic printed, .lex/.ast kept, no .sym"; \
	  else echo "  FAIL: bad_semantic exit=$$s"; cat $(NEGDIR)/sema1.err; exit 1; fi
	@printf "PRE-EXISTING SYM\n" > $(NEGDIR)/bad_semantic.sym; \
	  ./$(ICC) -q -d $(NEGDIR) tests/fixtures/bad_semantic.citu 2>$(NEGDIR)/sema2.err; s=$$?; \
	  if [ $$s -eq 1 ] && [ "$$(cat $(NEGDIR)/bad_semantic.sym)" = "PRE-EXISTING SYM" ] && \
	  grep -q "left unchanged" $(NEGDIR)/sema2.err && [ ! -f $(NEGDIR)/bad_semantic.sym.tmp ]; then \
	  echo "  OK: semantic error keeps a pre-existing .sym untouched and says so"; \
	  else echo "  FAIL: pre-existing .sym exit=$$s"; exit 1; fi
	@./$(ICC) -q -d $(NEGDIR) tests/fixtures/bad_semantic.citu tests/fixtures/multi_lib.citu 2>$(NEGDIR)/sema3.err; s=$$?; \
	  if [ $$s -eq 1 ] && grep -q "was not compiled" $(NEGDIR)/sema3.err && [ ! -f $(NEGDIR)/multi_lib.sym ]; then \
	  echo "  OK: a failed unit stops the later units of the same program"; \
	  else echo "  FAIL: multi-file chain exit=$$s"; exit 1; fi
	@./$(ICC) -q --stop-after parse -d $(STOPDIR) tests/fixtures/min_return.citu; s=$$?; \
	  if [ $$s -eq 0 ] && [ -f $(STOPDIR)/min_return.ast ] && [ ! -f $(STOPDIR)/min_return.sym ]; then \
	  echo "  OK: --stop-after parse -> .lex/.ast only (Phase 2 behaviour)"; else echo "  FAIL: stop-after parse exit=$$s"; exit 1; fi
	@./$(ICC) -q --stop-after codegen tests/fixtures/min_return.citu 2>/dev/null; s=$$?; \
	  if [ $$s -eq 2 ]; then echo "  OK: unknown stage -> exit 2"; else echo "  FAIL: unknown stage exit=$$s"; exit 1; fi
	@./$(ICC) -q 2>/dev/null; s=$$?; \
	  if [ $$s -eq 2 ]; then echo "  OK: no input -> exit 2"; else echo "  FAIL: no-input exit=$$s"; exit 1; fi
	@./$(ICC) -q examples/../README.md 2>/dev/null; s=$$?; \
	  if [ $$s -eq 2 ]; then echo "  OK: non-.citu input refused -> exit 2"; else echo "  FAIL: non-citu exit=$$s"; exit 1; fi
	@echo ""; echo "INTEGRATION PASSED"

check: test integration

$(BINDIR) $(OBJDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(ICC) $(BINDIR)/*.o $(BINDIR)/test_*

-include $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(UNIT_TESTS:%=$(OBJDIR)/%.d) $(OBJDIR)/test_integration.d
