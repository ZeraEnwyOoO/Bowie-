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
#
# ============================================================================
# Bowie — Root Makefile
# ============================================================================
#
# This Makefile builds:
#   - libbowie.a     (core library)
#   - bowie_test     (test program)
#
# Usage:
#   make             — build library
#   make test        — build test program
#   make run         — build and run test
#   make all         — build library + test
#   make clean       — clean build artifacts
#   make install     — install library (requires root)
#
# ============================================================================

# ============================================================================
# PROJECT INFO
# ============================================================================

PROJECT_NAME    := bowie
PROJECT_VERSION := 0.1.0

# ============================================================================
# DIRECTORIES
# ============================================================================

# Source root
SRC_DIR         := core/src
INC_DIR         := core/include

# Build output
BUILD_DIR       := build
OBJ_DIR         := $(BUILD_DIR)/obj
LIB_DIR         := $(BUILD_DIR)/lib
BIN_DIR         := $(BUILD_DIR)/bin

# Test
TEST_DIR        := tests

# ============================================================================
# COMPILER
# ============================================================================

CC              ?= gcc
AR              ?= ar

# ============================================================================
# FLAGS
# ============================================================================

# C standard
CSTD            := -std=c11

# Warnings
WARNINGS        := -Wall -Wextra -Wpedantic \
                   -Wno-unused-parameter \
                   -Wno-unused-function

# Optimization
OPT             := -O2

# Debug
DEBUG           := -g

# Defines
DEFINES         := -D_GNU_SOURCE \
                   -D_POSIX_C_SOURCE=200809L

# Includes
INCLUDES        := -I$(INC_DIR)

# Combine
CFLAGS          := $(CSTD) $(WARNINGS) $(OPT) $(DEBUG) $(DEFINES) $(INCLUDES)

# Linker flags
LDFLAGS         :=

# Libraries
LIBS            := -lpthread

# ============================================================================
# SOURCES — PHASE 1 (Foundation)
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
# SOURCES — PHASE 2 (Core Engine)
# ============================================================================

SRCS_PHASE2 := \
    $(SRC_DIR)/core/engine.c \
    $(SRC_DIR)/core/event.c \
    $(SRC_DIR)/core/state.c \
    $(SRC_DIR)/core/thread.c

# ============================================================================
# SOURCES — PHASE 3 (Platform)
# ============================================================================

SRCS_PHASE3 := \
    $(SRC_DIR)/platform/platform.c

# ============================================================================
# SOURCES — PHASE 4 (Network)
# ============================================================================

SRCS_PHASE4 := \
    $(SRC_DIR)/net/socket.c \
    $(SRC_DIR)/net/peer.c \
    $(SRC_DIR)/net/dht/dht.c \
    $(SRC_DIR)/net/dht/dht_node.c \
    $(SRC_DIR)/net/dht/dht_bucket.c \
    $(SRC_DIR)/net/dht/dht_routing.c \
    $(SRC_DIR)/net/dht/dht_search.c \
    $(SRC_DIR)/net/dht/dht_storage.c \
    $(SRC_DIR)/net/dht/dht_message.c \
    $(SRC_DIR)/net/dht/dht_token.c \
    $(SRC_DIR)/net/dht/dht_security.c \
    $(SRC_DIR)/net/dht/dht_bencode.c \
    $(SRC_DIR)/net/dht/dht_config.c

# ============================================================================
# ALL SOURCES
# ============================================================================

SRCS            := $(SRCS_PHASE1) $(SRCS_PHASE2) $(SRCS_PHASE3) $(SRCS_PHASE4)

# Object files
OBJS            := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Dependency files
DEPS            := $(OBJS:.o=.d)

# ============================================================================
# LIBRARY
# ============================================================================

LIB_NAME        := lib$(PROJECT_NAME).a
LIB_PATH        := $(LIB_DIR)/$(LIB_NAME)

# ============================================================================
# TEST PROGRAM
# ============================================================================

TEST_NAME       := bowie_test
TEST_PATH       := $(BIN_DIR)/$(TEST_NAME)

TEST_SRCS       := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS       := $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/test/%.o)

# ============================================================================
# TARGETS
# ============================================================================

.PHONY: all lib test run clean install uninstall help dirs

# Default target
all: lib

# ============================================================================
# DIRECTORIES
# ============================================================================

dirs:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/net
	@mkdir -p $(OBJ_DIR)/net/dht
	@mkdir -p $(OBJ_DIR)/core
	@mkdir -p $(OBJ_DIR)/util
	@mkdir -p $(OBJ_DIR)/platform
	@mkdir -p $(OBJ_DIR)/test
	@mkdir -p $(LIB_DIR)
	@mkdir -p $(BIN_DIR)

# ============================================================================
# LIBRARY
# ============================================================================

lib: dirs $(LIB_PATH)

$(LIB_PATH): $(OBJS)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

# ============================================================================
# OBJECT FILES
# ============================================================================

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ============================================================================
# TEST PROGRAM
# ============================================================================

test: dirs lib $(TEST_PATH)

$(TEST_PATH): $(TEST_OBJS) $(LIB_PATH)
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $(TEST_OBJS) $(LIB_PATH) $(LDFLAGS) $(LIBS) -o $@

$(OBJ_DIR)/test/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ============================================================================
# RUN TEST
# ============================================================================

run: test
	@echo ""
	@echo "  Running $(TEST_NAME)..."
	@echo ""
	@$(TEST_PATH)

# ============================================================================
# CLEAN
# ============================================================================

clean:
	@echo "  CLEAN   $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)

# ============================================================================
# INSTALL
# ============================================================================

install: lib
	@echo "  INSTALL $(LIB_NAME)"
	@install -d /usr/local/lib
	@install -d /usr/local/include/wingo
	@install -m 644 $(LIB_PATH) /usr/local/lib/
	@cp -r $(INC_DIR)/wingo/* /usr/local/include/wingo/
	@echo "  INSTALL done"

uninstall:
	@echo "  UNINSTALL $(LIB_NAME)"
	@rm -f /usr/local/lib/$(LIB_NAME)
	@rm -rf /usr/local/include/wingo
	@echo "  UNINSTALL done"

# ============================================================================
# HELP
# ============================================================================

help:
	@echo "Bowie — Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       — build library (default)"
	@echo "  lib       — build library"
	@echo "  test      — build test program"
	@echo "  run       — build and run test"
	@echo "  clean     — clean build artifacts"
	@echo "  install   — install library (requires root)"
	@echo "  uninstall — uninstall library"
	@echo "  help      — show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CC        — C compiler (default: gcc)"
	@echo "  AR        — archiver (default: ar)"
	@echo ""

# ============================================================================
# INCLUDE DEPENDENCIES
# ============================================================================

-include $(DEPS)

# ============================================================================
# END OF MAKEFILE
# ============================================================================
