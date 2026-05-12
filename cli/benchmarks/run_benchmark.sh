#!/bin/bash
#
# CISV CLI Benchmark
#
# Compares cisv CLI performance against other popular CSV command-line tools.
#
# Tools compared:
# - cisv: High-performance C CSV parser with SIMD optimizations
# - xan: Rust CSV CLI with SIMD parsing and parallel execution
# - qsv: Rust-based xsv fork with 80+ commands, SIMD-accelerated
# - xsv: Original Rust CSV toolkit
# - csvtk: Go-based cross-platform CSV/TSV toolkit
# - tsv-utils: D-based tools optimized for large datasets (eBay)
# - frawk: Rust AWK-like tool with native CSV support
# - miller: Go-based awk/sed/cut for name-indexed data
# - datamash: GNU statistical operations tool
# - goawk: Go AWK with native CSV support
# - csvkit: Python-based CSV utilities
# - wc/awk/cut: Unix baseline tools
#
# Usage:
#   ./run_benchmark.sh [OPTIONS]
#
# Options:
#   --rows=N        Number of rows to generate (default: 1000000)
#   --cols=N        Number of columns (default: 7)
#   --iterations=N  Benchmark iterations (default: 5)
#   --file=PATH     Use existing CSV file instead of generating
#   --no-count-variants
#                  Skip generated quoted/comment/skip-empty count fixtures
#   --slice-rows=N  Rows to emit in head/tail/slice benchmarks (default: 100)
#   --slice-start=N 1-based line start for cisv range slicing (default: 1000)
#   --fast          Skip slow tools (csvkit) and medium tools (miller, datamash, goawk)
#   --help          Show this help
#

set -o pipefail

# ============================================================================
# CONFIGURATION
# ============================================================================

ITERATIONS=5
ROWS=1000000
COLS=7
FAST_MODE=false
INPUT_FILE=""
COUNT_VARIANTS=true
SLICE_ROWS=100
SLICE_START=1000

# Temp file for results
RESULTS_FILE=""

# ============================================================================
# UTILITY FUNCTIONS
# ============================================================================

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

get_file_size() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        stat -f%z "$1" 2>/dev/null || echo "0"
    else
        stat -c%s "$1" 2>/dev/null || echo "0"
    fi
}

format_number() {
    printf "%'d" "$1" 2>/dev/null || echo "$1"
}

print_tool_version() {
    local name="$1"
    local cmd="$2"
    if command_exists "$name"; then
        local version
        version=$($cmd 2>/dev/null | head -n 1 || true)
        echo "  ${name}: ${version:-available}"
    else
        echo "  ${name}: not installed"
    fi
}

# ============================================================================
# TEST DATA GENERATION
# ============================================================================

generate_csv() {
    local rows=$1
    local cols=$2
    local filename=$3

    echo "Generating CSV: $(format_number $rows) rows × ${cols} columns..."

    local start=$(date +%s.%N 2>/dev/null || date +%s)

    python3 << PYEOF
import sys

rows = $rows
cols = $cols
filename = '$filename'

with open(filename, 'w') as f:
    # Header
    f.write(','.join(['col' + str(i) for i in range(cols)]) + '\n')
    # Data rows
    for i in range(rows):
        f.write(','.join(['value_' + str(i) + '_' + str(j) for j in range(cols)]) + '\n')
        if i > 0 and i % 500000 == 0:
            print(f"  Generated {i:,} rows...", file=sys.stderr)
PYEOF

    local end=$(date +%s.%N 2>/dev/null || date +%s)
    local elapsed=$(awk "BEGIN {printf \"%.2f\", $end - $start}")
    local size=$(get_file_size "$filename")
    local size_mb=$(awk "BEGIN {printf \"%.1f\", $size / 1048576}")
    echo "  Done in ${elapsed}s, file size: ${size_mb} MB"
}

