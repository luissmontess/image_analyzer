#!/usr/bin/env python3
"""CLI wrapper for image_analyzer.

No GUI dependencies needed. Interactive command-line interface.
"""

from __future__ import annotations

import datetime as _dt
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REQUIRED_IMAGE_COUNT = 3


def is_supported_bmp_24(path: Path) -> bool:
    """Check if file is 24-bit uncompressed BMP."""
    try:
        with path.open("rb") as f:
            header = f.read(54)
        if len(header) < 54:
            return False
        if header[0:2] != b"BM":
            return False

        bits_per_pixel = struct.unpack_from("<H", header, 28)[0]
        compression = struct.unpack_from("<I", header, 30)[0]
        return bits_per_pixel == 24 and compression == 0
    except OSError:
        return False


def collect_bmp_files(paths: list[str]) -> list[Path]:
    """Collect all valid BMP files from paths."""
    collected = []

    for path_str in paths:
        path = Path(path_str).expanduser().resolve()

        if path.is_dir():
            for item in path.glob("**/*.bmp"):
                if is_supported_bmp_24(item):
                    collected.append(item)
                else:
                    print(f"⊘ Skipped (not 24-bit BMP): {item}")
        elif path.is_file():
            if is_supported_bmp_24(path):
                collected.append(path)
            else:
                print(f"⊘ Skipped (not 24-bit BMP): {path}")

    return collected


def prepare_temp_input(files: list[Path]) -> Path:
    """Create temp input folder with symlinks/copies."""
    temp_root = Path(tempfile.mkdtemp(prefix="image_analyzer_cli_"))
    in_dir = temp_root / "input"
    in_dir.mkdir(parents=True, exist_ok=True)

    for idx, src in enumerate(files, start=1):
        safe_name = src.name.replace(" ", "_")
        dst = in_dir / f"img_{idx:02d}_{safe_name}"
        shutil.copy2(src, dst)
        print(f"  [{idx}] {dst.name}")

    return in_dir


def write_txt_report(
    output_dir: Path,
    used_files: list[Path],
    threads: int,
    exit_code: int,
    stdout_text: str,
    stderr_text: str,
) -> Path:
    """Write a formatted text report."""
    report_path = output_dir / "cli_last_run.txt"
    timestamp = _dt.datetime.now().isoformat(timespec="seconds")

    lines = [
        "═" * 70,
        f"Timestamp: {timestamp}",
        f"Threads: {threads}",
        f"Exit Code: {exit_code}",
        f"Input Count: {len(used_files)}",
        "─" * 70,
        "Input Files:",
    ]
    lines.extend(f"  {i+1}. {p}" for i, p in enumerate(used_files))
    lines.extend(
        [
            "─" * 70,
            "Output:",
            "  " + (stdout_text.strip() or "<no output>").replace("\n", "\n  "),
            "─" * 70,
            "Errors:",
            "  " + (stderr_text.strip() or "<no errors>").replace("\n", "\n  "),
            "═" * 70,
        ]
    )

    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_path


def main() -> int:
    repo_root = Path(__file__).resolve().parent
    binary_path = repo_root / "image_analyzer"

    print("╔" + "═" * 68 + "╗")
    print("║ " + "image_analyzer CLI".center(66) + " ║")
    print("╚" + "═" * 68 + "╝")
    print()

    if not binary_path.exists():
        print(f"✗ C binary not found: {binary_path}")
        print("  Run: make")
        return 1

    # Add files interactively
    files: list[Path] = []
    print("Enter file paths (one per line). Accepts files or directories.")
    print('Type "done" when finished:')
    print()

    while True:
        user_input = input(">> ").strip()
        if user_input.lower() == "done":
            break
        if not user_input:
            continue

        new_files = collect_bmp_files([user_input])
        if new_files:
            files.extend(new_files)
            print(f"  ✓ Added {len(new_files)} file(s)")
        else:
            print(f"  ⊘ No valid BMPs found")

    print()
    if not files:
        print("✗ No files added.")
        return 1

    print(f"✓ Total valid BMP files: {len(files)}")

    if len(files) < REQUIRED_IMAGE_COUNT:
        print(f"✗ Need at least {REQUIRED_IMAGE_COUNT} files (found {len(files)}).")
        return 1

    # Config
    print()
    print("Configuration:")
    print(f"  Threads: ", end="", flush=True)
    threads_str = input() or "6"
    try:
        threads = int(threads_str)
    except ValueError:
        threads = 6

    print(f"  Output dir [{repo_root / 'output'}]: ", end="", flush=True)
    output_str = input().strip() or str(repo_root / "output")
    output_dir = Path(output_str).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    # Prepare
    print()
    print("Preparing temp input folder...")
    temp_in_dir = prepare_temp_input(files[:REQUIRED_IMAGE_COUNT])

    program = str(binary_path)
    args = [
        "--input-dir",
        str(temp_in_dir),
        "--output-dir",
        str(output_dir),
        "--threads",
        str(threads),
    ]

    print()
    print("=" * 70)
    print(f"Command: {program} {' '.join(args)}")
    print("=" * 70)
    print()

    # Run
    try:
        result = subprocess.run(
            [program] + args,
            capture_output=True,
            text=True,
            timeout=600,
        )
        stdout_text = result.stdout
        stderr_text = result.stderr
        exit_code = result.returncode
    except subprocess.TimeoutExpired:
        print("✗ Process timed out (10 min limit).")
        stdout_text = ""
        stderr_text = "Timeout"
        exit_code = -1
    except Exception as exc:
        print(f"✗ Error: {exc}")
        stdout_text = ""
        stderr_text = str(exc)
        exit_code = -1

    print(stdout_text)
    if stderr_text:
        print("STDERR:", stderr_text, file=sys.stderr)

    report_path = write_txt_report(
        output_dir=output_dir,
        used_files=files[:REQUIRED_IMAGE_COUNT],
        threads=threads,
        exit_code=exit_code,
        stdout_text=stdout_text,
        stderr_text=stderr_text,
    )

    shutil.rmtree(temp_in_dir.parent, ignore_errors=True)

    print()
    print("=" * 70)
    if exit_code == 0:
        print(f"✓ SUCCESS")
        print(f"  Results: {output_dir / f'{threads}_threads'}")
        print(f"  Report: {report_path}")
    else:
        print(f"✗ FAILED (exit code {exit_code})")
        print(f"  Report: {report_path}")
    print("=" * 70)

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
