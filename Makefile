 # ============================================================================
# Wingo — P2P Internet Sharing Tool (Repo: Bowie)
# Copyright (C) 2024 ASBM Team
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
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

CORE_DIR        := core
INCLUDE_DIR     := $(CORE_DIR)/include
SRC_DIR         := $(CORE_DIR)/src
TEST_DIR        := $(CORE_DIR)/tests
PLATFORM_DIR    := platforms

BUILD_DIR       := build
OBJ_DIR         := $(BUILD_DIR)/obj
BIN_DIR         := $(BUILD_DIR)/bin
LIB_DIR         := $(BUILD_DIR)/lib
TEST_BIN_DIR    := $(BUILD_DIR)/test

# ============================================================================
# TARGETS
# ============================================================================

LIB_NAME        := libbowie.a
LIB_TARGET      := $(LIB_DIR)/$(LIB_NAME)

# Phase 1 Tests
TEST_BUFFER     := $(TEST_BIN_DIR)/test_buffer
TEST_LIST       := $(TEST_BIN_DIR)/test_list
TEST_QUEUE      := $(TEST_BIN_DIR)/test_queue
TEST_HASHMAP    := $(TEST_BIN_DIR)/test_hashmap

# Phase 2 Tests
TEST_ENGINE     := $(TEST_BIN_DIR)/test_engine
TEST_EVENT      := $(TEST_BIN_DIR)/test_event
TEST_STATE      := $(TEST_BIN_DIR)/test_state
TEST_THREAD     := $(TEST_BIN_DIR)/test_thread

TEST_TARGETS    := $(TEST_BUFFER) $(TEST_LIST) $(TEST_QUEUE) $(TEST_HASHMAP) \
                   $(TEST_ENGINE) $(TEST_EVENT) $(TEST_STATE) $(TEST_THREAD)

# ============================================================================
# COMPILER FLAGS
# ============================================================================

CSTD := -std=c11

WARNINGS := \
    -Wall \
    -Wextra \
    -Werror \
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
    -Wno-unused-function \
    -Wno-format-truncation \
    -Wno-stringop-truncation

OPT := -O2
DEBUG := -g

DEFINES := \
    -D_GNU_SOURCE \
    -D_POSIX_C_SOURCE=200809L

INCLUDES := \
    -I$(INCLUDE_DIR) \
    -I.

CFLAGS := \
    $(CSTD) \
    $(WARNINGS) \
    $(OPT) \
    $(DEBUG) \
    $(DEFINES) \
    $(INCLUDES) \
    -fPIC \
    -pthread

LDFLAGS := \
    -pthread

LIBS := \
    -lssl \
    -lcrypto \
    -lpthread

TEST_LIBS := \
    -lcheck \
    -lpthread \
    -lrt \
    -lm \
    $(LIBS)

# ============================================================================
# SOURCE FILES
# ============================================================================

# Phase 1: Foundation
CORE_SRCS := \
    $(SRC_DIR)/error.c \
    $(SRC_DIR)/log.c \
    $(SRC_DIR)/util/buffer.c \
    $(SRC_DIR)/util/list.c \
    $(SRC_DIR)/util/queue.c \
    $(SRC_DIR)/util/hashmap.c \
    $(SRC_DIR)/util/time.c \
    $(SRC_DIR)/util/random.c

# Phase 2: Core Engine
CORE_SRCS += \
    $(SRC_DIR)/core/engine.c \
    $(SRC_DIR)/core/event.c \
    $(SRC_DIR)/core/state.c \
    $(SRC_DIR)/core/thread.c

# Phase 3: Platform (Cross-Platform)
# NOTE: platform.c is in platforms/ (root level), NOT core/src/
CORE_SRCS += \
    $(PLATFORM_DIR)/platform.c

CORE_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))

# ============================================================================
# TEST SOURCES
# ============================================================================

# Phase 1 Tests
TEST_BUFFER_SRCS    := $(TEST_DIR)/unit/test_buffer.c
TEST_LIST_SRCS      := $(TEST_DIR)/unit/test_list.c
TEST_QUEUE_SRCS     := $(TEST_DIR)/unit/test_queue.c
TEST_HASHMAP_SRCS   := $(TEST_DIR)/unit/test_hashmap.c

# Phase 2 Tests
TEST_ENGINE_SRCS    := $(TEST_DIR)/unit/test_engine.c
TEST_EVENT_SRCS     := $(TEST_DIR)/unit/test_event.c
TEST_STATE_SRCS     := $(TEST_DIR)/unit/test_state.c
TEST_THREAD_SRCS    := $(TEST_DIR)/unit/test_thread.c

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

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@$(MKDIR) $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# TEST BUILD
# ============================================================================

.PHONY: tests
tests: $(TEST_TARGETS)

# Phase 1 Tests
$(TEST_BUFFER): $(TEST_BUFFER_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_LIST): $(TEST_LIST_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_QUEUE): $(TEST_QUEUE_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_HASHMAP): $(TEST_HASHMAP_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

# Phase 2 Tests
$(TEST_ENGINE): $(TEST_ENGINE_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_EVENT): $(TEST_EVENT_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_STATE): $(TEST_STATE_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

$(TEST_THREAD): $(TEST_THREAD_SRCS) $(LIB_TARGET) | $(TEST_BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lbowie $(TEST_LIBS) -o $@

# ============================================================================
# RUN TESTS
# ============================================================================

.PHONY: test
test: tests
	@echo ""
	@echo "=========================================="
	@echo "  Running Phase 1 Tests"
	@echo "=========================================="
	@$(TEST_BUFFER)
	@$(TEST_LIST)
	@$(TEST_QUEUE)
	@$(TEST_HASHMAP)
	@echo ""
	@echo "=========================================="
	@echo "  Running Phase 2 Tests"
	@echo "=========================================="
	@$(TEST_ENGINE)
	@$(TEST_EVENT)
	@$(TEST_STATE)
	@$(TEST_THREAD)
	@echo ""
	@echo "=========================================="
	@echo "  All tests passed!"
	@echo "=========================================="

# ============================================================================
# CLEAN
# ============================================================================

.PHONY: clean
clean:
	@echo "  CLEAN   $(BUILD_DIR)"
	@$(RMDIR) $(BUILD_DIR)

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
	@echo "$(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "  all       Build library (default)"
	@echo "  tests     Build all tests"
	@echo "  test      Build and run all tests"
	@echo "  clean     Remove build artifacts"
	@echo "  help      Show this help"
	@echo ""
