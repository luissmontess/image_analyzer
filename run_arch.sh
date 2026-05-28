#!/bin/bash
set -euo pipefail

ARCH=$(uname -m)
BASE_DIR=/mirror/image_parallel

if [ "$ARCH" = "aarch64" ]; then
    exec "$BASE_DIR/image_processor_arm" "$@"
elif [ "$ARCH" = "x86_64" ]; then
    exec "$BASE_DIR/image_processor_x86" "$@"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi
