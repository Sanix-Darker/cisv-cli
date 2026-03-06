#!/usr/bin/env bash
set -euo pipefail

# Build CLI from repo root first: make all
./cli/build/cisv -s 0,2 examples/sample.csv
./cli/build/cisv --from-line 2 --to-line 3 examples/sample.csv
./cli/build/cisv -d ',' --trim examples/sample.csv