generate_count_variant_csv() {
    local variant=$1
    local rows=$2
    local cols=$3
    local filename=$4

    echo "Generating count fixture (${variant}): $(format_number $rows) rows × ${cols} columns..."

    python3 << PYEOF
import sys

variant = '$variant'
rows = $rows
cols = $cols
filename = '$filename'

def data_row(i):
    return ','.join('value_' + str(i) + '_' + str(j) for j in range(cols)) + '\n'

with open(filename, 'w') as f:
    f.write(','.join('col' + str(i) for i in range(cols)) + '\n')

    if variant == 'quoted':
        for i in range(rows):
            fields = []
            for j in range(cols):
                if j == 1:
                    fields.append('"quoted,' + str(i) + ',' + str(j) + '"')
                elif j == 2:
                    fields.append('"line ' + str(i) + ' part A\nline ' + str(i) + ' part B"')
                else:
                    fields.append('value_' + str(i) + '_' + str(j))
            f.write(','.join(fields) + '\n')
            if i > 0 and i % 500000 == 0:
                print(f"  Generated {i:,} rows...", file=sys.stderr)
    elif variant == 'commented':
        for i in range(rows):
            if i % 10 == 0:
                f.write('#comment_' + str(i) + ',ignored\n')
            f.write(data_row(i))
            if i > 0 and i % 500000 == 0:
                print(f"  Generated {i:,} rows...", file=sys.stderr)
    elif variant == 'skip-empty':
        for i in range(rows):
            if i % 10 == 0:
                f.write('\n')
            f.write(data_row(i))
            if i > 0 and i % 500000 == 0:
                print(f"  Generated {i:,} rows...", file=sys.stderr)
    else:
        raise SystemExit('unknown count fixture: ' + variant)
PYEOF

    local size=$(get_file_size "$filename")
    local size_mb=$(awk "BEGIN {printf \"%.1f\", $size / 1048576}")
    echo "  Fixture size: ${size_mb} MB"
}

# ============================================================================
# BENCHMARK FUNCTION
# ============================================================================

