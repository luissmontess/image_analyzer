#!/usr/bin/env python3
"""Qt5 Designer-based GUI for image_analyzer.

This loads ui/main_window.ui and wires the screenshot-style layout to the C backend.
"""

from __future__ import annotations

import datetime as _dt
import shutil
import struct
import subprocess
import sys
import tempfile
from time import perf_counter
from pathlib import Path
from typing import Iterable, List

from PyQt5 import uic
from PyQt5.QtCore import QProcess, QUrl
from PyQt5.QtGui import QDesktopServices
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import (
    QApplication,
    QAbstractItemView,
    QCheckBox,
    QFileDialog,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
)

REQUIRED_IMAGE_COUNT = 3
MAX_FILES = 10

TRANSFORM_HORIZONTAL_GRAY = 0
TRANSFORM_VERTICAL_GRAY = 1
TRANSFORM_BLUR_GRAY = 2
TRANSFORM_HORIZONTAL_COLOR = 3
TRANSFORM_VERTICAL_COLOR = 4
TRANSFORM_BLUR_COLOR = 5


class DropListWidget(QListWidget):
    def __init__(self, add_callback, parent=None):
        super().__init__(parent)
        self._add_callback = add_callback
        self.setAcceptDrops(True)
        self.setDragDropMode(QAbstractItemView.DropOnly)

    def dragEnterEvent(self, event):  # noqa: N802
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(self, event):  # noqa: N802
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event):  # noqa: N802
        if not event.mimeData().hasUrls():
            event.ignore()
            return

        files = []
        for url in event.mimeData().urls():
            path = url.toLocalFile()
            if path:
                files.append(path)

        if files:
            self._add_callback(files)
        event.acceptProposedAction()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.repo_root = Path(__file__).resolve().parent
        self.ui_path = self.repo_root / "ui" / "main_window.ui"
        self.binary_path = self.repo_root / "image_analyzer"
        self.default_output_dir = self.repo_root / "output"
        self.logo_path = self.repo_root / "logo.png"

        uic.loadUi(str(self.ui_path), self)
        self._set_logos()

        self.process: QProcess | None = None
        self.temp_input_dir: Path | None = None
        self.selected_files: List[Path] = []
        self.effect_widgets: list[tuple[QCheckBox, int]] = []
        self.current_transforms: list[int] = []
        self.run_started_at = 0.0

        self._replace_drop_widget()
        self.effect_widgets = [
            (self.verticalGrayCheckBox, TRANSFORM_VERTICAL_GRAY),
            (self.verticalColorCheckBox, TRANSFORM_VERTICAL_COLOR),
            (self.horizontalGrayCheckBox, TRANSFORM_HORIZONTAL_GRAY),
            (self.horizontalColorCheckBox, TRANSFORM_HORIZONTAL_COLOR),
            (self.blurGrayCheckBox, TRANSFORM_BLUR_GRAY),
            (self.blurColorCheckBox, TRANSFORM_BLUR_COLOR),
        ]
        self._wire_signals()
        self.pathLineEdit.setText(str(self.default_output_dir))
        self.timeLineEdit.setText("")
        self.blurGrayKernelLineEdit.setText("3")
        self.blurColorKernelLineEdit.setText("3")
        self._set_all_effects(True)
        self._log("Ready. Add BMP files or drag and drop them.")

    def _set_logos(self) -> None:
        logo_pixmap = QPixmap(str(self.logo_path))
        if logo_pixmap.isNull():
            return

        if hasattr(self, "topLogoLabel"):
            self.topLogoLabel.setPixmap(logo_pixmap.scaledToWidth(120))
        if hasattr(self, "bottomLogoLabel"):
            self.bottomLogoLabel.setPixmap(logo_pixmap.scaledToWidth(96))

    def _replace_drop_widget(self) -> None:
        placeholder = self.filesListWidget
        parent = placeholder.parent()
        layout = parent.layout()

        self.filesListWidget = DropListWidget(self._add_files, parent)
        self.filesListWidget.setObjectName("filesListWidget")
        self.filesListWidget.setStyleSheet(placeholder.styleSheet())
        self.filesListWidget.setMinimumSize(placeholder.minimumSize())
        self.filesListWidget.setMaximumSize(placeholder.maximumSize())
        self.filesListWidget.setSelectionMode(placeholder.selectionMode())

        idx = layout.indexOf(placeholder)
        layout.removeWidget(placeholder)
        placeholder.deleteLater()
        layout.insertWidget(idx, self.filesListWidget)

    def _wire_signals(self) -> None:
        self.executeButton.clicked.connect(self._run_filters)
        self.allButton.clicked.connect(self._select_all_mode)
        for checkbox, _transform in self.effect_widgets:
            checkbox.clicked.connect(self._sync_effect_state)

    def _append_log(self, text: str) -> None:
        self.logPlainTextEdit.appendPlainText(text)

    def _log(self, text: str) -> None:
        self.logPlainTextEdit.appendPlainText(text)

    def _set_all_effects(self, checked: bool) -> None:
        for checkbox, _transform in self.effect_widgets:
            checkbox.setChecked(checked)

    def _select_all_mode(self) -> None:
        self._set_all_effects(True)
        self._log("Todos los efectos seleccionados.")

    def _sync_effect_state(self) -> None:
        selected_count = len(self._selected_transforms())
        self._log(f"Efectos seleccionados: {selected_count}")

    def _selected_transforms(self) -> list[int]:
        return [transform for checkbox, transform in self.effect_widgets if checkbox.isChecked()]

    @staticmethod
    def _read_kernel_value(text: str, fallback: int = 3) -> int:
        stripped = text.strip()
        if not stripped:
            return fallback

        try:
            value = int(stripped)
        except ValueError as exc:
            raise ValueError(f"Invalid kernel value: {text}") from exc

        if value < 3 or value % 2 == 0:
            raise ValueError("Kernel size must be an odd integer >= 3")

        return value

    def _pick_files(self) -> None:
        files, _ = QFileDialog.getOpenFileNames(
            self,
            "Select BMP files",
            str(self.repo_root),
            "BMP files (*.bmp *.BMP)",
        )
        if files:
            self._add_files(files)

    def _pick_output(self) -> None:
        out = QFileDialog.getExistingDirectory(self, "Select output directory", self.pathLineEdit.text())
        if out:
            self.pathLineEdit.setText(out)

    def _open_output(self) -> None:
        out = Path(self.pathLineEdit.text()).expanduser().resolve()
        if not out.exists():
            QMessageBox.warning(self, "Missing Output", f"Directory does not exist:\n{out}")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(out)))

    def _add_files(self, files: Iterable[str]) -> None:
        seen = {p.resolve() for p in self.selected_files}
        added = 0
        skipped = 0

        for p in files:
            if len(self.selected_files) >= MAX_FILES:
                self._log(f"Reached maximum of {MAX_FILES} files.")
                break

            path = Path(p)
            if not path.is_file() or path.suffix.lower() != ".bmp":
                skipped += 1
                continue

            rp = path.resolve()
            if rp in seen:
                continue

            seen.add(rp)
            self.selected_files.append(rp)
            self.filesListWidget.addItem(QListWidgetItem(str(rp)))
            added += 1

        if added:
            self._log(f"Added {added} file(s). Total: {len(self.selected_files)}")
        if skipped:
            self._log(f"Skipped {skipped} non-BMP or invalid entries.")

    def _remove_selected(self) -> None:
        # Not shown in the screenshot-style UI, but kept for future Designer edits.
        rows = sorted((idx.row() for idx in self.filesListWidget.selectedIndexes()), reverse=True)
        for row in rows:
            self.filesListWidget.takeItem(row)
            del self.selected_files[row]
        if rows:
            self._log(f"Removed {len(rows)} file(s).")

    def _clear_files(self) -> None:
        self.filesListWidget.clear()
        self.selected_files.clear()
        self._log("Cleared file list.")

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

    def _validate_inputs(self) -> List[Path]:
        valid = [p for p in self.selected_files if self._is_supported_bmp_24(p)]
        if len(valid) < REQUIRED_IMAGE_COUNT:
            raise ValueError(
                f"Need at least {REQUIRED_IMAGE_COUNT} valid 24-bit BMP files. Found {len(valid)} valid."
            )
        return valid[:REQUIRED_IMAGE_COUNT]

    def _prepare_temp_input(self, files: List[Path]) -> Path:
        root = Path(tempfile.mkdtemp(prefix="img_analyzer_qt5_"))
        in_dir = root / "input"
        in_dir.mkdir(parents=True, exist_ok=True)

        for i, src in enumerate(files, start=1):
            dst = in_dir / f"img_{i:02d}_{src.name.replace(' ', '_')}"
            shutil.copy2(src, dst)

        return in_dir

    def _set_enabled(self, enabled: bool) -> None:
        self.executeButton.setEnabled(enabled)
        self.allButton.setEnabled(enabled)
        for checkbox, _transform in self.effect_widgets:
            checkbox.setEnabled(enabled)
        self.pathLineEdit.setEnabled(enabled)
        self.filesListWidget.setEnabled(enabled)

    def _write_report(
        self,
        output_dir: Path,
        used_files: List[Path],
        threads: int,
        exit_code: int,
        stdout_text: str,
        stderr_text: str,
    ) -> Path:
        report_path = output_dir / "gui_qt_designer_last_run.txt"
        lines = [
            f"timestamp={_dt.datetime.now().isoformat(timespec='seconds')}",
            f"threads={threads}",
            f"blur_kernel_gray={self.blurGrayKernelLineEdit.text().strip() or '3'}",
            f"blur_kernel_color={self.blurColorKernelLineEdit.text().strip() or '3'}",
            f"selected_transforms={','.join(str(transform) for transform in self.current_transforms)}",
            f"exit_code={exit_code}",
            "inputs:",
        ]
        lines.extend([f"- {p}" for p in used_files])
        lines.append("stdout:")
        lines.append(stdout_text.strip() or "<empty>")
        lines.append("stderr:")
        lines.append(stderr_text.strip() or "<empty>")

        report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return report_path

    def _run_filters(self) -> None:
        if self.process is not None:
            QMessageBox.information(self, "Busy", "A run is already in progress.")
            return

        if not self.binary_path.exists():
            QMessageBox.critical(self, "Missing Binary", f"Not found:\n{self.binary_path}\n\nRun make first.")
            return

        selected_transforms = self._selected_transforms()
        if not selected_transforms:
            QMessageBox.warning(self, "No effects", "Select at least one effect.")
            return

        try:
            blur_kernel_gray = self._read_kernel_value(self.blurGrayKernelLineEdit.text())
            blur_kernel_color = self._read_kernel_value(self.blurColorKernelLineEdit.text())
        except ValueError as exc:
            QMessageBox.warning(self, "Invalid kernel", str(exc))
            return

        try:
            chosen = self._validate_inputs()
        except ValueError as exc:
            QMessageBox.warning(self, "Invalid Inputs", str(exc))
            return

        out_dir = Path(self.pathLineEdit.text()).expanduser().resolve()
        out_dir.mkdir(parents=True, exist_ok=True)

        self.temp_input_dir = self._prepare_temp_input(chosen)
        threads = 6

        program = str(self.binary_path)
        args = [
            "--input-dir",
            str(self.temp_input_dir),
            "--output-dir",
            str(out_dir),
            "--threads",
            str(threads),
            "--transforms",
            ",".join(str(transform) for transform in selected_transforms),
            "--blur-kernel-gray",
            str(blur_kernel_gray),
            "--blur-kernel-color",
            str(blur_kernel_color),
        ]

        self.current_transforms = selected_transforms
        self.logPlainTextEdit.clear()
        self._log("=" * 60)
        self._log(f"Running: {program} {' '.join(args)}")
        self._log(f"Effects: {', '.join(str(transform) for transform in selected_transforms)}")

        self.process = QProcess(self)
        self.process.setProgram(program)
        self.process.setArguments(args)
        self.process.readyReadStandardOutput.connect(self._on_stdout)
        self.process.readyReadStandardError.connect(self._on_stderr)
        self.process.finished.connect(self._on_finished)

        self.run_started_at = perf_counter()
        self.timeLineEdit.setText("")
        self._set_enabled(False)
        self.executeButton.setText("Ejecutando...")
        self.process.start()

    def _on_stdout(self) -> None:
        if self.process is None:
            return
        txt = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if txt:
            self._log(txt.rstrip())

    def _on_stderr(self) -> None:
        if self.process is None:
            return
        txt = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace")
        if txt:
            self._log(txt.rstrip())

    def _on_finished(self, exit_code: int, _exit_status) -> None:
        if self.process is None:
            return

        out_dir = Path(self.pathLineEdit.text()).expanduser().resolve()
        threads = 6
        stdout_text = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        stderr_text = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace")

        used_files: List[Path] = []
        try:
            used_files = self._validate_inputs()
        except ValueError:
            pass

        report_path = self._write_report(out_dir, used_files, threads, exit_code, stdout_text, stderr_text)
        elapsed = perf_counter() - self.run_started_at

        if self.temp_input_dir is not None:
            shutil.rmtree(self.temp_input_dir.parent, ignore_errors=True)
            self.temp_input_dir = None

        if exit_code == 0:
            self.timeLineEdit.setText(f"{elapsed:.6f} s")
            self._log(f"Run complete. Report: {report_path}")
            QMessageBox.information(
                self,
                "Success",
                f"Done.\n\nResults: {out_dir / f'{threads}_threads'}\nReport: {report_path}",
            )
        else:
            self.timeLineEdit.setText("ERROR")
            self._log(f"Run failed ({exit_code}). Report: {report_path}")
            QMessageBox.warning(
                self,
                "Failure",
                f"Exit code {exit_code}.\nCheck logs and report:\n{report_path}",
            )

        self.process.deleteLater()
        self.process = None
        self.executeButton.setText("Ejecutar")
        self._set_enabled(True)


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("image_analyzer_qt5_designer")
    window = MainWindow()
    window.show()
    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
