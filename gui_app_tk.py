#!/usr/bin/env python3
"""Tkinter GUI for image_analyzer (no external dependencies).

This lightweight app lets users drag and drop BMP files and run the C backend.
Uses only Python standard library (Tkinter).
"""

from __future__ import annotations

import datetime as _dt
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from tkinter import (
    END,
    DISABLED,
    NORMAL,
    Button,
    Entry,
    Frame,
    Label,
    Listbox,
    Scrollbar,
    Spinbox,
    StringVar,
    Text,
    Tk,
    filedialog,
    messagebox,
    dnd,
)

REQUIRED_IMAGE_COUNT = 3


class ImageAnalyzerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("image_analyzer GUI (Tkinter)")
        self.root.geometry("1000x700")

        self.repo_root = Path(__file__).resolve().parent
        self.binary_path = self.repo_root / "image_analyzer"
        self.default_output_dir = self.repo_root / "output"

        self.selected_files: list[Path] = []
        self.is_running = False

        self._build_ui()

    def _build_ui(self) -> None:
        # Title section
        title_frame = Frame(self.root, bg="#f0f0f0", height=80)
        title_frame.pack(fill="x", padx=0, pady=0)

        title = Label(
            title_frame,
            text="image_analyzer GUI",
            font=("Arial", 20, "bold"),
            bg="#f0f0f0",
        )
        title.pack(pady=8)

        subtitle = Label(
            title_frame,
            text="Drop BMP files or use Add Files. Supports 24-bit uncompressed BMP only.",
            font=("Arial", 10),
            bg="#f0f0f0",
            fg="#555",
        )
        subtitle.pack(pady=4)

        # Controls section
        ctrl_frame = Frame(self.root)
        ctrl_frame.pack(fill="x", padx=10, pady=10)

        Label(ctrl_frame, text="Threads:", font=("Arial", 10)).pack(side="left", padx=5)
        self.threads_var = StringVar(value="6")
        self.threads_spin = Spinbox(
            ctrl_frame,
            from_=1,
            to=128,
            textvariable=self.threads_var,
            width=5,
            font=("Arial", 10),
        )
        self.threads_spin.pack(side="left", padx=2)

        Label(ctrl_frame, text="  |  Output Dir:", font=("Arial", 10)).pack(side="left", padx=5)
        self.output_var = StringVar(value=str(self.default_output_dir))
        self.output_entry = Entry(ctrl_frame, textvariable=self.output_var, width=40, font=("Arial", 9))
        self.output_entry.pack(side="left", padx=2, fill="x", expand=True)

        btn_browse = Button(ctrl_frame, text="Browse", command=self._pick_output_dir, width=8)
        btn_browse.pack(side="left", padx=2)

        # File list section
        Label(
            self.root,
            text="Drag & drop .bmp files here, or use buttons below:",
            font=("Arial", 9, "italic"),
            fg="#666",
        ).pack(pady=5)

        list_frame = Frame(self.root)
        list_frame.pack(fill="both", expand=True, padx=10, pady=5)

        scrollbar = Scrollbar(list_frame)
        scrollbar.pack(side="right", fill="y")

        self.file_listbox = Listbox(
            list_frame,
            yscrollcommand=scrollbar.set,
            font=("Courier", 9),
            height=12,
        )
        self.file_listbox.pack(side="left", fill="both", expand=True)
        scrollbar.config(command=self.file_listbox.yview)

        # Buttons section
        button_frame = Frame(self.root)
        button_frame.pack(fill="x", padx=10, pady=10)

        btn_add = Button(button_frame, text="Add Files", command=self._pick_files, width=12)
        btn_add.pack(side="left", padx=2)

        btn_remove = Button(button_frame, text="Remove Selected", command=self._remove_file, width=15)
        btn_remove.pack(side="left", padx=2)

        btn_clear = Button(button_frame, text="Clear All", command=self._clear_files, width=12)
        btn_clear.pack(side="left", padx=2)

        btn_run = Button(
            button_frame,
            text="🚀 Run Filters",
            command=self._run_filters,
            width=20,
            bg="#4CAF50",
            fg="white",
            font=("Arial", 10, "bold"),
        )
        btn_run.pack(side="right", padx=2)

        btn_open = Button(
            button_frame, text="📁 Open Output", command=self._open_output, width=15, bg="#2196F3", fg="white"
        )
        btn_open.pack(side="right", padx=2)

        # Log section
        Label(self.root, text="Logs:", font=("Arial", 9, "bold")).pack(pady=(5, 0), padx=10, anchor="w")

        log_frame = Frame(self.root)
        log_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        scrollbar_log = Scrollbar(log_frame)
        scrollbar_log.pack(side="right", fill="y")

        self.log_text = Text(log_frame, height=10, font=("Courier", 9), yscrollcommand=scrollbar_log.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scrollbar_log.config(command=self.log_text.yview)

        self._append_log("Ready. Add .bmp files or drag & drop them here.")

    def _append_log(self, text: str) -> None:
        self.log_text.config(state=NORMAL)
        self.log_text.insert(END, text + "\n")
        self.log_text.see(END)
        self.log_text.config(state=DISABLED)
        self.root.update()

    def _pick_files(self) -> None:
        files = filedialog.askopenfilenames(
            title="Select BMP files",
            initialdir=str(self.repo_root),
            filetypes=[("BMP files", "*.bmp *.BMP"), ("All files", "*.*")],
        )
        if files:
            self._add_files(list(files))

    def _pick_output_dir(self) -> None:
        folder = filedialog.askdirectory(title="Select output directory")
        if folder:
            self.output_var.set(folder)

    def _add_files(self, files: list[str]) -> None:
        seen = {p.resolve() for p in self.selected_files}
        added = 0
        rejected = 0

        for file_path in files:
            candidate = Path(file_path)
            if not candidate.exists() or not candidate.is_file():
                rejected += 1
                continue
            if candidate.suffix.lower() != ".bmp":
                rejected += 1
                continue

            normalized = candidate.resolve()
            if normalized in seen:
                continue

            self.selected_files.append(normalized)
            seen.add(normalized)
            self.file_listbox.insert(END, str(normalized))
            added += 1

        if added:
            self._append_log(f"✓ Added {added} file(s). Total: {len(self.selected_files)}")
        if rejected:
            self._append_log(f"✗ Skipped {rejected} invalid/non-BMP item(s).")

    def _remove_file(self) -> None:
        selection = self.file_listbox.curselection()
        if not selection:
            messagebox.showinfo("Info", "Select a file to remove.")
            return

        for idx in sorted(selection, reverse=True):
            self.file_listbox.delete(idx)
            del self.selected_files[idx]

        self._append_log(f"Removed {len(selection)} file(s). Total: {len(self.selected_files)}")

    def _clear_files(self) -> None:
        self.file_listbox.delete(0, END)
        self.selected_files.clear()
        self._append_log("Cleared all files.")

    def _open_output(self) -> None:
        out_dir = Path(self.output_var.get()).expanduser().resolve()
        if not out_dir.exists():
            messagebox.showwarning("Not Found", f"Output directory does not exist:\n{out_dir}")
            return

        os.startfile(str(out_dir)) if sys.platform == "win32" else os.system(f"xdg-open '{out_dir}'")

    @staticmethod
    def _is_supported_bmp_24(path: Path) -> bool:
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

    def _validate_inputs(self) -> list[Path]:
        valid = [p for p in self.selected_files if self._is_supported_bmp_24(p)]
        if len(valid) < REQUIRED_IMAGE_COUNT:
            raise ValueError(
                f"Need {REQUIRED_IMAGE_COUNT} supported 24-bit BMP files, found {len(valid)}.\n"
                f"(Unsupported: {len(self.selected_files) - len(valid)})"
            )
        return valid[:REQUIRED_IMAGE_COUNT]

    def _prepare_temp_input(self, files: list[Path]) -> Path:
        temp_root = Path(tempfile.mkdtemp(prefix="img_analyzer_"))
        in_dir = temp_root / "input"
        in_dir.mkdir(parents=True, exist_ok=True)

        for idx, src in enumerate(files, start=1):
            safe_name = src.name.replace(" ", "_")
            dst = in_dir / f"img_{idx:02d}_{safe_name}"
            shutil.copy2(src, dst)

        return in_dir

    def _write_txt_report(
        self,
        output_dir: Path,
        used_files: list[Path],
        threads: int,
        exit_code: int,
        stdout_text: str,
        stderr_text: str,
    ) -> Path:
        report_path = output_dir / "gui_last_run.txt"
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

    def _set_controls_enabled(self, enabled: bool) -> None:
        state = NORMAL if enabled else DISABLED
        self.threads_spin.config(state=state)
        self.output_entry.config(state=state)
        self.file_listbox.config(state=state)
        for btn in self.root.winfo_children():
            if isinstance(btn, Button):
                btn.config(state=state)

    def _run_in_thread(self) -> None:
        if not self.binary_path.exists():
            messagebox.showerror(
                "Binary Not Found",
                f"C binary not found:\n{self.binary_path}\n\nRun 'make' first.",
            )
            return

        try:
            chosen_inputs = self._validate_inputs()
        except ValueError as exc:
            messagebox.showwarning("Invalid Inputs", str(exc))
            return

        out_dir = Path(self.output_var.get()).expanduser().resolve()
        out_dir.mkdir(parents=True, exist_ok=True)

        temp_in_dir = self._prepare_temp_input(chosen_inputs)
        threads = int(self.threads_var.get())

        program = str(self.binary_path)
        args = [
            "--input-dir",
            str(temp_in_dir),
            "--output-dir",
            str(out_dir),
            "--threads",
            str(threads),
        ]

        self._append_log("=" * 70)
        self._append_log(f"Command: {program} {' '.join(args)}")
        self._append_log("Inputs:")
        for p in chosen_inputs:
            self._append_log(f"  • {p}")
        self._append_log("─" * 70)

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
            self._append_log("ERROR: Process timed out after 10 minutes.")
            stdout_text = ""
            stderr_text = "Timeout"
            exit_code = -1
        except Exception as exc:
            self._append_log(f"ERROR: {exc}")
            stdout_text = ""
            stderr_text = str(exc)
            exit_code = -1

        self._append_log(stdout_text.strip() if stdout_text else "<no stdout>")
        if stderr_text.strip():
            self._append_log("STDERR: " + stderr_text.strip())

        report_path = self._write_txt_report(
            output_dir=out_dir,
            used_files=chosen_inputs,
            threads=threads,
            exit_code=exit_code,
            stdout_text=stdout_text,
            stderr_text=stderr_text,
        )

        shutil.rmtree(temp_in_dir.parent, ignore_errors=True)

        self._append_log("─" * 70)
        if exit_code == 0:
            self._append_log(f"✓ SUCCESS. Report: {report_path}")
            messagebox.showinfo(
                "Complete",
                f"Filters done!\n\nResults: {out_dir / f'{threads}_threads'}\nReport: {report_path}",
            )
        else:
            self._append_log(f"✗ FAILED (exit code {exit_code}). Report: {report_path}")
            messagebox.showerror(
                "Failed",
                f"Exit code {exit_code}.\n\nSee logs and:\n{report_path}",
            )

        self.is_running = False
        self._set_controls_enabled(True)

    def _run_filters(self) -> None:
        if self.is_running:
            messagebox.showinfo("Busy", "A process is already running.")
            return

        if not self.selected_files:
            messagebox.showwarning("No Files", "Add at least one BMP file.")
            return

        self.is_running = True
        self._set_controls_enabled(False)
        self._append_log("Starting processing in background...")

        thread = threading.Thread(target=self._run_in_thread, daemon=True)
        thread.start()


def main() -> int:
    root = Tk()
    app = ImageAnalyzerGUI(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
