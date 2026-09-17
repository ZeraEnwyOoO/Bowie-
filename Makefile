 # ============================================================================
# Wingo — P2P Internet Sharing Tool (Repo: Bowie)
# Copyright (C) 2024 ASBM Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ============================================================================

# ============================================================================
# PROJECT INFO
# ============================================================================

PROJECT_NAME    := bowie
PROJECT_VERSION := 0.1.0

# ============================================================================
# COMPILER
# ============================================================================

CC      := gcc
AR      := ar
 CFLAGS  := -std=c11 -Wall -Wextra -Wno-unused-parameter \
           -Wno-unused-function -Wno-format-truncation \
           -Wno-stringop-truncation -fPIC -O0 -g \
           -D_POSIX_C_SOURCE=200809L \
           -D_DEFAULT_SOURCE \
           -D_GNU_SOURCE
LDFLAGS :=

# ============================================================================
# PATHS
# ============================================================================

ROOT_DIR     := $(shell pwd)
CORE_DIR     := $(ROOT_DIR)/core
INCLUDE_DIR  := $(CORE_DIR)/include
SRC_DIR      := $(CORE_DIR)/src
TEST_DIR     := $(CORE_DIR)/tests/unit
BUILD_DIR    := $(ROOT_DIR)/build
OBJ_DIR      := $(BUILD_DIR)/obj
LIB_DIR      := $(BUILD_DIR)/lib
BIN_DIR      := $(BUILD_DIR)/bin

# ============================================================================
# PHASE 1: FOUNDATION
# ============================================================================

SRCS_PHASE1 := \
	$(SRC_DIR)/error.c \
	$(SRC_DIR)/log.c \
	$(SRC_DIR)/util/buffer.c \
	$(SRC_DIR)/util/list.c \
	$(SRC_DIR)/util/queue.c \
	$(SRC_DIR)/util/hashmap.c \
	$(SRC_DIR)/util/time.c \
	$(SRC_DIR)/util/random.c

# ============================================================================
# PHASE 2: CORE ENGINE
# ============================================================================

SRCS_PHASE2 := \
	$(SRC_DIR)/core/engine.c \
	$(SRC_DIR)/core/event.c \
	$(SRC_DIR)/core/state.c \
	$(SRC_DIR)/core/thread.c

# ============================================================================
# PHASE 3: PLATFORM
# ============================================================================

SRCS_PHASE3 := \
	$(SRC_DIR)/platform/platform.c

# ============================================================================
# PHASE 4: NETWORK
# ============================================================================

SRCS_PHASE4 := \
	$(SRC_DIR)/net/socket.c \
	$(SRC_DIR)/net/peer.c \
	$(SRC_DIR)/net/dht/dht_config.c \
	$(SRC_DIR)/net/dht/dht_bencode.c \
	$(SRC_DIR)/net/dht/dht_node.c \
	$(SRC_DIR)/net/dht/dht_bucket.c \
	$(SRC_DIR)/net/dht/dht_routing.c \
	$(SRC_DIR)/net/dht/dht_token.c \
	$(SRC_DIR)/net/dht/dht_security.c \
	$(SRC_DIR)/net/dht/dht_storage.c \
	$(SRC_DIR)/net/dht/dht_message.c \
	$(SRC_DIR)/net/dht/dht_search.c \
	$(SRC_DIR)/net/dht/dht.c

# ============================================================================
# ALL SOURCES
# ============================================================================

SRCS := $(SRCS_PHASE1) $(SRCS_PHASE2) $(SRCS_PHASE3) $(SRCS_PHASE4)

OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# ============================================================================
# LIBRARY
# ============================================================================

LIB_NAME := libwingo.a
LIB_PATH := $(LIB_DIR)/$(LIB_NAME)

# ============================================================================
# TESTS
# ============================================================================

TESTS := \
	test_dht_bencode \
	test_dht_node \
	test_dht_bucket \
	test_dht_routing \
	test_dht_token

