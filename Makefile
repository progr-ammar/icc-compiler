# ICC (ITU Compiler Collection) — Phase 2 + Phase 3 Stage 3A build
#
#   make              build bin/icc (the Phase 2 CLI)
#   make tests        build all unit-test executables into build/
#   make test         build and run all unit-test suites
#   make integration  run bin/icc on the samples and verify the files it writes
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
LIB_SRCS := src/lexer.cpp src/lex_writer.cpp src/lex_reader.cpp \
            src/ast_schema.cpp src/parser.cpp src/ast_writer.cpp src/ast_reader.cpp \
            src/sym_schema.cpp src/sym_writer.cpp src/sym_reader.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(OBJDIR)/%.o)
MAIN_OBJ := $(OBJDIR)/main.o

UNIT_TESTS := test_lexer test_lex_writer test_lex_reader test_parser \
              test_sym_writer test_sym_reader
UNIT_BINS  := $(UNIT_TESTS:%=$(BUILD)/%$(EXE))
INTEG_BIN  := $(BUILD)/test_integration$(EXE)

SAMPLES  := examples/prog1_geometry.citu examples/prog2_payroll.citu examples/prog3_grades.citu
FIXTURES := tests/fixtures/min_return.citu tests/fixtures/computeHalf.citu

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

# Unit suites write scratch files under bin/ and read examples/ relative to the repo root.
test: $(UNIT_BINS) | $(BINDIR)
	@status=0; for t in $(UNIT_BINS); do echo ""; echo ">>> $$t"; ./$$t || status=1; done; \
	echo ""; if [ $$status -eq 0 ]; then echo "ALL UNIT SUITES PASSED"; else echo "UNIT SUITE FAILURES"; fi; \
	exit $$status

integration: $(ICC) $(INTEG_BIN)
	@rm -rf $(OUTDIR) && mkdir -p $(OUTDIR)
	./$(ICC) -d $(OUTDIR) $(SAMPLES) $(FIXTURES)
	./$(INTEG_BIN) $(OUTDIR)
	@echo ""; echo ">>> CLI negative checks"
	@./$(ICC) -q -d $(OUTDIR) tests/fixtures/bad_lexical.citu; s=$$?; \
	  if [ $$s -eq 1 ] && [ ! -f $(OUTDIR)/bad_lexical.lex ]; then echo "  OK: lexical error -> exit 1, no .lex"; \
	  else echo "  FAIL: bad_lexical exit=$$s"; exit 1; fi
	@./$(ICC) -q -d $(OUTDIR) tests/fixtures/bad_syntax.citu; s=$$?; \
	  if [ $$s -eq 1 ] && [ -f $(OUTDIR)/bad_syntax.lex ] && [ ! -f $(OUTDIR)/bad_syntax.ast ]; then \
	  echo "  OK: syntax error -> exit 1, .lex kept, no .ast"; else echo "  FAIL: bad_syntax exit=$$s"; exit 1; fi
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
