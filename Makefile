
# ============================================================================
# Wingo — P2P Internet Sharing Tool (Repo: Bowie)
# Copyright (C) 2024 ASBM Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# ============================================================================

# ============================================================================
# PROJECT INFO
# ============================================================================

PROJECT_NAME    := bowie
PROJECT_VERSION := 0.1.0

# ============================================================================
# COMPILER & TOOLS
# ============================================================================

CC      := gcc
AR      := ar
RANLIB  := ranlib
MKDIR   := mkdir -p
RM      := rm -f
RMDIR   := rm -rf
INSTALL := install

# ============================================================================
# DIRECTORIES
# ============================================================================

# Core directories
CORE_DIR        := core
INCLUDE_DIR     := $(CORE_DIR)/include
SRC_DIR         := $(CORE_DIR)/src
TEST_DIR        := $(CORE_DIR)/tests

# Build directories
BUILD_DIR       := build
OBJ_DIR         := $(BUILD_DIR)/obj
BIN_DIR         := $(BUILD_DIR)/bin
LIB_DIR         := $(BUILD_DIR)/lib
TEST_BIN_DIR    := $(BUILD_DIR)/test

# ============================================================================
# TARGETS
# ============================================================================

# Main library
LIB_NAME        := libbowie.a
LIB_TARGET      := $(LIB_DIR)/$(LIB_NAME)

# Test targets
TEST_BUFFER     := $(TEST_BIN_DIR)/test_buffer
TEST_LIST       := $(TEST_BIN_DIR)/test_list
TEST_QUEUE      := $(TEST_BIN_DIR)/test_queue
TEST_HASHMAP    := $(TEST_BIN_DIR)/test_hashmap

TEST_TARGETS    := $(TEST_BUFFER) $(TEST_LIST) $(TEST_QUEUE) $(TEST_HASHMAP)

# ============================================================================
# COMPILER FLAGS
# ============================================================================

# C standard
CSTD := -std=c11

# Warning flags (strict)
WARNINGS := \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Wshadow \
    -Wpointer-arith \
    -Wcast-align \
    -Wwrite-strings \
    -Wmissing-prototypes \
    -Wmissing-declarations \
    -Wstrict-prototypes \
    -Wold-style-definition \
    -Wredundant-decls \
    -Wnested-externs \
    -Wno-unused-parameter \
    -Wno-unused-function

# Optimization
OPT := -O2

# Debug info
DEBUG := -g

# Feature test macros
DEFINES := \
    -D_GNU_SOURCE \
    -D_POSIX_C_SOURCE=200809L

# Include paths
INCLUDES := \
    -I$(INCLUDE_DIR)

# Base CFLAGS
CFLAGS := \
    $(CSTD) \
    $(WARNINGS) \
    $(OPT) \
    $(DEBUG) \
    $(DEFINES) \
    $(INCLUDES) \
    -fPIC \
    -pthread

# Linker flags
LDFLAGS := \
    -pthread

# Libraries
LIBS := \
    -lssl \
    -lcrypto \
    -lpthread

# Test libraries (Check Framework)
TEST_LIBS := \
    -lcheck \
    -lpthread \
    -lrt \
    -lm \
    $(LIBS)

# ============================================================================
# SOURCE FILES
# ============================================================================

# Core sources
CORE_SRCS := \
    $(SRC_DIR)/error.c \
    $(SRC_DIR)/log.c \
    $(SRC_DIR)/util/buffer.c \
    $(SRC_DIR)/util/list.c \
    $(SRC_DIR)/util/queue.c \
    $(SRC_DIR)/util/hashmap.c \
    $(SRC_DIR)/util/time.c \
    $(SRC_DIR)/util/random.c

# Object files (mirror source structure under OBJ_DIR)
CORE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))

# ============================================================================
# TEST SOURCES
# ============================================================================

TEST_BUFFER_SRCS    := $(TEST_DIR)/unit/test_buffer.c
TEST_LIST_SRCS      := $(TEST_DIR)/unit/test_list.c
TEST_QUEUE_SRCS     := $(TEST_DIR)/unit/test_queue.c
TEST_HASHMAP_SRCS   := $(TEST_DIR)/unit/test_hashmap.c

# ============================================================================
# DEFAULT TARGET
# ============================================================================

.PHONY: all
all: $(LIB_TARGET)

# ============================================================================
# LIBRARY BUILD
# ============================================================================

$(LIB_TARGET): $(CORE_OBJS) | $(LIB_DIR)
	@echo "  AR      $@"
	@$(AR) rcs $@ $(CORE_OBJS)
	@$(RANLIB) $@
	@echo "  ✓ Library built: $@"

# Compile rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@$(MKDIR) $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# TEST BUILD
# ============================================================================

