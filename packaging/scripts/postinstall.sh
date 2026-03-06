#!/bin/sh
set -e
if command -v mandb >/dev/null 2>&1; then
  mandb -q || true
fi
