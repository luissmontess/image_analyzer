#!/usr/bin/env python3
"""PyQt5 GUI for image_analyzer.

This app lets users drag and drop up to 10 BMP files, select multiple
transformations, configure blur kernels, and run the C backend.
"""

from __future__ import annotations

import datetime as _dt
import shutil
import struct
import sys
import tempfile
from pathlib import Path
from typing import Iterable, List

from PyQt5.QtCore import QProcess, QUrl, Qt
from PyQt5.QtGui import QDesktopServices
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import (
    QApplication,
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QTextEdit,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


MAX_GUI_IMAGES = 10
TRANSFORMS = [
    ("Inversi\u00f3n horizontal en grises", 0, "HG"),
    ("Inversi\u00f3n vertical en grises", 1, "VG"),
    ("Desenfoque en grises", 2, "DG"),
    ("Inversi\u00f3n horizontal a color", 3, "HC"),
    ("Inversi\u00f3n vertical a color", 4, "VC"),
    ("Desenfoque a color", 5, "DC"),
]


class DropListWidget(QListWidget):
    """List widget with file drag-and-drop support."""

    def __init__(self, add_callback, parent=None):
        super().__init__(parent)
        self._add_callback = add_callback
        self.setAcceptDrops(True)
        self.setDragEnabled(False)
        self.setSelectionMode(QListWidget.ExtendedSelection)

    def dragEnterEvent(self, event):  # noqa: N802 (Qt naming)
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(self, event):  # noqa: N802 (Qt naming)
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event):  # noqa: N802 (Qt naming)
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
        self.setWindowTitle("image_analyzer GUI")
        self.resize(1120, 760)

        self.repo_root = Path(__file__).resolve().parent
        self.binary_path = self.repo_root / "image_analyzer"
        self.default_output_dir = self.repo_root / "output"
        self.logo_path = self.repo_root / "logo.png"
        self.team_members = [
            "Luis Isaias Montes Rico",
            "Alonso Gabriel Lopez Baez",
            "Restituto Lara Lrios",
            "Jose Eduardo Puentes Martinez",
        ]

        self.process: QProcess | None = None
        self.temp_input_dir: Path | None = None
        self.selected_files: List[Path] = []
        self.stdout_buffer: List[str] = []
        self.stderr_buffer: List[str] = []
        self.control_widgets: List[QWidget] = []

        self._build_ui()

    def _build_ui(self) -> None:
        root = QWidget(self)
        self.setCentralWidget(root)

        main_layout = QVBoxLayout(root)
        header_row = QHBoxLayout()
        logo_label = QLabel()
        logo_pixmap = QPixmap(str(self.logo_path))
        if not logo_pixmap.isNull():
            logo_label.setPixmap(logo_pixmap.scaledToWidth(120))
        logo_label.setMinimumWidth(130)
        logo_label.setAlignment(Qt.AlignLeft | Qt.AlignTop)

        title_block = QVBoxLayout()
        subtitle.setWordWrap(True)
        subtitle.setStyleSheet("color: #555;")
        title_block.addWidget(title)
        title_block.addWidget(subtitle)

        header_row.addWidget(logo_label)
        header_row.addLayout(title_block)
        header_row.addStretch(1)
        main_layout.addLayout(header_row)

        top_layout = QHBoxLayout()
        main_layout.addLayout(top_layout)

        files_box = QGroupBox(self._files_box_title())
        files_layout = QVBoxLayout(files_box)
        self.file_list = DropListWidget(self._add_files)
        self.file_list.setMinimumHeight(310)
        self.file_list.setStyleSheet(
            "QListWidget {"
            "border: 2px dashed #7a8a9a;"
            "padding: 8px;"
            "background: #f7fbff;"
            "}"
        )
        files_layout.addWidget(self.file_list)

        file_buttons = QHBoxLayout()
        self.btn_add = QPushButton("Agregar archivos")
        self.btn_add.clicked.connect(self._pick_files)
        self.btn_remove = QPushButton("Quitar seleccionados")
        self.btn_remove.clicked.connect(self._remove_selected_files)
        self.btn_clear = QPushButton("Limpiar")
        self.btn_clear.clicked.connect(self._clear_files)
        file_buttons.addWidget(self.btn_add)
        file_buttons.addWidget(self.btn_remove)
        file_buttons.addWidget(self.btn_clear)
        files_layout.addLayout(file_buttons)
        top_layout.addWidget(files_box, 3)

        options_box = QGroupBox("Procesamiento")
        options_layout = QVBoxLayout(options_box)

        self.transform_checks: List[QCheckBox] = []
        transform_grid = QGridLayout()
        for index, (label, transform_index, acronym) in enumerate(TRANSFORMS):
            checkbox = QCheckBox(f"{label} ({acronym})")
            checkbox.setChecked(True)
            self.transform_checks.append(checkbox)
            self.control_widgets.append(checkbox)
            row = index // 2
            column = index % 2
            transform_grid.addWidget(checkbox, row, column)
        options_layout.addLayout(transform_grid)

        transform_buttons = QHBoxLayout()
        self.btn_select_all = QPushButton("Seleccionar todo")
        self.btn_select_all.clicked.connect(self._select_all_transforms)
        self.btn_clear_all = QPushButton("Deseleccionar todo")
        self.btn_clear_all.clicked.connect(self._clear_all_transforms)
        transform_buttons.addWidget(self.btn_select_all)
        transform_buttons.addWidget(self.btn_clear_all)
        options_layout.addLayout(transform_buttons)

        kernel_form = QFormLayout()
        self.gray_kernel_spin = QSpinBox()
        self.gray_kernel_spin.setRange(3, 99)
        self.gray_kernel_spin.setSingleStep(2)
        self.gray_kernel_spin.setValue(3)
        self.color_kernel_spin = QSpinBox()
        self.color_kernel_spin.setRange(3, 99)
        self.color_kernel_spin.setSingleStep(2)
        self.color_kernel_spin.setValue(3)
        kernel_form.addRow("Kernel desenfoque grises", self.gray_kernel_spin)
        kernel_form.addRow("Kernel desenfoque color", self.color_kernel_spin)
        options_layout.addLayout(kernel_form)

        run_form = QFormLayout()
        self.threads_spin = QSpinBox()
        self.threads_spin.setRange(1, 128)
        self.threads_spin.setValue(6)
        run_form.addRow("Threads", self.threads_spin)

        output_row = QHBoxLayout()
        self.output_edit = QLineEdit(str(self.default_output_dir))
        self.output_edit.setPlaceholderText("Directorio de salida")
        self.btn_output = QPushButton("Examinar")
        self.btn_output.clicked.connect(self._pick_output_dir)
        self.btn_open_output = QPushButton("Abrir salida")
        self.btn_open_output.clicked.connect(self._open_output_folder)
        output_row.addWidget(self.output_edit)
        output_row.addWidget(self.btn_output)
        output_row.addWidget(self.btn_open_output)
        run_form.addRow("Salida", output_row)
        options_layout.addLayout(run_form)

        self.btn_run = QPushButton("Procesar lote")
        self.btn_run.clicked.connect(self._run_filters)
        self.btn_about = QPushButton("Acerca de")
        self.btn_about.clicked.connect(self._show_about_dialog)
        options_layout.addWidget(self.btn_run)
        options_layout.addWidget(self.btn_about)

        top_layout.addWidget(options_box, 2)

        bottom_logo_row = QHBoxLayout()
        bottom_logo_row.addStretch(1)
        bottom_logo_label = QLabel()
        bottom_logo_pixmap = QPixmap(str(self.logo_path))
        if not bottom_logo_pixmap.isNull():
            bottom_logo_label.setPixmap(bottom_logo_pixmap.scaledToWidth(96))
        bottom_logo_label.setAlignment(Qt.AlignRight | Qt.AlignBottom)
        bottom_logo_row.addWidget(bottom_logo_label)
        main_layout.addLayout(bottom_logo_row)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setPlaceholderText("Los mensajes de ejecucion apareceran aqui...")
        main_layout.addWidget(self.log)

        self._append_log("Listo. Arrastra hasta 10 imagenes .bmp validas.")
        self._update_files_box_title()

        self.control_widgets.extend(
            [
                self.file_list,
                self.btn_add,
                self.btn_remove,
                self.btn_clear,
                self.btn_select_all,
                self.btn_clear_all,
                self.gray_kernel_spin,
                self.color_kernel_spin,
                self.threads_spin,
                self.output_edit,
                self.btn_output,
                self.btn_run,
                self.btn_about,
            ]
        )

    def _files_box_title(self) -> str:
        return f"Imagenes seleccionadas ({len(self.selected_files)}/{MAX_GUI_IMAGES})"

    def _update_files_box_title(self) -> None:
        for widget in self.findChildren(QGroupBox):
            if widget.title().startswith("Imagenes seleccionadas"):
                widget.setTitle(self._files_box_title())
                break

    def _append_log(self, text: str) -> None:
        self.log.appendPlainText(text)

    def _pick_files(self) -> None:
        files, _ = QFileDialog.getOpenFileNames(
            self,
            "Seleccionar BMP",
            str(self.repo_root),
            "BMP files (*.bmp *.BMP)",
        )
        if files:
            self._add_files(files)

    def _pick_output_dir(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "Seleccionar directorio de salida", self.output_edit.text())
        if folder:
            self.output_edit.setText(folder)

    def _select_all_transforms(self) -> None:
        for checkbox in self.transform_checks:
            checkbox.setChecked(True)

    def _clear_all_transforms(self) -> None:
        for checkbox in self.transform_checks:
            checkbox.setChecked(False)

    @staticmethod
    def _is_supported_bmp_24(path: Path) -> bool:
        try:
            with path.open("rb") as file_obj:
                header = file_obj.read(54)
            if len(header) < 54:
                return False
            if header[0:2] != b"BM":
                return False

            bits_per_pixel = struct.unpack_from("<H", header, 28)[0]
            compression = struct.unpack_from("<I", header, 30)[0]
            return bits_per_pixel == 24 and compression == 0
        except OSError:
            return False

    def _add_files(self, files: Iterable[str]) -> None:
        seen_paths = {item.resolve() for item in self.selected_files}
        seen_names = {item.name.lower() for item in self.selected_files}
        added = 0
        rejected = 0

        for file_path in files:
            if len(self.selected_files) >= MAX_GUI_IMAGES:
                rejected += 1
                continue

            candidate = Path(file_path)
            if not candidate.exists() or not candidate.is_file():
                rejected += 1
                continue
            if candidate.suffix.lower() != ".bmp":
                rejected += 1
                continue
            if not self._is_supported_bmp_24(candidate):
                rejected += 1
                continue

            normalized = candidate.resolve()
            if normalized in seen_paths:
                continue
            if candidate.name.lower() in seen_names:
                rejected += 1
                self._append_log(
                    f"Nombre duplicado rechazado para mantener salidas unicas: {candidate.name}"
                )
                continue

            self.selected_files.append(normalized)
            seen_paths.add(normalized)
            seen_names.add(candidate.name.lower())
            self.file_list.addItem(QListWidgetItem(str(normalized)))
            added += 1

        if added:
            self._append_log(f"Agregadas {added} imagen(es). Total: {len(self.selected_files)}")
        if rejected:
            self._append_log(f"Se descartaron {rejected} archivo(s) no validos o por exceso de cupo.")
        self._update_files_box_title()

    def _remove_selected_files(self) -> None:
        rows = sorted({index.row() for index in self.file_list.selectedIndexes()}, reverse=True)
        if not rows:
            return

        for row in rows:
            self.file_list.takeItem(row)
            del self.selected_files[row]

        self._append_log(f"Eliminadas {len(rows)} imagen(es). Total: {len(self.selected_files)}")
        self._update_files_box_title()

    def _clear_files(self) -> None:
        self.file_list.clear()
        self.selected_files.clear()
        self._append_log("Lista de imagenes limpiada.")
        self._update_files_box_title()

    def _open_output_folder(self) -> None:
        out_dir = Path(self.output_edit.text()).expanduser().resolve()
        if not out_dir.exists():
            QMessageBox.warning(self, "Salida no encontrada", f"El directorio no existe:\n{out_dir}")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(out_dir)))

    def _show_about_dialog(self) -> None:
        dialog = QDialog(self)
        dialog.setWindowTitle("Acerca de")
        dialog.resize(720, 360)

        outer = QVBoxLayout(dialog)
        content = QHBoxLayout()

        text_box = QTextEdit()
        text_box.setReadOnly(True)
        text_box.setPlainText(
            "Integrantes del equipo:\n\n" + "\n".join(f"- {member}" for member in self.team_members)
        )

        logo_label = QLabel()
        logo_pixmap = QPixmap(str(self.logo_path))
        if not logo_pixmap.isNull():
            logo_label.setPixmap(logo_pixmap.scaledToWidth(220))
        logo_label.setAlignment(Qt.AlignCenter)

        content.addWidget(text_box, 3)
        content.addWidget(logo_label, 2)

        buttons = QDialogButtonBox(QDialogButtonBox.Close)
        buttons.rejected.connect(dialog.reject)

        outer.addLayout(content)
        outer.addWidget(buttons)
        dialog.exec_()

    def _selected_transform_indices(self) -> List[int]:
        selected = []
        for checkbox, (_, transform_index, _) in zip(self.transform_checks, TRANSFORMS):
            if checkbox.isChecked():
                selected.append(transform_index)
        return selected

    def _validate_inputs(self) -> List[Path]:
        if not self.selected_files:
            raise ValueError("Debes seleccionar al menos una imagen BMP valida.")
        if len(self.selected_files) > MAX_GUI_IMAGES:
            raise ValueError(f"Solo se permiten hasta {MAX_GUI_IMAGES} imagenes.")

        selected_transforms = self._selected_transform_indices()
        if not selected_transforms:
            raise ValueError("Debes seleccionar al menos una transformacion.")

        return self.selected_files[:]

    def _prepare_temp_input(self, files: List[Path]) -> Path:
        temp_root = Path(tempfile.mkdtemp(prefix="image_analyzer_gui_"))
        in_dir = temp_root / "input"
        in_dir.mkdir(parents=True, exist_ok=True)

        for src in files:
            dst = in_dir / src.name
            shutil.copy2(src, dst)

        return in_dir

    def _write_txt_report(
        self,
        output_dir: Path,
        used_files: List[Path],
        threads: int,
        selected_transforms: List[int],
        blur_gray: int,
        blur_color: int,
        exit_code: int,
        stdout_text: str,
        stderr_text: str,
    ) -> Path:
        report_path = output_dir / "gui_last_run.txt"
        timestamp = _dt.datetime.now().isoformat(timespec="seconds")

        lines = [
            f"timestamp={timestamp}",
            f"threads={threads}",
            f"images={len(used_files)}",
            f"selected_transforms={','.join(str(item) for item in selected_transforms)}",
            f"blur_kernel_gray={blur_gray}",
            f"blur_kernel_color={blur_color}",
            f"exit_code={exit_code}",
            "inputs:",
        ]
        lines.extend(f"- {path}" for path in used_files)
        lines.append("stdout:")
        lines.append(stdout_text.strip() or "<empty>")
        lines.append("stderr:")
        lines.append(stderr_text.strip() or "<empty>")

        report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return report_path

    def _set_controls_enabled(self, enabled: bool) -> None:
        for widget in self.control_widgets:
            widget.setEnabled(enabled)

    def _run_filters(self) -> None:
        if self.process is not None:
            QMessageBox.information(self, "Ocupado", "Ya hay un proceso ejecutandose.")
            return

        if not self.binary_path.exists():
            QMessageBox.critical(
                self,
                "Binario no encontrado",
                f"No se encontro el binario C en:\n{self.binary_path}\n\nEjecuta 'make' primero.",
            )
            return

        try:
            chosen_inputs = self._validate_inputs()
        except ValueError as exc:
            QMessageBox.warning(self, "Entradas invalidas", str(exc))
            return

        selected_transforms = self._selected_transform_indices()
        out_dir = Path(self.output_edit.text()).expanduser().resolve()
        out_dir.mkdir(parents=True, exist_ok=True)
        self.temp_input_dir = self._prepare_temp_input(chosen_inputs)

        threads = self.threads_spin.value()
        blur_gray = self.gray_kernel_spin.value()
        blur_color = self.color_kernel_spin.value()

        program = str(self.binary_path)
        args = [
            "--input-dir",
            str(self.temp_input_dir),
            "--output-dir",
            str(out_dir),
            "--threads",
            str(threads),
            "--transforms",
            ",".join(str(index) for index in selected_transforms),
            "--blur-kernel-gray",
            str(blur_gray),
            "--blur-kernel-color",
            str(blur_color),
        ]

        self.stdout_buffer = []
        self.stderr_buffer = []
        self._append_log("=" * 70)
        self._append_log(f"Ejecutando: {program} {' '.join(args)}")
        self._append_log("Imagenes seleccionadas:")
        for path in chosen_inputs:
            self._append_log(f"  - {path}")
        self._append_log(
            "Transformaciones: "
            + ", ".join(
                f"{label} ({acronym})"
                for label, transform_index, acronym in TRANSFORMS
                if transform_index in selected_transforms
            )
        )

        self.process = QProcess(self)
        self.process.setProgram(program)
        self.process.setArguments(args)
        self.process.finished.connect(self._on_process_finished)
        self.process.readyReadStandardOutput.connect(self._on_stdout)
        self.process.readyReadStandardError.connect(self._on_stderr)

        self._set_controls_enabled(False)
        self.process.start()

    def _on_stdout(self) -> None:
        if self.process is None:
            return
        text = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if text:
            self.stdout_buffer.append(text)
            self._append_log(text.rstrip())

    def _on_stderr(self) -> None:
        if self.process is None:
            return
        text = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace")
        if text:
            self.stderr_buffer.append(text)
            self._append_log(text.rstrip())

    def _on_process_finished(self, exit_code: int, _exit_status) -> None:
        if self.process is None:
            return

        out_dir = Path(self.output_edit.text()).expanduser().resolve()
        threads = self.threads_spin.value()
        blur_gray = self.gray_kernel_spin.value()
        blur_color = self.color_kernel_spin.value()
        selected_transforms = self._selected_transform_indices()

        stdout_text = "".join(self.stdout_buffer)
        stderr_text = "".join(self.stderr_buffer)
        report_path = self._write_txt_report(
            output_dir=out_dir,
            used_files=self.selected_files[:],
            threads=threads,
            selected_transforms=selected_transforms,
            blur_gray=blur_gray,
            blur_color=blur_color,
            exit_code=exit_code,
            stdout_text=stdout_text,
            stderr_text=stderr_text,
        )

        if self.temp_input_dir is not None:
            shutil.rmtree(self.temp_input_dir.parent, ignore_errors=True)
            self.temp_input_dir = None

        if exit_code == 0:
            self._append_log(f"Ejecucion completada. Reporte TXT: {report_path}")
            QMessageBox.information(
                self,
                "Listo",
                f"Procesamiento completado.\n\nResultados: {out_dir / f'{threads}_threads'}\nReporte: {report_path}",
            )
        else:
            self._append_log(f"Fallo la ejecucion con codigo {exit_code}. Reporte: {report_path}")
            QMessageBox.warning(
                self,
                "Fallo",
                f"El backend C retorno el codigo {exit_code}.\nRevisa los logs y el reporte:\n{report_path}",
            )

        self.process.deleteLater()
        self.process = None
        self._set_controls_enabled(True)


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("image_analyzer_gui")
    window = MainWindow()
    window.show()
    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
