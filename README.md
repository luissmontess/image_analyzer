# image_analyzer

Proyecto en C para procesar imagenes BMP grandes con una estrategia de paralelizacion a nivel de tareas.

## Que hace

El programa toma las primeras 3 imagenes `.bmp` encontradas en `input/` y genera 18 tareas:

- 3 imagenes
- 6 transformaciones por imagen

Las transformaciones implementadas son:

- inversion horizontal en escala de grises
- inversion vertical en escala de grises
- desenfoque 3x3 en escala de grises
- inversion horizontal a color
- inversion vertical a color
- desenfoque 3x3 a color

Cada tarea se asigna dinamicamente a un worker `pthread` hasta que no quedan tareas pendientes. Esto permite ejecutar el mismo experimento con `1`, `6`, `12` o `18` threads.

## Estructura

- `main.c`: carga imagenes, construye tareas, ejecuta el pool y escribe reportes
- `bmp.c` / `bmp.h`: lectura y escritura de BMP de 24 bits sin compresion
- `filters.c` / `filters.h`: transformaciones de color y grises
- `task_pool.c` / `task_pool.h`: cola simple compartida por indice con `pthread_mutex`
- `timing.c` / `timing.h`: medicion con `clock_gettime(CLOCK_MONOTONIC)`
- `run_experiments.sh`: automatiza las 4 corridas pedidas

## Soporte BMP

Se soportan BMP de 24 bits sin compresion. La salida se conserva tambien en formato BMP de 24 bits, incluso para grises, para mantener compatibilidad y simplicidad.

## Manejo de bordes en desenfoque

El desenfoque usa un kernel promedio 3x3:

```text
1 1 1
1 1 1
1 1 1
```

dividido entre el numero real de vecinos disponibles.

En bordes y esquinas se ignoran los vecinos fuera de rango. Eso evita lecturas invalidas y deja una politica consistente y documentada.

## Compilacion en Ubuntu

```bash
make
```

Comando equivalente manual:

```bash
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2 main.c bmp.c filters.c task_pool.c timing.c -o image_analyzer -pthread
```

## Ejecucion

Coloca al menos 3 imagenes BMP grandes en `input/`.

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
6,imagen1,inversion_horizontal_grises,input/imagen1.bmp,output/6_threads/imagen1__flip_h_gray.bmp,ok,0.812345
```

Formato de `experiment_report.csv`:

```csv
threads,total_time_seconds,speedup,efficiency
1,12.345678,1.000000,1.000000
6,3.210000,3.846005,0.641001
```

## Portabilidad Visual Studio / Ubuntu

El proyecto original estaba orientado a funciones sueltas y no tenia una solucion portable de paralelizacion. Esta version queda preparada para Ubuntu con `pthread`, que es el entorno mas directo para el requisito de paralelizacion a nivel de tareas.

Si despues quieres volver a usarlo en Visual Studio, la separacion modular permite sustituir el pool basado en `pthread` por otra API de threads manteniendo intactos los modulos de BMP, filtros, medicion y definicion de tareas.
