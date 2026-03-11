# CISV CLI Makefile
CC ?= gcc
VERSION ?= $(shell cat ../VERSION 2>/dev/null || echo dev)

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
PORTABLE ?= 0

# SECURITY: Binary hardening flags for executables
# -fstack-protector-strong: Protect against stack buffer overflows
# -D_FORTIFY_SOURCE=2: Detect buffer overflows in string/memory functions
# -fPIE / -pie: Position Independent Executable for ASLR
# -Wl,-z,relro,-z,now: Full RELRO - make GOT read-only after relocation
SECURITY_CFLAGS = -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
SECURITY_LDFLAGS =

CC_VERSION := $(shell $(CC) --version 2>/dev/null | head -n 1)
CC_IS_CLANG := $(findstring clang,$(CC_VERSION))

BASE_CFLAGS = -O3 -pipe -fomit-frame-pointer -Wall -Wextra -std=c11 \
              -ffast-math -funroll-loops $(SECURITY_CFLAGS)

ifeq ($(PORTABLE),1)
    ifeq ($(UNAME_M),x86_64)
        ARCH_CFLAGS = -msse4.2
    else ifeq ($(UNAME_M),amd64)
        ARCH_CFLAGS = -msse4.2
    else ifeq ($(UNAME_M),arm64)
        ARCH_CFLAGS = -march=armv8-a
    else ifeq ($(UNAME_M),aarch64)
        ARCH_CFLAGS = -march=armv8-a
    else
        ARCH_CFLAGS =
    endif
else
    ifeq ($(UNAME_M),x86_64)
        ARCH_CFLAGS = -march=native -mavx2 -mtune=native
    else ifeq ($(UNAME_M),amd64)
        ARCH_CFLAGS = -march=native -mavx2 -mtune=native
    else ifeq ($(UNAME_M),arm64)
        ifeq ($(UNAME_S),Darwin)
            ARCH_CFLAGS =
        else
            ARCH_CFLAGS = -mcpu=native
        endif
    else ifeq ($(UNAME_M),aarch64)
        ARCH_CFLAGS = -march=native
    else
        ARCH_CFLAGS = -march=native
    endif
endif

LTO_CFLAGS =
LTO_LDFLAGS =

ifneq ($(CC_IS_CLANG),)
    LTO_CFLAGS =
    LTO_LDFLAGS =
else
    LTO_CFLAGS = -flto
    LTO_LDFLAGS = -flto
endif

CFLAGS ?= $(BASE_CFLAGS) $(ARCH_CFLAGS) $(LTO_CFLAGS)
STRIP_LDFLAGS = -s
LDFLAGS ?= $(LTO_LDFLAGS) $(STRIP_LDFLAGS) $(SECURITY_LDFLAGS)
CFLAGS += -DCISV_CLI_VERSION="$(VERSION)"
STATIC ?= 1

# Directories
SRC_DIR = src
BUILD_DIR = build
CORE_DIR = ../core/core

# Source files
SRCS = $(SRC_DIR)/main.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Binary
CLI_BIN = $(BUILD_DIR)/cisv

# Platform detection
ifeq ($(UNAME_S),Linux)
    SECURITY_LDFLAGS = -Wl,-z,relro,-z,now -pie
    CFLAGS += -D_GNU_SOURCE
    LDFLAGS += -lrt
endif
ifeq ($(UNAME_S),Darwin)
    SECURITY_LDFLAGS =
    STRIP_LDFLAGS =
    CFLAGS += -D__APPLE__
    STATIC := 0
endif

ifeq ($(STATIC),1)
    LDFLAGS += -static
endif

# Targets
.PHONY: all clean install uninstall

all: $(CLI_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Ensure core library is built first
$(CORE_DIR)/build/libcisv.a:
	$(MAKE) -C $(CORE_DIR) static

$(CLI_BIN): $(OBJS) $(CORE_DIR)/build/libcisv.a | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(CORE_DIR)/build/libcisv.a $(LDFLAGS) -lm -lpthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(CORE_DIR)/include -c -o $@ $<

# Installation
PREFIX ?= /usr/local

install: $(CLI_BIN)
	install -d $(PREFIX)/bin
	install -m 755 $(CLI_BIN) $(PREFIX)/bin/cisv
	install -d $(PREFIX)/share/man/man1
	install -m 644 ../docs/man/cisv.1 $(PREFIX)/share/man/man1/cisv.1

uninstall:
	rm -f $(PREFIX)/bin/cisv
	rm -f $(PREFIX)/share/man/man1/cisv.1

clean:
	rm -rf $(BUILD_DIR)

# Test CLI
test: $(CLI_BIN)
	@echo "Testing CLI..."
	@echo "a,b,c" > /tmp/test.csv
	@echo "1,2,3" >> /tmp/test.csv
	@echo "4,5,6" >> /tmp/test.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) -c /tmp/test.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header /tmp/test.csv > /tmp/test_no_header.out
	@grep -q '^1,2,3$$' /tmp/test_no_header.out
	@grep -q '^4,5,6$$' /tmp/test_no_header.out
	@cat /tmp/test.csv | env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) - > /tmp/test_stdin.out
	@grep -q '^a,b,c$$' /tmp/test_stdin.out
	@grep -q '^1,2,3$$' /tmp/test_stdin.out
	@cat /tmp/test.csv | env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header - > /tmp/test_stdin_no_header.out
	@grep -q '^1,2,3$$' /tmp/test_stdin_no_header.out
	@! grep -q '^a,b,c$$' /tmp/test_stdin_no_header.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select-name a,c /tmp/test.csv > /tmp/test_select_name.out
	@grep -q '^a,c$$' /tmp/test_select_name.out
	@grep -q '^1,3$$' /tmp/test_select_name.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --where 'a==1' /tmp/test.csv > /tmp/test_where.out
	@grep -q '^1,2,3$$' /tmp/test_where.out
	@! grep -q '^4,5,6$$' /tmp/test_where.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --json /tmp/test.csv > /tmp/test_json.out
	@test "$$(head -c 1 /tmp/test_json.out)" = "["
	@grep -q '"a":"1"' /tmp/test_json.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --jsonl /tmp/test.csv > /tmp/test_jsonl.out
	@grep -q '^{' /tmp/test_jsonl.out
	@grep -q '"a":"1"' /tmp/test_jsonl.out
	@rm /tmp/test.csv
	@rm -f /tmp/test_no_header.out /tmp/test_stdin.out /tmp/test_stdin_no_header.out /tmp/test_select_name.out /tmp/test_where.out /tmp/test_json.out /tmp/test_jsonl.out
	@echo "CLI tests passed"
