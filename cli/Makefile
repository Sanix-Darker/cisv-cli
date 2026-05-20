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
CFLAGS += -DCISV_CLI_VERSION=\"$(VERSION)\"
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
	@mkdir -p /tmp/test_cisv_stdin_tmp
	@printf 'a,b\n1,2\n' | env TMPDIR=/tmp/test_cisv_stdin_tmp LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) - >/tmp/test_stdin_tmp_success.out
	@test -z "$$(ls -A /tmp/test_cisv_stdin_tmp)"
	@! printf '12345,x\n' | env TMPDIR=/tmp/test_cisv_stdin_tmp LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-row 4 - >/tmp/test_stdin_tmp_fail.out 2>/tmp/test_stdin_tmp_fail.err
	@test -z "$$(ls -A /tmp/test_cisv_stdin_tmp)"
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select-name a,c /tmp/test.csv > /tmp/test_select_name.out
	@grep -q '^a,c$$' /tmp/test_select_name.out
	@grep -q '^1,3$$' /tmp/test_select_name.out
	@bash -o pipefail -c 'env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select-name a,c /tmp/test.csv | head -1 >/tmp/test_select_name_pipe.out'
	@grep -q '^a,c$$' /tmp/test_select_name_pipe.out
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select-name missing /tmp/test.csv >/tmp/test_select_name_missing.out 2>/tmp/test_select_name_missing.err
	@grep -q 'Missing selected column: missing' /tmp/test_select_name_missing.err
	@printf 'existing\n' > /tmp/test_atomic_existing.out
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select-name missing -o /tmp/test_atomic_existing.out /tmp/test.csv >/tmp/test_atomic_fail.stdout 2>/tmp/test_atomic_fail.err
	@grep -q '^existing$$' /tmp/test_atomic_existing.out
	@test -z "$$(ls /tmp/test_atomic_existing.out.tmp.* 2>/dev/null)"
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) -o /tmp/test_atomic_success.out /tmp/test.csv
	@grep -q '^a,b,c$$' /tmp/test_atomic_success.out
	@printf 'Id,Name\n2,new\n3,keep\n4,del\n' > /tmp/test_merge_newest.csv
	@printf 'Id,Name\n1,mid1\n2,dup\n4,del2\n' > /tmp/test_merge_middle.csv
	@printf 'Id,Name\n1,dupold\n5,old\n' > /tmp/test_merge_oldest.csv
	@printf 'Id\n4\n' > /tmp/test_merge_deleted.csv
	@printf 'Id,Name\n2,new\n3,keep\n1,mid1\n5,old\n' > /tmp/test_merge.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) merge rows /tmp/test_merge_newest.csv /tmp/test_merge_middle.csv /tmp/test_merge_oldest.csv --dedup-key Id --exclude-keys /tmp/test_merge_deleted.csv:Id -o /tmp/test_merge.out --stats-json /tmp/test_merge.stats.json
	@cmp -s /tmp/test_merge.expected /tmp/test_merge.out
	@grep -q '"output_rows": 4' /tmp/test_merge.stats.json
	@grep -q '"duplicate_rows": 2' /tmp/test_merge.stats.json
	@grep -q '"excluded_rows": 2' /tmp/test_merge.stats.json
	@mkdir -p /tmp/test_cisv_merge_external_tmp
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) merge rows /tmp/test_merge_newest.csv /tmp/test_merge_middle.csv /tmp/test_merge_oldest.csv --dedup-key Id --exclude-keys /tmp/test_merge_deleted.csv:Id --external --memory-limit 1MiB --tmp-dir /tmp/test_cisv_merge_external_tmp -o /tmp/test_merge_external.out --stats-json /tmp/test_merge_external.stats.json
	@cmp -s /tmp/test_merge.expected /tmp/test_merge_external.out
	@grep -q '"mode": "external"' /tmp/test_merge_external.stats.json
	@test -z "$$(ls -A /tmp/test_cisv_merge_external_tmp)"
	@printf 'Id,V\n1,a\n2,b\n1,c\n' > /tmp/test_dedup_last.csv
	@printf 'Id,V\n2,b\n1,c\n' > /tmp/test_dedup_last.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) dedup /tmp/test_dedup_last.csv --key Id --keep last > /tmp/test_dedup_last.out
	@cmp -s /tmp/test_dedup_last.expected /tmp/test_dedup_last.out
	@printf 'Id,V\n1,a\n2,b\n3,c\n' > /tmp/test_filter_source.csv
	@printf 'Id\n2\n' > /tmp/test_filter_deleted.csv
	@printf 'Id,V\n1,a\n3,c\n' > /tmp/test_filter.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) filter exclude /tmp/test_filter_source.csv --key Id --keys-file /tmp/test_filter_deleted.csv --keys-file-key Id > /tmp/test_filter.out
	@cmp -s /tmp/test_filter.expected /tmp/test_filter.out
	@printf 'a,b\n1,2\n' > /tmp/test_cat_a.csv
	@printf 'a,b\n3,4\n' > /tmp/test_cat_b.csv
	@printf 'a,b\n1,2\n3,4\n' > /tmp/test_cat.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) cat rows /tmp/test_cat_a.csv /tmp/test_cat_b.csv > /tmp/test_cat.out
	@cmp -s /tmp/test_cat.expected /tmp/test_cat.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --where 'a==1' /tmp/test.csv > /tmp/test_where.out
	@grep -q '^1,2,3$$' /tmp/test_where.out
	@! grep -q '^4,5,6$$' /tmp/test_where.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --json /tmp/test.csv > /tmp/test_json.out
	@test "$$(head -c 1 /tmp/test_json.out)" = "["
	@grep -q '"a":"1"' /tmp/test_json.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --json --head 1 /tmp/test.csv > /tmp/test_json_head.out
	@grep -q '"a":"1"' /tmp/test_json_head.out
	@! grep -q '"a":"4"' /tmp/test_json_head.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --json --no-header /tmp/test.csv > /tmp/test_json_no_header.out
	@grep -q '^\[\["1","2","3"\]' /tmp/test_json_no_header.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --jsonl /tmp/test.csv > /tmp/test_jsonl.out
	@grep -q '^{' /tmp/test_jsonl.out
	@grep -q '"a":"1"' /tmp/test_jsonl.out
	@printf 'a,b\nx\000y,z\n' > /tmp/test_json_nul.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --json /tmp/test_json_nul.csv > /tmp/test_json_nul.out
	@grep -q '"a":"x\\u0000y"' /tmp/test_json_nul.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --where 'b~5' /tmp/test.csv > /tmp/test_where_contains.out
	@grep -q '^4,5,6$$' /tmp/test_where_contains.out
	@! grep -q '^1,2,3$$' /tmp/test_where_contains.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header --where '0>1' --head 1 /tmp/test.csv > /tmp/test_where_head.out
	@grep -q '^4,5,6$$' /tmp/test_where_head.out
	@! grep -q '^1,2,3$$' /tmp/test_where_head.out
	@printf 'a,b,c\n1,2,3\n' > /tmp/test_head.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --head 2 /tmp/test.csv > /tmp/test_head.out
	@cmp -s /tmp/test_head.expected /tmp/test_head.out
	@printf '1,2,3\n' > /tmp/test_head_no_header.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header --head 1 /tmp/test.csv > /tmp/test_head_no_header.out
	@cmp -s /tmp/test_head_no_header.expected /tmp/test_head_no_header.out
	@printf 'a,b\r1,2\r3,4\r' > /tmp/test_head_cr_only.csv
	@printf 'a,b\n1,2\n' > /tmp/test_head_cr_only.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --head 2 /tmp/test_head_cr_only.csv > /tmp/test_head_cr_only.out
	@cmp -s /tmp/test_head_cr_only.expected /tmp/test_head_cr_only.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header --where '0<2' --tail 1 /tmp/test.csv > /tmp/test_where_tail.out
	@grep -q '^1,2,3$$' /tmp/test_where_tail.out
	@! grep -q '^4,5,6$$' /tmp/test_where_tail.out
	@printf '4,5,6\n' > /tmp/test_tail.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --tail 1 /tmp/test.csv > /tmp/test_tail.out
	@cmp -s /tmp/test_tail.expected /tmp/test_tail.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --no-header --tail 1 /tmp/test.csv > /tmp/test_tail_no_header.out
	@cmp -s /tmp/test_tail.expected /tmp/test_tail_no_header.out
	@printf 'a,b\r1,2\r3,4\r' > /tmp/test_tail_cr_only.csv
	@printf '1,2\n3,4\n' > /tmp/test_tail_cr_only.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --tail 2 /tmp/test_tail_cr_only.csv > /tmp/test_tail_cr_only.out
	@cmp -s /tmp/test_tail_cr_only.expected /tmp/test_tail_cr_only.out
	@printf '"a,b",c\n' > /tmp/test_quoted.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_quoted.csv > /tmp/test_quoted.out
	@grep -q '^"a,b",c$$' /tmp/test_quoted.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 0 /tmp/test_quoted.csv > /tmp/test_select_quoted.out
	@grep -q '^"a,b"$$' /tmp/test_select_quoted.out
	@printf '"a""b",c\n' > /tmp/test_escaped_quote.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_escaped_quote.csv > /tmp/test_escaped_quote.out
	@grep -q '^"a""b",c$$' /tmp/test_escaped_quote.out
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 0 /tmp/test_escaped_quote.csv > /tmp/test_select_escaped_quote.out
	@grep -q '^"a""b"$$' /tmp/test_select_escaped_quote.out
	@printf 'a"b,c\n' > /tmp/test_unquoted_quote.csv
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 0 /tmp/test_unquoted_quote.csv >/tmp/test_select_unquoted_quote.out 2>/tmp/test_select_unquoted_quote.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_unquoted_quote.csv >/tmp/test_unquoted_quote.out 2>/tmp/test_unquoted_quote.err
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --relaxed --select 0 /tmp/test_unquoted_quote.csv > /tmp/test_select_unquoted_quote_relaxed.out
	@grep -q '^"a""b"$$' /tmp/test_select_unquoted_quote_relaxed.out
	@printf '"a"x,b\n' > /tmp/test_bad_select_quote.csv
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 0 /tmp/test_bad_select_quote.csv >/tmp/test_bad_select_quote.out 2>/tmp/test_bad_select_quote.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_bad_select_quote.csv >/tmp/test_bad_iterator_quote.out 2>/tmp/test_bad_iterator_quote.err
	@printf '"unterminated\n' > /tmp/test_unterminated_iterator_quote.csv
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_unterminated_iterator_quote.csv >/tmp/test_unterminated_iterator_quote.out 2>/tmp/test_unterminated_iterator_quote.err
	@printf '"line1\nline2",x\n' > /tmp/test_multiline.csv
	@printf '"line1\nline2",x\n' > /tmp/test_multiline.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_multiline.csv > /tmp/test_multiline.out
	@cmp -s /tmp/test_multiline.expected /tmp/test_multiline.out
	@printf '"line1\nline2"\n' > /tmp/test_select_multiline.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 0 /tmp/test_multiline.csv > /tmp/test_select_multiline.out
	@cmp -s /tmp/test_select_multiline.expected /tmp/test_select_multiline.out
	@printf '"line1\nline2",x\n' > /tmp/test_head_multiline.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --head 1 /tmp/test_multiline.csv > /tmp/test_head_multiline.out
	@cmp -s /tmp/test_head_multiline.expected /tmp/test_head_multiline.out
	@printf '"line1\nline2",x\n' > /tmp/test_tail_multiline.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --tail 1 /tmp/test_multiline.csv > /tmp/test_tail_multiline.out
	@cmp -s /tmp/test_tail_multiline.expected /tmp/test_tail_multiline.out
	@printf 'a,b\r1,2\r' > /tmp/test_cr_only.csv
	@printf 'a,b\n1,2\n' > /tmp/test_cr_only.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) /tmp/test_cr_only.csv > /tmp/test_cr_only.out
	@cmp -s /tmp/test_cr_only.expected /tmp/test_cr_only.out
	@printf 'b\n2\n' > /tmp/test_select_cr_only.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --select 1 /tmp/test_cr_only.csv > /tmp/test_select_cr_only.out
	@cmp -s /tmp/test_select_cr_only.expected /tmp/test_select_cr_only.out
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test_cr_only.csv)" = "2"
	@printf '#skip\n1,,3\n\n2,3,4\n' > /tmp/test_count_semantics.csv
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --skip-empty --comment '#' /tmp/test_count_semantics.csv)" = "2"
	@printf 'a,b\n1,2' > /tmp/test_count_no_newline.csv
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test_count_no_newline.csv)" = "2"
	@: > /tmp/test_count_empty.csv
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test_count_empty.csv)" = "0"
	@printf 'a\nb\nc\nd\n' > /tmp/test_count_range.csv
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --from-line 2 --to-line 3 /tmp/test_count_range.csv)" = "2"
	@printf 'b\nc\n' > /tmp/test_range.expected
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --from-line 2 --to-line 3 /tmp/test_count_range.csv > /tmp/test_range.out
	@cmp -s /tmp/test_range.expected /tmp/test_range.out
	@printf 'a\rb\rc\rd\r' > /tmp/test_range_cr_only.csv
	@env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --from-line 2 --to-line 3 /tmp/test_range_cr_only.csv > /tmp/test_range_cr_only.out
	@cmp -s /tmp/test_range.expected /tmp/test_range_cr_only.out
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --no-header /tmp/test.csv)" = "2"
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --where 'a==1' /tmp/test.csv)" = "1"
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --no-header --where '0>1' /tmp/test.csv)" = "1"
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --no-header --head 1 /tmp/test.csv)" = "1"
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --no-header --tail 1 /tmp/test.csv)" = "1"
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count --select-name missing /tmp/test.csv >/tmp/test_count_select_name_missing.out 2>/tmp/test_count_select_name_missing.err
	@grep -q 'Missing selected column: missing' /tmp/test_count_select_name_missing.err
	@printf '123456789,x\n' > /tmp/test_max_row.csv
	@! env CISV_MAX_ROW_SIZE=8 LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test_max_row.csv >/tmp/test_max_row_fail.out 2>/tmp/test_max_row_fail.err
	@test "$$(env CISV_MAX_ROW_SIZE=8 LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-row 64 --count /tmp/test_max_row.csv)" = "1"
	@awk 'BEGIN { for (i = 0; i < 70000; i++) printf "x"; print ",y" }' > /tmp/test_gomemlimit.csv
	@! env GOMEMLIMIT=1MiB LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test_gomemlimit.csv >/tmp/test_gomemlimit_fail.out 2>/tmp/test_gomemlimit_fail.err
	@test "$$(env GOMEMLIMIT=1MiB LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-row 100000 --count /tmp/test_gomemlimit.csv)" = "1"
	@test "$$(env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-procs 1 --parallel --count /tmp/test.csv)" = "3"
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --delimiter "::" --count /tmp/test.csv >/tmp/test_bad_delimiter.out 2>/tmp/test_bad_delimiter.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --quote "qq" --count /tmp/test.csv >/tmp/test_bad_quote.out 2>/tmp/test_bad_quote.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --escape "ee" --count /tmp/test.csv >/tmp/test_bad_escape.out 2>/tmp/test_bad_escape.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --comment "##" --count /tmp/test.csv >/tmp/test_bad_comment.out 2>/tmp/test_bad_comment.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --escape '"' --count /tmp/test.csv >/tmp/test_bad_escape_quote.out 2>/tmp/test_bad_escape_quote.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-procs 0 --count /tmp/test.csv >/tmp/test_max_procs_fail.out 2>/tmp/test_max_procs_fail.err
	@! env LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-memory nope --count /tmp/test.csv >/tmp/test_max_memory_fail.out 2>/tmp/test_max_memory_fail.err
	@! env CISV_MAX_MEMORY=nope LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --count /tmp/test.csv >/tmp/test_env_bad_memory.out 2>/tmp/test_env_bad_memory.err
	@grep -q 'CISV_MAX_MEMORY' /tmp/test_env_bad_memory.err
	@! env GOMAXPROCS=0 LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --parallel --count /tmp/test.csv >/tmp/test_env_bad_procs.out 2>/tmp/test_env_bad_procs.err
	@grep -q 'GOMAXPROCS' /tmp/test_env_bad_procs.err
	@! env CISV_PARALLEL_MIN_BYTES=nope LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --parallel --count /tmp/test.csv >/tmp/test_env_bad_parallel_min.out 2>/tmp/test_env_bad_parallel_min.err
	@grep -q 'CISV_PARALLEL_MIN_BYTES' /tmp/test_env_bad_parallel_min.err
	@test "$$(env GOMEMLIMIT=nope LD_LIBRARY_PATH=$(CORE_DIR)/build:$${LD_LIBRARY_PATH:-} DYLD_LIBRARY_PATH=$(CORE_DIR)/build:$${DYLD_LIBRARY_PATH:-} $(CLI_BIN) --max-memory 1MiB --count /tmp/test.csv)" = "3"
	@rm /tmp/test.csv
	@rm -f /tmp/test_no_header.out /tmp/test_stdin.out /tmp/test_stdin_no_header.out /tmp/test_stdin_tmp_success.out /tmp/test_stdin_tmp_fail.out /tmp/test_stdin_tmp_fail.err /tmp/test_select_name.out /tmp/test_select_name_pipe.out /tmp/test_select_name_missing.out /tmp/test_select_name_missing.err /tmp/test_atomic_existing.out /tmp/test_atomic_existing.out.tmp.* /tmp/test_atomic_fail.stdout /tmp/test_atomic_fail.err /tmp/test_atomic_success.out /tmp/test_atomic_success.out.tmp.* /tmp/test_where.out /tmp/test_json.out /tmp/test_json_head.out /tmp/test_json_no_header.out /tmp/test_jsonl.out /tmp/test_json_nul.csv /tmp/test_json_nul.out /tmp/test_where_contains.out /tmp/test_where_head.out /tmp/test_where_tail.out
	@rm -f /tmp/test_merge_newest.csv /tmp/test_merge_middle.csv /tmp/test_merge_oldest.csv /tmp/test_merge_deleted.csv /tmp/test_merge.expected /tmp/test_merge.out /tmp/test_merge.stats.json /tmp/test_merge_external.out /tmp/test_merge_external.stats.json
	@rm -f /tmp/test_dedup_last.csv /tmp/test_dedup_last.expected /tmp/test_dedup_last.out /tmp/test_filter_source.csv /tmp/test_filter_deleted.csv /tmp/test_filter.expected /tmp/test_filter.out /tmp/test_cat_a.csv /tmp/test_cat_b.csv /tmp/test_cat.expected /tmp/test_cat.out
	@rm -f /tmp/test_head.expected /tmp/test_head.out /tmp/test_head_no_header.expected /tmp/test_head_no_header.out /tmp/test_head_cr_only.csv /tmp/test_head_cr_only.expected /tmp/test_head_cr_only.out /tmp/test_head_multiline.expected /tmp/test_head_multiline.out
	@rm -f /tmp/test_tail.expected /tmp/test_tail.out /tmp/test_tail_no_header.out /tmp/test_tail_cr_only.csv /tmp/test_tail_cr_only.expected /tmp/test_tail_cr_only.out /tmp/test_tail_multiline.expected /tmp/test_tail_multiline.out
	@rmdir /tmp/test_cisv_stdin_tmp
	@rmdir /tmp/test_cisv_merge_external_tmp
	@rm -f /tmp/test_quoted.csv /tmp/test_quoted.out /tmp/test_select_quoted.out /tmp/test_escaped_quote.csv /tmp/test_escaped_quote.out /tmp/test_select_escaped_quote.out
	@rm -f /tmp/test_unquoted_quote.csv /tmp/test_select_unquoted_quote.out /tmp/test_select_unquoted_quote.err /tmp/test_unquoted_quote.out /tmp/test_unquoted_quote.err /tmp/test_select_unquoted_quote_relaxed.out /tmp/test_bad_select_quote.csv /tmp/test_bad_select_quote.out /tmp/test_bad_select_quote.err /tmp/test_bad_iterator_quote.out /tmp/test_bad_iterator_quote.err /tmp/test_unterminated_iterator_quote.csv /tmp/test_unterminated_iterator_quote.out /tmp/test_unterminated_iterator_quote.err
	@rm -f /tmp/test_multiline.csv /tmp/test_multiline.expected /tmp/test_multiline.out /tmp/test_select_multiline.expected /tmp/test_select_multiline.out /tmp/test_cr_only.csv /tmp/test_cr_only.expected /tmp/test_cr_only.out /tmp/test_select_cr_only.expected /tmp/test_select_cr_only.out /tmp/test_count_semantics.csv
	@rm -f /tmp/test_count_no_newline.csv /tmp/test_count_empty.csv /tmp/test_count_range.csv /tmp/test_range.expected /tmp/test_range.out /tmp/test_range_cr_only.csv /tmp/test_range_cr_only.out /tmp/test_count_select_name_missing.out /tmp/test_count_select_name_missing.err
	@rm -f /tmp/test_max_row.csv /tmp/test_max_row_fail.out /tmp/test_max_row_fail.err /tmp/test_gomemlimit.csv /tmp/test_gomemlimit_fail.out /tmp/test_gomemlimit_fail.err
	@rm -f /tmp/test_bad_delimiter.out /tmp/test_bad_delimiter.err /tmp/test_bad_quote.out /tmp/test_bad_quote.err
	@rm -f /tmp/test_bad_escape.out /tmp/test_bad_escape.err /tmp/test_bad_comment.out /tmp/test_bad_comment.err /tmp/test_bad_escape_quote.out /tmp/test_bad_escape_quote.err
	@rm -f /tmp/test_max_procs_fail.out /tmp/test_max_procs_fail.err /tmp/test_max_memory_fail.out /tmp/test_max_memory_fail.err
	@rm -f /tmp/test_env_bad_memory.out /tmp/test_env_bad_memory.err /tmp/test_env_bad_procs.out /tmp/test_env_bad_procs.err /tmp/test_env_bad_parallel_min.out /tmp/test_env_bad_parallel_min.err
	@echo "CLI tests passed"
