#!/usr/bin/env bash
set -euo pipefail
xcode-select --install >/dev/null 2>&1 || true
make
echo "Built macOS binary: ./image_analyzer"
