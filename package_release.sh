#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$ROOT_DIR/dist"
STAMP="$(date +%Y%m%d_%H%M%S)"
BASE_NAME="image_analyzer_release_${STAMP}"

mkdir -p "$DIST_DIR"

echo "[1/4] Building Linux executable..."
make -C "$ROOT_DIR"

echo "[2/4] Preparing release folders..."
LINUX_DIR="$DIST_DIR/${BASE_NAME}_linux"
MACOS_DIR="$DIST_DIR/${BASE_NAME}_macos"
WINDOWS_DIR="$DIST_DIR/${BASE_NAME}_windows"

mkdir -p "$LINUX_DIR" "$MACOS_DIR" "$WINDOWS_DIR"

copy_common() {
  local target="$1"
  cp -r \
    "$ROOT_DIR/ui" \
    "$ROOT_DIR/input" \
    "$ROOT_DIR/output" \
    "$ROOT_DIR/main.c" \
    "$ROOT_DIR/bmp.c" \
    "$ROOT_DIR/bmp.h" \
    "$ROOT_DIR/filters.c" \
    "$ROOT_DIR/filters.h" \
    "$ROOT_DIR/task_pool.c" \
    "$ROOT_DIR/task_pool.h" \
    "$ROOT_DIR/timing.c" \
    "$ROOT_DIR/timing.h" \
    "$ROOT_DIR/Makefile" \
    "$ROOT_DIR/README.md" \
    "$ROOT_DIR/wiki.md" \
    "$ROOT_DIR/DELIVERY.md" \
    "$ROOT_DIR/gui_app.py" \
    "$ROOT_DIR/gui_app_tk.py" \
    "$ROOT_DIR/gui_qt_designer.py" \
    "$ROOT_DIR/gui_web.py" \
    "$ROOT_DIR/cli_app.py" \
    "$ROOT_DIR/run_experiments.sh" \
    "$ROOT_DIR/requirements-gui.txt" \
    "$ROOT_DIR/logo.png" \
    "$target/"
}

copy_common "$LINUX_DIR"
copy_common "$MACOS_DIR"
copy_common "$WINDOWS_DIR"

cp "$ROOT_DIR/image_analyzer" "$LINUX_DIR/image_analyzer"
chmod +x "$LINUX_DIR/image_analyzer"

cat > "$MACOS_DIR/build_macos.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
xcode-select --install >/dev/null 2>&1 || true
make
echo "Built macOS binary: ./image_analyzer"
EOF
chmod +x "$MACOS_DIR/build_macos.sh"

cat > "$WINDOWS_DIR/build_windows_msys2.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
make
cp image_analyzer image_analyzer.exe
echo "Built Windows binary: ./image_analyzer.exe"
EOF
chmod +x "$WINDOWS_DIR/build_windows_msys2.sh"

cat > "$WINDOWS_DIR/build_windows_msvc.bat" <<'EOF'
@echo off
echo Build with MSYS2/MinGW is recommended for pthread compatibility.
echo If using MSVC, adapt threading layer first.
EOF

echo "[3/4] Compressing release packages..."
(
  cd "$DIST_DIR"
  tar -czf "${BASE_NAME}_linux.tar.gz" "$(basename "$LINUX_DIR")"
  tar -czf "${BASE_NAME}_macos.tar.gz" "$(basename "$MACOS_DIR")"
  zip -qr "${BASE_NAME}_windows.zip" "$(basename "$WINDOWS_DIR")"
)

echo "[4/4] Done."
echo "Generated:"
echo "  $DIST_DIR/${BASE_NAME}_linux.tar.gz"
echo "  $DIST_DIR/${BASE_NAME}_macos.tar.gz"
echo "  $DIST_DIR/${BASE_NAME}_windows.zip"