.PHONY: tests
tests: $(TEST_TARGETS)

$(TEST_BUFFER): $(TEST_BUFFER_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@
	@echo "  ✓ Test built: $@"

$(TEST_LIST): $(TEST_LIST_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@
	@echo "  ✓ Test built: $@"

$(TEST_QUEUE): $(TEST_QUEUE_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@
	@echo "  ✓ Test built: $@"

$(TEST_HASHMAP): $(TEST_HASHMAP_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@
	@echo "  ✓ Test built: $@"

# ============================================================================
# RUN TESTS
# ============================================================================

.PHONY: test
test: tests
	@echo ""
	@echo "=========================================="
	@echo "  Running Tests"
	@echo "=========================================="
	@echo ""
	@$(TEST_BUFFER)
	@echo ""
	@$(TEST_LIST)
	@echo ""
	@$(TEST_QUEUE)
	@echo ""
	@$(TEST_HASHMAP)
	@echo ""
	@echo "=========================================="
	@echo "  All tests passed!"
	@echo "=========================================="

.PHONY: test-verbose
test-verbose: tests
	@echo "Running tests (verbose)..."
	@CK_VERBOSITY=verbose $(TEST_BUFFER)
	@CK_VERBOSITY=verbose $(TEST_LIST)
	@CK_VERBOSITY=verbose $(TEST_QUEUE)
	@CK_VERBOSITY=verbose $(TEST_HASHMAP)

.PHONY: test-buffer
test-buffer: $(TEST_BUFFER)
	@$(TEST_BUFFER)

.PHONY: test-list
test-list: $(TEST_LIST)
	@$(TEST_LIST)

.PHONY: test-queue
test-queue: $(TEST_QUEUE)
	@$(TEST_QUEUE)

.PHONY: test-hashmap
test-hashmap: $(TEST_HASHMAP)
	@$(TEST_HASHMAP)

# ============================================================================
# MEMORY CHECK (VALGRIND)
# ============================================================================

.PHONY: valgrind
valgrind: tests
	@echo "Running tests with Valgrind..."
	@valgrind --leak-check=full \
	          --show-leak-kinds=all \
	          --track-origins=yes \
	          --error-exitcode=1 \
	          $(TEST_BUFFER)
	@valgrind --leak-check=full \
	          --show-leak-kinds=all \
	          --track-origins=yes \
	          --error-exitcode=1 \
	          $(TEST_LIST)
	@valgrind --leak-check=full \
	          --show-leak-kinds=all \
	          --track-origins=yes \
	          --error-exitcode=1 \
	          $(TEST_QUEUE)
	@valgrind --leak-check=full \
	          --show-leak-kinds=all \
	          --track-origins=yes \
	          --error-exitcode=1 \
	          $(TEST_HASHMAP)

# ============================================================================
# SANITIZER BUILD
# ============================================================================

.PHONY: sanitize
sanitize: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all tests
	@echo "Built with sanitizers (ASan + UBSan)"

.PHONY: sanitize-test
sanitize-test: sanitize
	@echo "Running sanitized tests..."
	@$(TEST_BUFFER)
	@$(TEST_LIST)
	@$(TEST_QUEUE)
	@$(TEST_HASHMAP)

# ============================================================================
# CODE COVERAGE
# ============================================================================

.PHONY: coverage
coverage: CFLAGS += --coverage -fprofile-arcs -ftest-coverage
coverage: LDFLAGS += --coverage
coverage: clean all tests
	@echo "Running coverage tests..."
	@$(TEST_BUFFER)
	@$(TEST_LIST)
	@$(TEST_QUEUE)
	@$(TEST_HASHMAP)
	@echo ""
	@echo "Generating coverage report..."
	@gcov -r $(OBJ_DIR)/*.gcno 2>/dev/null || true
	@echo "Coverage data generated in $(OBJ_DIR)/"

# ============================================================================
# STATIC ANALYSIS
# ============================================================================

.PHONY: analyze
analyze:
	@echo "Running static analysis with clang-tidy..."
	@for src in $(CORE_SRCS); do \
	    clang-tidy $$src -- $(CFLAGS) 2>/dev/null || true; \
	done

.PHONY: cppcheck
cppcheck:
	@echo "Running cppcheck..."
	@cppcheck --enable=all \
	          --std=c11 \
	          --suppress=missingIncludeSystem \
	          --inline-suppr \
	          $(CORE_SRCS)

# ============================================================================
# FORMAT
# ============================================================================

.PHONY: format
format:
	@echo "Formatting source files..."
	@for src in $(CORE_SRCS); do \
	    clang-format -i $$src; \
	done
	@echo "Done."

.PHONY: format-check
format-check:
	@echo "Checking format..."
	@for src in $(CORE_SRCS); do \
	    clang-format --dry-run --Werror $$src || exit 1; \
	done
	@echo "All files formatted correctly."

# ============================================================================
# CLEAN
# ============================================================================

.PHONY: clean
clean:
	@echo "  CLEAN   $(BUILD_DIR)"
	@$(RMDIR) $(BUILD_DIR)

.PHONY: clean-obj
clean-obj:
	@echo "  CLEAN   $(OBJ_DIR)"
	@$(RMDIR) $(OBJ_DIR)

.PHONY: clean-test
clean-test:
	@echo "  CLEAN   $(TEST_BIN_DIR)"
	@$(RMDIR) $(TEST_BIN_DIR)

# ============================================================================
# INSTALL
# ============================================================================

PREFIX      ?= /usr/local
INSTALL_LIB := $(PREFIX)/lib
INSTALL_INC := $(PREFIX)/include
INSTALL_BIN := $(PREFIX)/bin

.PHONY: install
install: $(LIB_TARGET)
	@echo "Installing to $(PREFIX)..."
	@$(MKDIR) $(INSTALL_LIB)
	@$(MKDIR) $(INSTALL_INC)/wingo
	@$(MKDIR) $(INSTALL_INC)/wingo/util
	@$(INSTALL) -m 644 $(LIB_TARGET) $(INSTALL_LIB)/
	@$(INSTALL) -m 644 $(INCLUDE_DIR)/wingo/*.h $(INSTALL_INC)/wingo/
	@$(INSTALL) -m 644 $(INCLUDE_DIR)/wingo/util/*.h $(INSTALL_INC)/wingo/util/
	@echo "  ✓ Installed to $(PREFIX)"

.PHONY: uninstall
uninstall:
	@echo "Uninstalling from $(PREFIX)..."
	@$(RM) $(INSTALL_LIB)/$(LIB_NAME)
	@$(RMDIR) $(INSTALL_INC)/wingo
	@echo "  ✓ Uninstalled"

# ============================================================================
# DIRECTORY CREATION
# ============================================================================

$(BUILD_DIR):
	@$(MKDIR) $(BUILD_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
	@$(MKDIR) $(OBJ_DIR)

$(LIB_DIR): | $(BUILD_DIR)
	@$(MKDIR) $(LIB_DIR)

$(TEST_BIN_DIR): | $(BUILD_DIR)
	@$(MKDIR) $(TEST_BIN_DIR)

# ============================================================================
# HELP
# ============================================================================

.PHONY: help
help:
	@echo ""
	@echo "$(PROJECT_NAME) v$(PROJECT_VERSION) — Build System"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build targets:"
	@echo "  all             Build library (default)"
	@echo "  tests           Build all tests"
	@echo "  test            Build and run all tests"
	@echo "  test-verbose    Run tests with verbose output"
	@echo ""
	@echo "Individual test targets:"
	@echo "  test-buffer     Run buffer tests"
	@echo "  test-list       Run list tests"
	@echo "  test-queue      Run queue tests"
	@echo "  test-hashmap    Run hashmap tests"
	@echo ""
	@echo "Quality targets:"
	@echo "  valgrind        Run tests with Valgrind"
	@echo "  sanitize        Build with ASan + UBSan"
	@echo "  sanitize-test   Build and run with sanitizers"
	@echo "  coverage        Generate code coverage report"
	@echo "  analyze         Run static analysis (clang-tidy)"
	@echo "  cppcheck        Run cppcheck"
	@echo "  format          Format source files"
	@echo "  format-check    Check formatting"
	@echo ""
	@echo "Install targets:"
	@echo "  install         Install to PREFIX (default: /usr/local)"
	@echo "  uninstall       Remove from PREFIX"
	@echo ""
	@echo "Clean targets:"
	@echo "  clean           Remove all build artifacts"
	@echo "  clean-obj       Remove object files"
	@echo "  clean-test      Remove test binaries"
	@echo ""
	@echo "  help            Show this help"
	@echo ""

# ============================================================================
# INFO
# ============================================================================

.PHONY: info
info:
	@echo "Project:    $(PROJECT_NAME)"
	@echo "Version:    $(PROJECT_VERSION)"
	@echo "CC:         $(CC)"
	@echo "CFLAGS:     $(CFLAGS)"
	@echo "LDFLAGS:    $(LDFLAGS)"
	@echo "LIBS:       $(LIBS)"
	@echo ""
	@echo "Sources:"
	@for src in $(CORE_SRCS); do echo "  $$src"; done
	@echo ""
	@echo "Objects:"
	@for obj in $(CORE_OBJS); do echo "  $$obj"; done
	@echo ""
	@echo "Library:    $(LIB_TARGET)"
	@echo "Tests:      $(TEST_TARGETS)"