TEST_SRCS := \
	$(TEST_DIR)/net/dht/test_dht_bencode.c \
	$(TEST_DIR)/net/dht/test_dht_node.c \
	$(TEST_DIR)/net/dht/test_dht_bucket.c \
	$(TEST_DIR)/net/dht/test_dht_routing.c \
	$(TEST_DIR)/net/dht/test_dht_token.c

TEST_BINS := $(patsubst %,$(BIN_DIR)/%,$(TESTS))

# ============================================================================
# INCLUDES
# ============================================================================

INCLUDES := \
	-I$(INCLUDE_DIR) \
	-I$(ROOT_DIR)

# ============================================================================
# CHECK FRAMEWORK
# ============================================================================

CHECK_CFLAGS := $(shell pkg-config --cflags check 2>/dev/null)
CHECK_LIBS   := $(shell pkg-config --libs check 2>/dev/null)

ifeq ($(CHECK_LIBS),)
	CHECK_LIBS := -lcheck -lsubunit -lm -lpthread -lrt
endif

# ============================================================================
# TARGETS
# ============================================================================

.PHONY: all lib tests test compile-all clean help dirs

# ----- Default -----
all: lib

# ----- Library -----
lib: dirs $(LIB_PATH)

$(LIB_PATH): $(OBJS)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

# ----- Object files -----
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ----- Directories -----
dirs:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BIN_DIR)

# ----- Tests -----
tests: $(TEST_BINS)

$(BIN_DIR)/test_dht_bencode: $(TEST_DIR)/net/dht/test_dht_bencode.c $(LIB_PATH)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) $(CHECK_CFLAGS) $< $(LIB_PATH) $(CHECK_LIBS) -o $@

$(BIN_DIR)/test_dht_node: $(TEST_DIR)/net/dht/test_dht_node.c $(LIB_PATH)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) $(CHECK_CFLAGS) $< $(LIB_PATH) $(CHECK_LIBS) -o $@

$(BIN_DIR)/test_dht_bucket: $(TEST_DIR)/net/dht/test_dht_bucket.c $(LIB_PATH)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) $(CHECK_CFLAGS) $< $(LIB_PATH) $(CHECK_LIBS) -o $@

$(BIN_DIR)/test_dht_routing: $(TEST_DIR)/net/dht/test_dht_routing.c $(LIB_PATH)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) $(CHECK_CFLAGS) $< $(LIB_PATH) $(CHECK_LIBS) -o $@

$(BIN_DIR)/test_dht_token: $(TEST_DIR)/net/dht/test_dht_token.c $(LIB_PATH)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) $(INCLUDES) $(CHECK_CFLAGS) $< $(LIB_PATH) $(CHECK_LIBS) -o $@

# ----- Run tests -----
test: tests
	@echo ""
	@echo "========================================="
	@echo "  Running Phase 4 tests"
	@echo "========================================="
	@for t in $(TEST_BINS); do \
		echo ""; \
		echo ">>> $$t"; \
		$$t || exit 1; \
	done
	@echo ""
	@echo "========================================="
	@echo "  All tests passed!"
	@echo "========================================="

# ----- Compile-only (check errors) -----
compile-all: dirs $(OBJS)
	@echo ""
	@echo "========================================="
	@echo "  All source files compiled successfully!"
	@echo "========================================="

# ----- Clean -----
clean:
	@echo "  RM      $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

# ----- Help -----
help:
	@echo "Wingo — Bowie Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all           Build library (default)"
	@echo "  lib           Build static library"
	@echo "  tests         Build test binaries"
	@echo "  test          Build + run all tests"
	@echo "  compile-all   Compile all sources (check errors)"
	@echo "  clean         Remove build directory"
	@echo "  help          Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo ""
	@echo "Sources:"
	@echo "  Phase 1: $(words $(SRCS_PHASE1)) files"
	@echo "  Phase 2: $(words $(SRCS_PHASE2)) files"
	@echo "  Phase 3: $(words $(SRCS_PHASE3)) files"
	@echo "  Phase 4: $(words $(SRCS_PHASE4)) files"
	@echo "  Total:   $(words $(SRCS)) files"
