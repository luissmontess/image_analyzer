# image_analyzer

Proyecto en C para procesar imagenes BMP grandes con una estrategia de paralelizacion a nivel de datos.

## Que hace

El programa procesa hasta 10 imagenes `.bmp` encontradas en `input/` y genera una tarea por cada combinacion imagen-transformacion.

Las transformaciones implementadas son:

- inversion horizontal en escala de grises
- inversion vertical en escala de grises
- desenfoque 3x3 en escala de grises
- inversion horizontal a color
- inversion vertical a color
- desenfoque 3x3 a color

El desenfoque usa un kernel cuadrado configurable de tamano impar, con valor por defecto `3`.

Cada tarea se ejecuta de forma secuencial, pero su procesamiento interno se divide por bloques de filas de la imagen entre `pthread` workers. Esto permite ejecutar el mismo experimento con `1`, `6`, `12` o `18` threads y medir el impacto de paralelizar los datos de cada transformacion.

## Estructura

- `main.c`: carga imagenes, construye tareas, ejecuta el pool y escribe reportes
- `bmp.c` / `bmp.h`: lectura y escritura de BMP de 24 bits sin compresion
- `filters.c` / `filters.h`: transformaciones de color y grises
- `task_pool.c` / `task_pool.h`: ejecucion de tareas y despacho de transformaciones paralelas por datos
- `timing.c` / `timing.h`: medicion con `clock_gettime(CLOCK_MONOTONIC)`
- `run_experiments.sh`: automatiza las 4 corridas pedidas

## Soporte BMP

Se soportan BMP de 24 bits sin compresion. La salida se conserva tambien en formato BMP de 24 bits, incluso para grises, para mantener compatibilidad y simplicidad.

## Manejo de bordes en desenfoque

El desenfoque usa un kernel promedio 3x3:

1 1 1
1 1 1
```

```bash
make
```

Comando equivalente manual:

```bash
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2 main.c bmp.c filters.c task_pool.c timing.c -o image_analyzer -pthread
```

## GUI en Python (Web, recomendado)

Si quieres GUI sin depender de PyQt5/Tkinter, usa la interfaz web local:

Seleccionar efectos desde la linea de comandos:

```bash
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms 0,2,5
```

Tambien puedes controlar el kernel de desenfoque:

```bash
./image_analyzer --input-dir input --output-dir output --threads 6 --transforms 2,5 --blur-kernel-gray 5 --blur-kernel-color 7
```

Indices de transformacion:

- `0`: inversion horizontal en escala de grises
- `1`: inversion vertical en escala de grises
- `2`: desenfoque 3x3 en escala de grises
- `3`: inversion horizontal a color
- `4`: inversion vertical a color
- `5`: desenfoque 3x3 a color
```bash
python3 gui_web.py
```

Luego abre en tu navegador:

```text
http://127.0.0.1:8765
```

Esta GUI permite arrastrar y soltar BMP, configurar threads y ejecutar el backend C.

## Otras GUIs (opcionales)

Si quieres interfaz grafica de escritorio, hay dos opciones:

### Opcion Qt5 Designer (la que pediste)

Archivos:

- [ui/main_window.ui](ui/main_window.ui) (estructura tipo la captura)
- [gui_qt_designer.py](gui_qt_designer.py) (carga el `.ui` y conecta la logica)

Comandos de instalacion (elige uno):

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install -y python3-pyqt5 qttools5-dev-tools
```

```bash
# Pip
python3 -m pip install PyQt5 pyqt5-tools
```

Abrir la interfaz en Designer para editarla:

```bash
python3 gui_qt_designer.py
```

La GUI muestra campos para el kernel del desenfoque y envia esos valores al backend C.

La documentacion detallada de la interfaz y sus eventos esta en [wiki.md](wiki.md).

### Opcion 1: Tkinter (recomendado - sin dependencias)

Usa `gui_app_tk.py`. No necesita instalar nada exterior (viene con Python).

```bash
python3 gui_app_tk.py
```

### Opcion 2: PyQt5 (mas moderna)

Usa `gui_app.py` si quieres interfaz mas moderna.

```bash
python3 -m pip install PyQt5
python3 gui_app.py
```

Ambas GUIs integran con el backend en C asi:
- permite arrastrar y soltar archivos `.bmp`
- valida que sean BMP de 24 bits sin compresion (como exige `image_analyzer`)
- toma hasta 10 imagenes validas
- ejecuta `./image_analyzer` por `subprocess`
- guarda un reporte rapido en texto: `output/gui_last_run.txt`

Notas:

- compila primero el backend: `make`
- si arrastras BMP de 32 bits, la GUI los descarta como no soportados
- si repites nombres de archivo, la GUI rechaza el duplicado para mantener salidas unicas
- la salida de imagenes sigue en `output/<threads>_threads/`

## Ejecucion

Coloca entre 1 y 10 imagenes BMP grandes en `input/`.

Ejecutar una corrida:

```bash
./image_analyzer --input-dir input --output-dir output --threads 1
./image_analyzer --input-dir input --output-dir output --threads 6
./image_analyzer --input-dir input --output-dir output --threads 12
./image_analyzer --input-dir input --output-dir output --threads 18
```

Ejecutar el experimento completo:

```bash
chmod +x run_experiments.sh
./run_experiments.sh input output
```

## Salidas

Las imagenes procesadas se guardan en subdirectorios como:

```text
output/1_threads/
output/6_threads/
output/12_threads/
output/18_threads/
```

Archivos de resultados:

- `output/summary_runs.csv`
- `output/task_runs.csv`
- `output/experiment_report.csv`

Formato de `summary_runs.csv`:

```csv
threads,image_count,task_count,successful_tasks,total_time_seconds
1,3,18,18,12.345678
```

Formato de `task_runs.csv`:

```csv
threads,image,transform,input_path,output_path,status,task_time_seconds
6,imagen1,inversion_horizontal_grises,input/imagen1.bmp,output/6_threads/imagen1_hg.bmp,ok,0.812345
```

Formato de `experiment_report.csv`:

```csv
threads,total_time_seconds,speedup,efficiency
1,12.345678,1.000000,1.000000
6,3.210000,3.846005,0.641001
```

## Portabilidad Visual Studio / Ubuntu

El proyecto original estaba orientado a funciones sueltas y no tenia una solucion portable de paralelizacion. Esta version queda preparada para Ubuntu con `pthread`, que es el entorno mas directo para el requisito de paralelizacion a nivel de datos.

Si despues quieres volver a usarlo en Visual Studio, la separacion modular permite sustituir el pool basado en `pthread` por otra API de threads manteniendo intactos los modulos de BMP, filtros, medicion y definicion de tareas.
