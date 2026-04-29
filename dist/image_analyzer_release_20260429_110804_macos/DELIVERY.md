# Delivery Guide

This file describes exactly what to submit and how to run the project on Linux, macOS, and Windows.

## What To Submit

Submit the full project folder (recommended), including:

- C backend source: `main.c`, `bmp.c`, `bmp.h`, `filters.c`, `filters.h`, `task_pool.c`, `task_pool.h`, `timing.c`, `timing.h`
- Build files/scripts: `Makefile`, `run_experiments.sh`
- GUI files: `gui_qt_designer.py`, `gui_app.py`, `gui_app_tk.py`, `gui_web.py`, `ui/main_window.ui`
- Documentation: `README.md`, `wiki.md`, `DELIVERY.md`
- Assets and folders: `logo.png`, `input/`, `output/` (can be empty except `.gitkeep`)

Do not submit only a binary if your instructor expects source review, reproducibility, or portability.

## Recommended Packaging

Create one compressed archive of the whole project:

- Linux/macOS: `zip -r image_analyzer_submission.zip image_analyzer/`
- Windows (PowerShell): `Compress-Archive -Path image_analyzer -DestinationPath image_analyzer_submission.zip`

## Platform Notes

You cannot use one single native executable for all OSes.

- Linux needs Linux binary
- macOS needs macOS binary
- Windows needs `.exe`

If you want convenience, include optional prebuilt binaries per platform, but still include full source.

## Ready-Made Release Script

This project now includes `package_release.sh` to generate release archives per platform:

```bash
chmod +x package_release.sh
./package_release.sh
```

It creates:

- `dist/*_linux.tar.gz` with Linux executable already built
- `dist/*_macos.tar.gz` with source + `build_macos.sh`
- `dist/*_windows.zip` with source + `build_windows_msys2.sh`

Note: macOS and Windows binaries must be built on their respective environments (or matching cross-toolchains).

## Build And Run

### Linux

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-pip
make
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms all
python3 gui_qt_designer.py
```

### macOS

```bash
xcode-select --install
brew install make python
make
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms all
python3 gui_qt_designer.py
```

### Windows

Use WSL (recommended for `pthread`) or build with a POSIX-compatible toolchain.

WSL path:

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-pip
make
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms all
python3 gui_qt_designer.py
```

## Quick Validation Before Submission

1. Put 1 to 10 BMP files in `input/` (8/24-bit uncompressed, or 32-bit BI_RGB/BI_BITFIELDS/BI_ALPHABITFIELDS).
2. Run:

```bash
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms 0,3
```

3. Confirm:
   - Program prints `Tareas exitosas: X/X`
   - Images appear in `output/6_threads/`
   - `output/summary_runs.csv` and `output/task_runs.csv` are updated

## Suggested Submission Message

Include a short note like:

"Submitted complete source + GUI + docs. Project builds on Linux/macOS and on Windows via WSL. Supports BMP 8/24-bit uncompressed and 32-bit BI_RGB/BI_BITFIELDS/BI_ALPHABITFIELDS."
