# Wiki de la interfaz y flujo de ejecución

## Objetivo de esta sección

Documentar cómo la interfaz gráfica (Qt Designer + PyQt5) se conecta con los procesos del backend en C para:

- recibir hasta 10 imágenes BMP por arrastre o selección,
- permitir seleccionar transformaciones,
- ejecutar el procesamiento por lotes,
- generar archivos de salida con acrónimos por transformación.

## Arquitectura general

- `ui/main_window.ui`: define la estructura visual (widgets, layouts, estilos, textos).
- `gui_qt_designer.py`: carga el `.ui` y conecta eventos de UI con funciones de lógica.
- `image_analyzer` (binario C): backend que procesa imágenes y genera salidas.

Flujo global:

1. Usuario agrega archivos (drag & drop o diálogo).
2. La GUI valida formato BMP soportado.
3. El usuario selecciona transformaciones y kernels.
4. La GUI construye y lanza comando del backend con `QProcess`.
5. El backend procesa y escribe resultados en `output/<threads>_threads/`.
6. La GUI muestra logs, tiempo total y reporte.

## Elementos gráficos y acciones que detonan

### Área de archivos

- `filesListWidget` (lista de archivos):
  - Permite arrastrar y soltar archivos.
  - Se reemplaza en runtime por `DropListWidget` en `MainWindow._replace_drop_widget()`.
- Drag & drop:
  - Lo maneja `DropListWidget` sobrescribiendo:
    - `dragEnterEvent`
    - `dragMoveEvent`
    - `dropEvent`
  - En `dropEvent` se llama `self._add_callback(files)`, que termina en `MainWindow._add_files(...)`.
- Validación inicial al agregar:
  - Se valida extensión `.bmp`.
  - Se evita duplicados.
  - Se respeta límite máximo `MAX_FILES = 10`.

### Área de transformaciones

- Checkboxes:
  - `verticalGrayCheckBox`
  - `verticalColorCheckBox`
  - `horizontalGrayCheckBox`
  - `horizontalColorCheckBox`
  - `blurGrayCheckBox`
  - `blurColorCheckBox`
- Mapeo a índices de backend (en `effect_widgets`):
  - `0`: horizontal grises (`hg`)
  - `1`: vertical grises (`vg`)
  - `2`: desenfoque grises (`dg`)
  - `3`: horizontal color (`hc`)
  - `4`: vertical color (`vc`)
  - `5`: desenfoque color (`dc`)
- Botón `allButton`:
  - Conectado a `MainWindow._select_all_mode()`.
  - Selecciona todas las transformaciones.

### Parámetros de ejecución

- `blurGrayKernelLineEdit` y `blurColorKernelLineEdit`:
  - Leídos y validados en `MainWindow._read_kernel_value(...)`.
  - Regla: entero impar mayor o igual a 3.
- `threadsSpinBox` o `threadsLineEdit` (si existe en UI):
  - Se toma en `MainWindow._run_filters()`.
  - Si no existe o es inválido, usa valor por defecto `6`.

### Ejecución y salida

- `executeButton`:
  - Conectado a `MainWindow._run_filters()`.
  - Dispara el flujo completo de validación y ejecución.
- `aboutButton`:
  - Conectado a `MainWindow._show_about_dialog()`.
  - Abre ventana de información del equipo.
- `logPlainTextEdit`:
  - Recibe trazas por `_log(...)`.
  - Muestra comando ejecutado, validaciones y salida del backend.
- `timeLineEdit`:
  - Muestra tiempo total de corrida en segundos.
  - Se llena en `_on_finished(...)` con `perf_counter()`.

## Conexión entre UI y funciones (signals/slots)

Se realiza en `MainWindow._wire_signals()`:

- `executeButton.clicked -> _run_filters`
- `allButton.clicked -> _select_all_mode`
- `aboutButton.clicked -> _show_about_dialog`
- `checkbox.clicked -> _sync_effect_state` (para cada checkbox de transformación)

## Flujo detallado de ejecución (`_run_filters`)

1. Verifica que no haya proceso en curso (`self.process is None`).
2. Verifica que exista el binario backend (`self.binary_path`).
3. Obtiene transformaciones seleccionadas (`_selected_transforms`).
4. Valida kernels (`_read_kernel_value`).
5. Valida entradas BMP (`_validate_inputs` + `_bmp_support_reason`).
6. Prepara carpeta temporal de entrada (`_prepare_temp_input`).
7. Construye argumentos:
   - `--input-dir`
   - `--output-dir`
   - `--threads`
   - `--transforms`
   - `--blur-kernel-gray`
   - `--blur-kernel-color`
8. Lanza backend con `QProcess`.
9. Lee stdout/stderr en `_on_stdout` / `_on_stderr`.
10. Al finalizar (`_on_finished`):
    - calcula tiempo total,
    - escribe reporte `output/gui_qt_designer_last_run.txt`,
    - muestra resultado en UI.

## Formato de salida y nomenclatura requerida

Cada imagen procesada conserva nombre original y agrega acrónimo:

- `Fotoa.bmp -> Fotoa_vg.bmp` (vertical grises)
- `Fotoa.bmp -> Fotoa_vc.bmp` (vertical color)
- `Fotoa.bmp -> Fotoa_hg.bmp` (horizontal grises)
- `Fotoa.bmp -> Fotoa_hc.bmp` (horizontal color)
- `Fotoa.bmp -> Fotoa_dg.bmp` (desenfoque grises)
- `Fotoa.bmp -> Fotoa_dc.bmp` (desenfoque color)

Las salidas se guardan en:

- `output/<threads>_threads/`

## Formatos BMP aceptados

La validación en GUI y backend permite:

- BMP 8-bit sin compresión.
- BMP 24-bit sin compresión.
- BMP 32-bit con compresión `0`, `3` o `6` (BI_RGB / BI_BITFIELDS / BI_ALPHABITFIELDS).

## Evidencia para video del entregable

https://youtu.be/nVW_H0ISGow

## Ejecucion en cluster con MPICH (multi-VM)

El backend ahora soporta ejecucion distribuida por MPI entre nodos y mantiene hilos (`pthread`) dentro de cada proceso.

### Requisitos en el cluster

1. Terminar la guia de MPICH cluster:
   https://help.ubuntu.com/community/MpichCluster
2. Tener MPICH instalado en todos los nodos.
3. Tener SSH sin password entre nodo maestro y workers.
4. Tener carpeta compartida (NFS) para que todos vean el mismo `input/` y `output/`.

### Pasos (maestro)

```bash
git clone <tu-repo>
cd image_analyzer
make
```

Crear archivo `hosts` (ejemplo):

```text
master slots=4
worker1 slots=4
worker2 slots=4
```

Ejecutar distribuido:

```bash
mpirun -f hosts -n 12 ./image_analyzer \
  --threads 4 \
  --input-dir input \
  --output-dir output \
  --transforms all \
  --blur-kernel-gray 5 \
  --blur-kernel-color 5
```

Notas:

- `-n` = procesos MPI totales en el cluster.
- `--threads` = hilos por proceso (paralelismo interno por nodo).
- El resumen y CSV los escribe el rank 0 (proceso lider).


## Referencias de código clave

- UI cargada desde Designer: `MainWindow.__init__ -> uic.loadUi(...)`
- Drag & drop: clase `DropListWidget`
- Conexión botones-funciones: `MainWindow._wire_signals()`
- Validación de archivos: `MainWindow._validate_inputs()` y `_bmp_support_reason()`
- Ejecución backend: `MainWindow._run_filters()`
- Fin de corrida: `MainWindow._on_finished()`
