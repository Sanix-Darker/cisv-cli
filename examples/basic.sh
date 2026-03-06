#!/usr/bin/env bash
set -euo pipefail

# Build CLI from repo root first: make all
./cli/build/cisv examples/sample.csv
./cli/build/cisv -c examples/sample.csv
