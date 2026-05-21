#!/usr/bin/env bash
#
# Generate and validate large CSV fixtures covering RFC 4180, RFC 7111's
# text/csv registration update, and the current RFC 4180-bis draft behavior.
#
# Defaults intentionally generate 1M data rows per fixture. Use --rows for
# faster local smoke runs.

set -euo pipefail

ROWS=1000000
OUT_DIR="/tmp/cisv-rfc-corpus"
CISV="${CISV:-}"

usage() {
  cat <<'USAGE'
Usage: generate_rfc_csv_corpus.sh [OPTIONS]

Options:
  --rows=N       Data rows per fixture (default: 1000000)
  --out-dir=DIR  Output directory (default: /tmp/cisv-rfc-corpus)
  --cisv=PATH    cisv binary (default: ../build/cisv or ./build/cisv)
  --help         Show this help
USAGE
}

for arg in "$@"; do
  case "$arg" in
    --rows=*) ROWS="${arg#*=}" ;;
    --out-dir=*) OUT_DIR="${arg#*=}" ;;
    --cisv=*) CISV="${arg#*=}" ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

case "$ROWS" in
  ''|*[!0-9]*) echo "--rows must be a positive integer" >&2; exit 2 ;;
esac
if [ "$ROWS" -le 0 ]; then
  echo "--rows must be a positive integer" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$CISV" ]; then
  if [ -x "$SCRIPT_DIR/../build/cisv" ]; then
    CISV="$SCRIPT_DIR/../build/cisv"
  elif [ -x "$SCRIPT_DIR/../../cli/build/cisv" ]; then
    CISV="$SCRIPT_DIR/../../cli/build/cisv"
  elif command -v cisv >/dev/null 2>&1; then
    CISV="$(command -v cisv)"
  else
    echo "cisv binary not found; pass --cisv=PATH" >&2
    exit 2
  fi
fi

mkdir -p "$OUT_DIR"

echo "cisv: $("$CISV" --version | head -n 1)"
echo "rows: $ROWS"
echo "out : $OUT_DIR"

python3 - "$ROWS" "$OUT_DIR" <<'PY'
import os
import sys

rows = int(sys.argv[1])
out_dir = sys.argv[2]
manifest = []

def emit(name, expected_rows, writer):
    path = os.path.join(out_dir, name)
    with open(path, "wb") as f:
        writer(f)
    manifest.append((name, expected_rows, os.path.getsize(path)))

def b(s):
    return s.encode("utf-8")

def basic_crlf(f):
    f.write(b"Id,Name,Amount\r\n")
    for i in range(1, rows + 1):
        f.write(b(f"{i},name_{i},{i % 10000}\r\n"))

def quoted_specials_crlf(f):
    f.write(b"Id,Text,Quote\r\n")
    for i in range(1, rows + 1):
        f.write(b(f'{i},"comma, inside {i}","a ""quoted"" token {i}"\r\n'))

def multiline_crlf(f):
    f.write(b"Id,Text,Status\r\n")
    for i in range(1, rows + 1):
        f.write(b(f'{i},"line {i} A\r\nline {i} B",ok\r\n'))

def lf_utf8_binary(f):
    f.write(b"Id,Utf8,Binary\n")
    for i in range(1, rows + 1):
        f.write(f"{i},cafe_\u00e9_{i},nul_".encode("utf-8"))
        f.write(b"\x00")
        f.write(b(f"_{i}\n"))

def bare_cr(f):
    f.write(b"Id,Name,Amount\r")
    for i in range(1, rows + 1):
        f.write(b(f"{i},name_{i},{i % 10000}\r"))

def no_final_linebreak(f):
    f.write(b"Id,Name,Amount\n")
    for i in range(1, rows + 1):
        line = b(f"{i},name_{i},{i % 10000}")
        if i < rows:
            line += b"\n"
        f.write(line)

def empty_comments(f):
    f.write(b"Id,Text,Status\n")
    for i in range(1, rows + 1):
        if i % 100000 == 0:
            f.write(b(f"# comment {i}\n"))
        if i % 250000 == 0:
            f.write(b"\n")
        if i % 333333 == 0:
            f.write(b(f'{i},"# not a comment {i}",ok\n'))
        else:
            f.write(b(f"{i},value_{i},ok\n"))

def truncated_quoted_eof(f):
    f.write(b"Id,Status,Text\n")
    for i in range(1, rows):
        f.write(b(f"{i},ok,value_{i}\n"))
    f.write(b(f'{rows},ok,"truncated final value'))

emit("rfc4180_basic_crlf.csv", rows + 1, basic_crlf)
emit("rfc4180_quoted_specials_crlf.csv", rows + 1, quoted_specials_crlf)
emit("rfc4180_multiline_crlf.csv", rows + 1, multiline_crlf)
emit("rfc4180_no_final_linebreak.csv", rows + 1, no_final_linebreak)
emit("rfc4180bis_lf_utf8_binary.csv", rows + 1, lf_utf8_binary)
emit("rfc4180bis_bare_cr.csv", rows + 1, bare_cr)

comment_rows = rows // 100000
empty_rows = rows // 250000
emit("rfc4180bis_empty_comments.csv", rows + 1 + comment_rows + empty_rows, empty_comments)
emit("relaxed_truncated_quoted_eof.csv", rows + 1, truncated_quoted_eof)

with open(os.path.join(out_dir, "manifest.tsv"), "w", encoding="utf-8") as f:
    f.write("file\texpected_rows\tbytes\n")
    for name, expected, size in manifest:
        f.write(f"{name}\t{expected}\t{size}\n")
PY

echo
echo "Generated fixtures:"
awk 'NR == 1 { next } { printf "  %-38s rows=%s bytes=%s\n", $1, $2, $3 }' "$OUT_DIR/manifest.tsv"

validate_count() {
  local file="$1"
  local expected="$2"
  shift 2
  local got
  got="$("$CISV" "$@" --count "$OUT_DIR/$file")"
  if [ "$got" != "$expected" ]; then
    echo "count mismatch for $file: expected $expected got $got" >&2
    exit 1
  fi
}

while IFS=$'\t' read -r file expected bytes; do
  [ "$file" = "file" ] && continue
  validate_count "$file" "$expected"
done < "$OUT_DIR/manifest.tsv"

semantic_expected=$((ROWS + 1))
validate_count "rfc4180bis_empty_comments.csv" "$semantic_expected" --skip-empty --comment '#'

select_lines="$("$CISV" --relaxed --select-name Id "$OUT_DIR/relaxed_truncated_quoted_eof.csv" | wc -l | tr -d ' ')"
if [ "$select_lines" != "$semantic_expected" ]; then
  echo "relaxed truncated select mismatch: expected $semantic_expected lines got $select_lines" >&2
  exit 1
fi

echo
echo "Validation OK"
