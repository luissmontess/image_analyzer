# Wiki de la interfaz y flujo de ejecución

## Objetivo de esta sección

Documentar cómo la interfaz gráfica (Qt Designer + PyQt5) y el backend en C trabajan sobre las carpetas del proyecto para:

- usar imágenes BMP ubicadas dentro del proyecto, especialmente en `input/`,
- recibir hasta 10 imágenes BMP desde la GUI por arrastre,
- validar que los BMP sean soportados antes de ejecutarlos,
- permitir seleccionar transformaciones y kernels de desenfoque,
- ejecutar el procesamiento por lotes mediante `QProcess`,
- generar archivos de salida en `output/<threads>_threads/`.

## Arquitectura general

- `ui/main_window.ui`: define la estructura visual (widgets, layouts, estilos, textos).
- `gui_qt_designer.py`: carga el `.ui` y conecta eventos de UI con funciones de lógica.
- `image_analyzer` (binario C): backend que procesa imágenes y genera salidas.

Flujo global:

1. Las imágenes BMP de prueba se colocan en `input/`.
2. En la GUI, el usuario puede arrastrar hasta 10 imágenes BMP desde esa carpeta.
3. La GUI valida extensión y formato BMP soportado.
4. El usuario selecciona transformaciones y kernels.
5. La GUI copia los archivos válidos a una carpeta temporal de entrada.
6. La GUI construye y lanza comando del backend con `QProcess`.
7. El backend procesa y escribe resultados en `output/<threads>_threads/`.
8. La GUI muestra logs, tiempo total y reporte.

## Alcance del proyecto

La interfaz gráfica está pensada para el flujo de demostración del entregable y limita la carga a un máximo de 10 imágenes BMP por corrida.

El backend por terminal trabaja por carpeta: procesa los archivos `.bmp` encontrados en `--input-dir` y no impone el límite de 10 imágenes de la GUI. En ejecución MPI, el conjunto de imágenes se reparte entre los ranks disponibles, y cada rank registra en logs qué computadora procesó qué imágenes.

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

## Formato de salida y nomenclatura

El backend genera cada salida usando el nombre base del archivo que recibe y agrega el acrónimo de la transformación:

- `Fotoa.bmp -> Fotoa_vg.bmp` (vertical grises)
- `Fotoa.bmp -> Fotoa_vc.bmp` (vertical color)
- `Fotoa.bmp -> Fotoa_hg.bmp` (horizontal grises)
- `Fotoa.bmp -> Fotoa_hc.bmp` (horizontal color)
- `Fotoa.bmp -> Fotoa_dg.bmp` (desenfoque grises)
- `Fotoa.bmp -> Fotoa_dc.bmp` (desenfoque color)

En ejecuciones directas por terminal, el backend usa los nombres tal como existen dentro de `--input-dir`.

En ejecuciones desde la GUI, antes de llamar al backend se copian los archivos válidos a una carpeta temporal con prefijo de orden:

- `Fotoa.bmp -> img_01_Fotoa.bmp`
- salida generada: `img_01_Fotoa_vg.bmp`, `img_01_Fotoa_hc.bmp`, etc.

Este prefijo ayuda a mantener un orden estable en el lote y evitar choques entre archivos con nombres repetidos.

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
- Cada rank escribe su propio log en `logs/rank_<rank>_<hostname>.log`.
- Los logs permiten verificar qué computadora procesó cada parte del trabajo, ya que incluyen rank, hostname, arquitectura, imágenes asignadas, transformaciones ejecutadas, rutas de salida y tiempos.
- El rank 0 imprime el resumen total en stdout al finalizar la corrida.


## Referencias de código clave

- UI cargada desde Designer: `MainWindow.__init__ -> uic.loadUi(...)`
- Drag & drop: clase `DropListWidget`
- Conexión botones-funciones: `MainWindow._wire_signals()`
- Validación de archivos: `MainWindow._validate_inputs()` y `_bmp_support_reason()`
- Ejecución backend: `MainWindow._run_filters()`
- Fin de corrida: `MainWindow._on_finished()`
