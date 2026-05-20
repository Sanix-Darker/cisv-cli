#!/bin/bash
#
# CISV row merge benchmark.
#
# Generates the production-shaped fixture:
#   newest.csv: IDs 125001..375000
#   middle.csv: IDs  62501..312500
#   oldest.csv: IDs      1..250000
#   deleted.csv: IDs 175001..200000
#
# Expected data rows:
#   input:      750000
#   output:     350000
#   duplicates: 325000
#   excluded:    75000

set -euo pipefail

ITERATIONS=3
ROOT="/tmp/cisv_merge_benchmark"
ROWS_PER_FILE=250000
FAST=false

show_help() {
    cat <<EOF
CISV merge benchmark

Usage: $0 [OPTIONS]

Options:
  --iterations=N      Iterations per command (default: 3)
  --tmp-dir=DIR       Fixture/output directory (default: /tmp/cisv_merge_benchmark)
  --rows-per-file=N   Rows per source file (default: 250000; exact validation expects 250000)
  --fast              Skip non-cisv comparators
  --help              Show this help

EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iterations=*) ITERATIONS="${1#*=}"; shift ;;
        --tmp-dir=*) ROOT="${1#*=}"; shift ;;
        --rows-per-file=*) ROWS_PER_FILE="${1#*=}"; shift ;;
        --fast) FAST=true; shift ;;
        --help|-h) show_help; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

get_file_size() {
    stat -c%s "$1" 2>/dev/null || echo "0"
}

find_cisv() {
    if [ -x "./cli/build/cisv" ]; then
        echo "./cli/build/cisv"
    elif [ -x "/benchmark/cisv/cli/build/cisv" ]; then
        echo "/benchmark/cisv/cli/build/cisv"
    elif command_exists cisv; then
        command -v cisv
    else
        echo ""
    fi
}

generate_fixture() {
    mkdir -p "$ROOT"
    python3 <<PY
from pathlib import Path
root = Path("$ROOT")
rows = int("$ROWS_PER_FILE")
cols = ["Id", "Name", "Email", "Status", "Amount", "UpdatedAt", "Payload"]

def write_source(name, start):
    end = start + rows - 1
    with (root / name).open("w", newline="") as f:
        f.write(",".join(cols) + "\\n")
        for i in range(start, end + 1):
            f.write(f"{i},name_{i},user{i}@example.com,active,{i % 10000},2026-05-20T00:00:00Z,payload_{i}\\n")

write_source("newest.csv", 125001)
write_source("middle.csv", 62501)
write_source("oldest.csv", 1)
with (root / "deleted.csv").open("w", newline="") as f:
    f.write("Id\\n")
    for i in range(175001, 200001):
        f.write(f"{i}\\n")
PY
}

time_command() {
    local name="$1"
    local cmd="$2"
    local time_file="$ROOT/time.$$"
    local total_wall=0
    local total_user=0
    local total_sys=0
    local max_rss=0

    for i in $(seq 1 "$ITERATIONS"); do
        rm -f "$ROOT/${name}.csv" "$ROOT/${name}.stats.json"
        /usr/bin/time -f "%e %U %S %M" -o "$time_file" bash -o pipefail -c "$cmd"
        read -r wall user sys rss < "$time_file"
        total_wall=$(awk "BEGIN {print $total_wall + $wall}")
        total_user=$(awk "BEGIN {print $total_user + $user}")
        total_sys=$(awk "BEGIN {print $total_sys + $sys}")
        if [ "$rss" -gt "$max_rss" ]; then max_rss="$rss"; fi
    done

    local avg_wall avg_user avg_sys
    avg_wall=$(awk "BEGIN {printf \"%.3f\", $total_wall / $ITERATIONS}")
    avg_user=$(awk "BEGIN {printf \"%.3f\", $total_user / $ITERATIONS}")
    avg_sys=$(awk "BEGIN {printf \"%.3f\", $total_sys / $ITERATIONS}")
    printf "%-18s %8ss %8ss %8ss %10s KiB\n" "$name" "$avg_wall" "$avg_user" "$avg_sys" "$max_rss"
    rm -f "$time_file"
}

validate_cisv_stats() {
    local stats="$1"
    python3 <<PY
import json
from pathlib import Path
stats = json.loads(Path("$stats").read_text())
expected = {
    "input_rows": 750000,
    "output_rows": 350000,
    "duplicate_rows": 325000,
    "excluded_rows": 75000,
}
for key, value in expected.items():
    if stats.get(key) != value:
        raise SystemExit(f"{key}: expected {value}, got {stats.get(key)}")
PY
}

CISV_BIN="$(find_cisv)"
if [ -z "$CISV_BIN" ]; then
    echo "cisv binary not found" >&2
    exit 1
fi

if [[ "$CISV_BIN" == /benchmark/* ]]; then
    export LD_LIBRARY_PATH="/benchmark/cisv/core/core/build:${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="./core/core/build:${LD_LIBRARY_PATH:-}"
fi

generate_fixture

SOURCE_BYTES=$(( $(get_file_size "$ROOT/newest.csv") + $(get_file_size "$ROOT/middle.csv") + $(get_file_size "$ROOT/oldest.csv") ))

echo "============================================================"
echo "CISV Merge Rows Benchmark"
echo "============================================================"
echo "Fixture dir: $ROOT"
echo "Rows per source: $ROWS_PER_FILE"
echo "Source bytes: $SOURCE_BYTES"
echo "Iterations: $ITERATIONS"
echo "cisv: $($CISV_BIN --version | head -n 1)"
if command_exists xan; then echo "xan: $(xan --version)"; else echo "xan: not installed"; fi
echo ""
printf "%-18s %9s %9s %9s %14s\n" "Tool" "Wall" "User" "Sys" "Peak RSS"
echo "----------------------------------------------------------------"

CISV_CMD="$CISV_BIN merge rows '$ROOT/newest.csv' '$ROOT/middle.csv' '$ROOT/oldest.csv' --dedup-key Id --exclude-keys '$ROOT/deleted.csv:Id' --output '$ROOT/cisv.csv' --stats-json '$ROOT/cisv.stats.json'"
time_command "cisv" "$CISV_CMD"
validate_cisv_stats "$ROOT/cisv.stats.json"

CISV_EXTERNAL_CMD="$CISV_BIN merge rows '$ROOT/newest.csv' '$ROOT/middle.csv' '$ROOT/oldest.csv' --dedup-key Id --exclude-keys '$ROOT/deleted.csv:Id' --external --memory-limit 64MiB --tmp-dir '$ROOT' --output '$ROOT/cisv-external.csv' --stats-json '$ROOT/cisv-external.stats.json'"
time_command "cisv-external" "$CISV_EXTERNAL_CMD"
validate_cisv_stats "$ROOT/cisv-external.stats.json"
cmp -s "$ROOT/cisv.csv" "$ROOT/cisv-external.csv"

if [ "$FAST" != "true" ] && command_exists xan; then
    XAN_CMD="xan cat rows '$ROOT/newest.csv' '$ROOT/middle.csv' '$ROOT/oldest.csv' | xan join --anti Id - Id '$ROOT/deleted.csv' | xan dedup -s Id > '$ROOT/xan.csv'"
    time_command "xan-pipeline" "$XAN_CMD"
    cmp -s "$ROOT/cisv.csv" "$ROOT/xan.csv"
fi

echo ""
echo "Validation: OK"
