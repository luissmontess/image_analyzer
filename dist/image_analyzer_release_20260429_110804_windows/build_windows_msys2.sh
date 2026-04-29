#!/usr/bin/env bash
set -euo pipefail
make
cp image_analyzer image_analyzer.exe
echo "Built Windows binary: ./image_analyzer.exe"