run_benchmark() {
    local name="$1"
    local cmd="$2"
    local category="$3"

    echo "Benchmarking ${name}..."

    local times=()
    local row_count=0

    for i in $(seq 1 $ITERATIONS); do
        local start=$(date +%s.%N 2>/dev/null || date +%s)

        local output
        if output=$(timeout 300 bash -c "$cmd" 2>/dev/null); then
            local end=$(date +%s.%N 2>/dev/null || date +%s)
            local elapsed=$(awk "BEGIN {print $end - $start}")
            times+=("$elapsed")
            # Only infer row count from command output in "count" benchmarks.
            # For "select", tools output CSV text and numeric-only extraction can
            # mis-detect values from payload lines.
            if [[ "$category" == count* ]]; then
                local trimmed
                trimmed=$(echo "$output" | tr -d '[:space:]')
                if [[ "$trimmed" =~ ^[0-9]+$ ]]; then
                    row_count="$trimmed"
                fi
            fi
        else
            echo "  Error: command failed"
            return 1
        fi
    done

    if [ ${#times[@]} -eq 0 ]; then
        return 1
    fi

    # Calculate average
    local total=0
    for t in "${times[@]}"; do
        total=$(awk "BEGIN {print $total + $t}")
    done
    local avg=$(awk "BEGIN {printf \"%.3f\", $total / ${#times[@]}}")

    # Store result
    echo "$category|$name|$avg|$row_count" >> "$RESULTS_FILE"
}

# ============================================================================
# FORMAT OUTPUT
# ============================================================================

format_throughput() {
    local file_size=$1
    local time=$2

    if [ "$time" = "0" ] || [ -z "$time" ]; then
        echo "N/A"
    else
        local mb_per_sec=$(awk "BEGIN {printf \"%.1f\", ($file_size / 1048576) / $time}")
        echo "${mb_per_sec} MB/s"
    fi
}

print_results_table() {
    local category="$1"
    local file_size="$2"
    local default_rows="$3"

    echo ""
    echo "============================================================"
    echo "RESULTS: $category"
    echo "============================================================"
    printf "%-16s %12s %14s %12s\n" "Library" "Parse Time" "Throughput" "Rows"
    echo "------------------------------------------------------------"

    # Filter and sort results by time
    grep "^${category}|" "$RESULTS_FILE" 2>/dev/null | sort -t'|' -k3 -n | while IFS='|' read cat name time rows; do
        [ -z "$rows" ] || [ "$rows" = "0" ] && rows="$default_rows"
        local throughput=$(format_throughput "$file_size" "$time")
        printf "%-16s %10ss %14s %12s\n" "$name" "$time" "$throughput" "$(format_number $rows)"
    done
}

run_count_variant_benchmarks() {
    local variant="$1"
    local filepath="$2"
    local cisv_options="$3"
    local xan_comparable="$4"
    local file_size
    file_size=$(get_file_size "$filepath")

    echo ""
    echo "--- Row Counting Benchmarks (${variant}) ---"
    echo ""

    local category="count-${variant}"
    if [ -n "$CISV_BIN" ]; then
        run_benchmark "cisv" "$CISV_BIN -c ${cisv_options} \"$filepath\"" "$category"
    else
        echo "Benchmarking cisv..."
        echo "  Skipped: cisv not available"
    fi

    if [ "$xan_comparable" = "true" ]; then
        if command_exists xan; then
            run_benchmark "xan" "xan count -n \"$filepath\"" "$category"
        else
            echo "Benchmarking xan..."
            echo "  Skipped: xan not installed"
        fi
    else
        echo "Benchmarking xan..."
        echo "  Skipped: no equivalent xan options for this CISV semantic fixture"
    fi

    print_results_table "$category" "$file_size" "$ROWS"
}

# ============================================================================
# MAIN
# ============================================================================

show_help() {
    cat << EOF
CISV CLI Benchmark

Usage: $0 [OPTIONS]

Options:
    --rows=N        Number of rows to generate (default: 1000000)
    --cols=N        Number of columns (default: 7)
    --iterations=N  Benchmark iterations (default: 5)
    --file=PATH     Use existing CSV file instead of generating
    --no-count-variants
                    Skip generated quoted/comment/skip-empty count fixtures
    --slice-rows=N  Rows to emit in head/tail/slice benchmarks (default: 100)
    --slice-start=N 1-based line start for cisv range slicing (default: 1000)
    --fast          Skip slow tools (csvkit) and medium tools (miller, datamash, goawk)
    --help          Show this help

Tool categories:
    Fast (always run):   cisv, qsv, xsv, csvtk, tsv-utils, frawk, wc, awk, cut
    Medium (skip --fast): miller, datamash, goawk
    Slow (skip --fast):   csvkit

Examples:
    $0 --rows=100000
    $0 --file=/data/large.csv
    $0 --rows=1000000 --fast

EOF
}

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --rows=*) ROWS="${1#*=}"; shift ;;
            --cols=*) COLS="${1#*=}"; shift ;;
            --iterations=*) ITERATIONS="${1#*=}"; shift ;;
            --file=*) INPUT_FILE="${1#*=}"; shift ;;
            --no-count-variants) COUNT_VARIANTS=false; shift ;;
            --slice-rows=*) SLICE_ROWS="${1#*=}"; shift ;;
            --slice-start=*) SLICE_START="${1#*=}"; shift ;;
            --fast) FAST_MODE=true; shift ;;
            --help|-h) show_help; exit 0 ;;
            *) shift ;;
        esac
    done

    # Create temp file for results
    RESULTS_FILE=$(mktemp)
    trap "rm -f $RESULTS_FILE" EXIT

    echo "============================================================"
    echo "CISV CLI Benchmark"
    echo "============================================================"
    echo ""

    # Generate or use existing file
    local filepath
    local file_size

    if [ -n "$INPUT_FILE" ]; then
        filepath="$INPUT_FILE"
        if [ ! -f "$filepath" ]; then
            echo "Error: File not found: $filepath"
            exit 1
        fi
        file_size=$(get_file_size "$filepath")
        local size_mb=$(awk "BEGIN {printf \"%.1f\", $file_size / 1048576}")
        echo "Using existing file: $filepath (${size_mb} MB)"
    else
        filepath="/tmp/cisv_benchmark_$$.csv"
        generate_csv "$ROWS" "$COLS" "$filepath"
        file_size=$(get_file_size "$filepath")
    fi

    local size_mb=$(awk "BEGIN {printf \"%.1f\", $file_size / 1048576}")
    local row_count=$(wc -l < "$filepath" | tr -d ' ')

    echo ""
    echo "============================================================"
    echo "BENCHMARK: $(format_number $ROWS) rows × $COLS columns"
    echo "File size: ${size_mb} MB"
    echo "Iterations: $ITERATIONS"
    echo "Fast mode: $FAST_MODE"
    echo "Count variants: $COUNT_VARIANTS"
    echo "Slice rows: $SLICE_ROWS"
    echo "Slice start: $SLICE_START"
    echo "CPU cores: $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
    echo "Resource env: GOMAXPROCS=${GOMAXPROCS:-unset} GOMEMLIMIT=${GOMEMLIMIT:-unset} CISV_MAX_PROCS=${CISV_MAX_PROCS:-unset} CISV_MAX_MEMORY=${CISV_MAX_MEMORY:-unset} CISV_MAX_ROW_SIZE=${CISV_MAX_ROW_SIZE:-unset}"
    echo "============================================================"
    echo ""

    # Find cisv binary
    local CISV_BIN=""
    if [ -f "./cli/build/cisv" ]; then
        CISV_BIN="./cli/build/cisv"
        export LD_LIBRARY_PATH="./core/core/build:${LD_LIBRARY_PATH:-}"
    elif [ -f "/benchmark/cisv/cli/build/cisv" ]; then
        CISV_BIN="/benchmark/cisv/cli/build/cisv"
        export LD_LIBRARY_PATH="/benchmark/cisv/core/core/build:${LD_LIBRARY_PATH:-}"
    elif [ -f "/usr/local/bin/cisv" ]; then
        CISV_BIN="/usr/local/bin/cisv"
    fi

    echo "Tool versions:"
    if [ -n "$CISV_BIN" ]; then
        echo "  cisv: $($CISV_BIN --version 2>/dev/null | head -n 1 || echo available)"
    else
        echo "  cisv: not available"
    fi
    print_tool_version "xan" "xan --version"
    print_tool_version "qsv" "qsv --version"
    print_tool_version "xsv" "xsv --version"
    echo ""

    # ========================================================================
    # ROW COUNTING BENCHMARKS
    # ========================================================================

    echo "--- Row Counting Benchmarks ---"
    echo ""

    # ---- FAST TOOLS (always run) ----

    # cisv -c
    if [ -n "$CISV_BIN" ]; then
        run_benchmark "cisv" "$CISV_BIN -c \"$filepath\"" "count"
    else
        echo "Benchmarking cisv..."
        echo "  Skipped: cisv not available"
    fi

    # xan count
    if command_exists xan; then
        run_benchmark "xan" "xan count -n \"$filepath\"" "count"
    else
        echo "Benchmarking xan..."
        echo "  Skipped: xan not installed"
    fi

    # qsv count
    if command_exists qsv; then
        run_benchmark "qsv" "qsv count \"$filepath\"" "count"
    else
        echo "Benchmarking qsv..."
        echo "  Skipped: qsv not installed"
    fi

    # xsv count
    if command_exists xsv; then
        run_benchmark "xsv" "xsv count \"$filepath\"" "count"
    else
        echo "Benchmarking xsv..."
        echo "  Skipped: xsv not installed"
    fi

    # csvtk nrow
    if command_exists csvtk; then
        run_benchmark "csvtk" "csvtk nrow \"$filepath\"" "count"
    else
        echo "Benchmarking csvtk..."
        echo "  Skipped: csvtk not installed"
    fi

    # tsv-utils: tsv-summarize (needs CSV to be treated properly)
    if command_exists tsv-summarize; then
        run_benchmark "tsv-utils" "tsv-summarize --count \"$filepath\"" "count"
    else
        echo "Benchmarking tsv-utils..."
        echo "  Skipped: tsv-utils not installed"
    fi

    # frawk
    if command_exists frawk; then
        run_benchmark "frawk" "frawk -i csv 'END{print NR}' \"$filepath\"" "count"
    else
        echo "Benchmarking frawk..."
        echo "  Skipped: frawk not installed"
    fi

    # wc -l (baseline)
    run_benchmark "wc" "wc -l < \"$filepath\"" "count"

    # awk (baseline)
    run_benchmark "awk" "awk 'END{print NR}' \"$filepath\"" "count"

    # ---- MEDIUM TOOLS (skip with --fast) ----

    if [ "$FAST_MODE" != "true" ]; then
        # miller
        if command_exists mlr; then
            run_benchmark "miller" "mlr --csv count \"$filepath\"" "count"
        else
            echo "Benchmarking miller..."
            echo "  Skipped: miller not installed"
        fi

        # datamash
        if command_exists datamash; then
            run_benchmark "datamash" "datamash -H count 1 < \"$filepath\"" "count"
        else
            echo "Benchmarking datamash..."
            echo "  Skipped: datamash not installed"
        fi

        # goawk
        if command_exists goawk; then
            run_benchmark "goawk" "goawk --csv 'END{print NR}' \"$filepath\"" "count"
        else
            echo "Benchmarking goawk..."
            echo "  Skipped: goawk not installed"
        fi
    fi

    # ---- SLOW TOOLS (skip with --fast) ----

    if [ "$FAST_MODE" != "true" ]; then
        # csvkit (Python-based, slow)
        if command_exists csvstat; then
            run_benchmark "csvkit" "csvstat --count \"$filepath\"" "count"
        else
            echo "Benchmarking csvkit..."
            echo "  Skipped: csvkit not installed"
        fi
    fi

    # Print row counting results
    print_results_table "count" "$file_size" "$row_count"

    if [ -n "$INPUT_FILE" ] && [ "$COUNT_VARIANTS" = "true" ]; then
        echo ""
        echo "Skipping generated count variants because --file was provided"
    fi

    local count_variant_files=()
    if [ -z "$INPUT_FILE" ] && [ "$COUNT_VARIANTS" = "true" ]; then
        local quoted_path="/tmp/cisv_benchmark_quoted_$$.csv"
        local commented_path="/tmp/cisv_benchmark_commented_$$.csv"
        local skip_empty_path="/tmp/cisv_benchmark_skip_empty_$$.csv"

        generate_count_variant_csv "quoted" "$ROWS" "$COLS" "$quoted_path"
        count_variant_files+=("$quoted_path")
        run_count_variant_benchmarks "quoted" "$quoted_path" "" "true"

        generate_count_variant_csv "commented" "$ROWS" "$COLS" "$commented_path"
        count_variant_files+=("$commented_path")
        run_count_variant_benchmarks "commented" "$commented_path" "--comment '#'" "false"

        generate_count_variant_csv "skip-empty" "$ROWS" "$COLS" "$skip_empty_path"
        count_variant_files+=("$skip_empty_path")
        run_count_variant_benchmarks "skip-empty" "$skip_empty_path" "--skip-empty" "false"
    fi

    # ========================================================================
    # COLUMN SELECTION BENCHMARKS
    # ========================================================================

    echo ""
    echo "--- Column Selection Benchmarks ---"
    echo ""

    # ---- FAST TOOLS (always run) ----

    # cisv -s (0-indexed)
    if [ -n "$CISV_BIN" ]; then
        run_benchmark "cisv" "$CISV_BIN -s 0,2,3 \"$filepath\" | wc -l" "select"
    fi

    # xan select (1-indexed)
    if command_exists xan; then
        run_benchmark "xan" "xan select 1,3,4 \"$filepath\" | wc -l" "select"
    fi

    # qsv select (1-indexed)
    if command_exists qsv; then
        run_benchmark "qsv" "qsv select 1,3,4 \"$filepath\" | wc -l" "select"
    fi

    # xsv select (1-indexed)
    if command_exists xsv; then
        run_benchmark "xsv" "xsv select 1,3,4 \"$filepath\" | wc -l" "select"
    fi

    # csvtk cut (1-indexed)
    if command_exists csvtk; then
        run_benchmark "csvtk" "csvtk cut -f 1,3,4 \"$filepath\" | wc -l" "select"
    fi

    # tsv-select (1-indexed, works on CSV with -d)
    if command_exists tsv-select; then
        run_benchmark "tsv-utils" "tsv-select -d, -f 1,3,4 \"$filepath\" | wc -l" "select"
    fi

    # frawk (1-indexed with CSV mode)
    if command_exists frawk; then
        run_benchmark "frawk" "frawk -i csv -o csv '{print \$1,\$3,\$4}' \"$filepath\" | wc -l" "select"
    fi

    # cut (baseline, 1-indexed)
    run_benchmark "cut" "cut -d',' -f1,3,4 \"$filepath\" | wc -l" "select"

    # awk (baseline, 1-indexed)
    run_benchmark "awk" "awk -F',' '{print \$1,\$3,\$4}' \"$filepath\" | wc -l" "select"

    # ---- MEDIUM TOOLS (skip with --fast) ----

    if [ "$FAST_MODE" != "true" ]; then
        # miller
        if command_exists mlr; then
            run_benchmark "miller" "mlr --csv cut -f col0,col2,col3 \"$filepath\" | wc -l" "select"
        fi

        # goawk (1-indexed)
        if command_exists goawk; then
            run_benchmark "goawk" "goawk --csv '{print \$1,\$3,\$4}' \"$filepath\" | wc -l" "select"
        fi
    fi

    # ---- SLOW TOOLS (skip with --fast) ----

    if [ "$FAST_MODE" != "true" ]; then
        # csvkit (1-indexed)
        if command_exists csvcut; then
            run_benchmark "csvkit" "csvcut -c 1,3,4 \"$filepath\" | wc -l" "select"
        fi
    fi

    # Print column selection results
    print_results_table "select" "$file_size" "$row_count"

    # ========================================================================
    # ROW SLICING BENCHMARKS
    # ========================================================================

    echo ""
    echo "--- Row Slicing Benchmarks ---"
    echo ""

    local slice_end=$((SLICE_START + SLICE_ROWS - 1))
    local xan_slice_start=$((SLICE_START - 1))
    if [ "$xan_slice_start" -lt 0 ]; then
        xan_slice_start=0
    fi

    if [ -n "$CISV_BIN" ]; then
        run_benchmark "cisv-head" "$CISV_BIN --head $SLICE_ROWS \"$filepath\" | wc -l" "head"
        run_benchmark "cisv-tail" "$CISV_BIN --tail $SLICE_ROWS \"$filepath\" | wc -l" "tail"
        run_benchmark "cisv-range" "$CISV_BIN --from-line $SLICE_START --to-line $slice_end \"$filepath\" | wc -l" "slice"
    fi

    if command_exists xan; then
        run_benchmark "xan-head" "xan head -l $SLICE_ROWS \"$filepath\" | wc -l" "head"
        run_benchmark "xan-slice-head" "xan slice -l $SLICE_ROWS \"$filepath\" | wc -l" "head"
        run_benchmark "xan-tail" "xan tail -l $SLICE_ROWS \"$filepath\" | wc -l" "tail"
        run_benchmark "xan-slice-tail" "xan slice -L $SLICE_ROWS \"$filepath\" | wc -l" "tail"
        run_benchmark "xan-slice" "xan slice -s $xan_slice_start -l $SLICE_ROWS \"$filepath\" | wc -l" "slice"
    else
        echo "Benchmarking xan row slicing..."
        echo "  Skipped: xan not installed"
    fi

    run_benchmark "unix-head" "head -n $SLICE_ROWS \"$filepath\" | wc -l" "head"
    run_benchmark "unix-tail" "tail -n $SLICE_ROWS \"$filepath\" | wc -l" "tail"

    print_results_table "head" "$file_size" "$SLICE_ROWS"
    print_results_table "tail" "$file_size" "$SLICE_ROWS"
    print_results_table "slice" "$file_size" "$SLICE_ROWS"

    # Cleanup
    if [ -z "$INPUT_FILE" ]; then
        rm -f "$filepath"
        for count_variant_file in "${count_variant_files[@]}"; do
            rm -f "$count_variant_file"
        done
        echo ""
        echo "Cleaned up temporary files"
    fi

    echo ""
    echo "Benchmark complete!"
}

main "$@"
