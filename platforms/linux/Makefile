 # ============================================================================
# Wingo — P2P Internet Sharing Tool (Repo: Bowie)
# Copyright (C) 2024 ASBM Team
#
# Linux Platform Build
#
# This Makefile builds the Linux-specific parts of Bowie:
#   - Platform implementation (TUN, network, iptables)
#   - Main entry point
#   - Bowie Tools CLI
#
# It depends on libbowie.a (built by the top-level Makefile).
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

# Root of the project (two levels up)
ROOT_DIR        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
PLATFORM_DIR    := $(ROOT_DIR)/platforms/linux
CORE_DIR        := $(ROOT_DIR)/core
INCLUDE_DIR     := $(CORE_DIR)/include
SRC_DIR         := $(CORE_DIR)/src
BUILD_DIR       := $(ROOT_DIR)/build
LIB_DIR         := $(BUILD_DIR)/lib
BIN_DIR         := $(BUILD_DIR)/bin
OBJ_DIR         := $(BUILD_DIR)/obj/platforms/linux

# ============================================================================
# TARGETS
# ============================================================================

# Library from top-level Makefile
CORE_LIB        := $(LIB_DIR)/libbowie.a

# Main binary
BOWIE_BIN       := $(BIN_DIR)/bowie

# Tools binary
TOOLS_BIN       := $(BIN_DIR)/bowie-tools

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
    -I$(ROOT_DIR)

CFLAGS := \
    $(CSTD) \
    $(WARNINGS) \
    $(OPT) \
    $(DEBUG) \
    $(DEFINES) \
    $(INCLUDES) \
    -pthread

LDFLAGS := \
    -pthread

LIBS := \
    -lssl \
    -lcrypto \
    -lpthread

# ============================================================================
# SOURCE FILES
# ============================================================================

# Linux platform sources
PLATFORM_SRCS := \
    $(PLATFORM_DIR)/platform.c

# Main source
MAIN_SRC := $(PLATFORM_DIR)/main.c

# Tools source
TOOLS_SRC := $(PLATFORM_DIR)/bowie-tools.c

# Object files
PLATFORM_OBJS := $(patsubst $(PLATFORM_DIR)/%.c,$(OBJ_DIR)/%.o,$(PLATFORM_SRCS))
MAIN_OBJ := $(OBJ_DIR)/main.o
TOOLS_OBJ := $(OBJ_DIR)/bowie-tools.o

# ============================================================================
# DEFAULT TARGET
# ============================================================================

.PHONY: all
all: $(BOWIE_BIN) $(TOOLS_BIN)

# ============================================================================
# LIBRARY DEPENDENCY
# ============================================================================

# Build core library if not present
$(CORE_LIB):
	@echo "  Building core library..."
	@$(MAKE) -C $(ROOT_DIR) all

# ============================================================================
# MAIN BINARY
# ============================================================================

$(BOWIE_BIN): $(PLATFORM_OBJS) $(MAIN_OBJ) $(CORE_LIB) | $(BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) $(PLATFORM_OBJS) $(MAIN_OBJ) \
	    -L$(LIB_DIR) -lbowie \
	    $(LIBS) -o $@
	@echo "  ✓ Built: $@"

# ============================================================================
# TOOLS BINARY
# ============================================================================

$(TOOLS_BIN): $(PLATFORM_OBJS) $(TOOLS_OBJ) $(CORE_LIB) | $(BIN_DIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) $(PLATFORM_OBJS) $(TOOLS_OBJ) \
	    -L$(LIB_DIR) -lbowie \
	    $(LIBS) -o $@
	@echo "  ✓ Built: $@"

# ============================================================================
# OBJECT FILES
# ============================================================================

$(OBJ_DIR)/%.o: $(PLATFORM_DIR)/%.c | $(OBJ_DIR)
	@$(MKDIR) $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# DIRECTORY CREATION
# ============================================================================

$(OBJ_DIR):
	@$(MKDIR) $(OBJ_DIR)

$(BIN_DIR):
	@$(MKDIR) $(BIN_DIR)

$(LIB_DIR):
	@$(MKDIR) $(LIB_DIR)

# ============================================================================
# RUN
# ============================================================================

.PHONY: run
run: $(BOWIE_BIN)
	@echo "Running $(BOWIE_BIN)..."
	@sudo $(BOWIE_BIN) --tun

.PHONY: run-tools
run-tools: $(TOOLS_BIN)
	@echo "Running $(TOOLS_BIN)..."
	@$(TOOLS_BIN) diag interfaces

# ============================================================================
# INSTALL
# ============================================================================

PREFIX      ?= /usr/local
INSTALL_BIN := $(PREFIX)/bin

.PHONY: install
install: $(BOWIE_BIN) $(TOOLS_BIN)
	@echo "Installing to $(PREFIX)..."
	@$(MKDIR) $(INSTALL_BIN)
	@$(INSTALL) -m 755 $(BOWIE_BIN) $(INSTALL_BIN)/
	@$(INSTALL) -m 755 $(TOOLS_BIN) $(INSTALL_BIN)/
	@echo "  ✓ Installed to $(INSTALL_BIN)"

.PHONY: uninstall
uninstall:
	@echo "Uninstalling from $(PREFIX)..."
	@$(RM) $(INSTALL_BIN)/bowie
	@$(RM) $(INSTALL_BIN)/bowie-tools
	@echo "  ✓ Uninstalled"

# ============================================================================
# CLEAN
# ============================================================================

.PHONY: clean
clean:
	@echo "  CLEAN   $(OBJ_DIR)"
	@$(RMDIR) $(OBJ_DIR)
	@$(RM) $(BOWIE_BIN) $(TOOLS_BIN)

.PHONY: clean-all
clean-all: clean
	@echo "  CLEAN   $(ROOT_DIR)/build"
	@$(RMDIR) $(ROOT_DIR)/build

# ============================================================================
# INFO
# ============================================================================

.PHONY: info
info:
	@echo "Project:       $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "Platform:      Linux"
	@echo "Root:          $(ROOT_DIR)"
	@echo "Platform dir:  $(PLATFORM_DIR)"
	@echo "Core lib:      $(CORE_LIB)"
	@echo ""
	@echo "Sources:"
	@for src in $(PLATFORM_SRCS) $(MAIN_SRC) $(TOOLS_SRC); do echo "  $$src"; done
	@echo ""
	@echo "Binaries:"
	@echo "  $(BOWIE_BIN)"
	@echo "  $(TOOLS_BIN)"

# ============================================================================
# HELP
# ============================================================================

.PHONY: help
help:
	@echo ""
	@echo "$(PROJECT_NAME) v$(PROJECT_VERSION) — Linux Platform"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build targets:"
	@echo "  all             Build main + tools (default)"
	@echo "  install         Install to PREFIX (default: /usr/local)"
	@echo "  uninstall       Remove from PREFIX"
	@echo ""
	@echo "Run targets:"
	@echo "  run             Run main binary (requires sudo)"
	@echo "  run-tools       Run tools binary"
	@echo ""
	@echo "Clean targets:"
	@echo "  clean           Remove object files and binaries"
	@echo "  clean-all       Remove entire build directory"
	@echo ""
	@echo "Info targets:"
	@echo "  info            Show build information"
	@echo "  help            Show this help"
	@echo ""
