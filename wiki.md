# Wiki de la interfaz

## Objetivo

La aplicacion grafica permite arrastrar hasta 10 imagenes BMP de 24 bits sin compresion, elegir una o varias transformaciones, definir el tamano del kernel de desenfoque y ejecutar el backend en C.

## Elementos graficos

### Area de imagenes

- `Lista de imagenes`: muestra los archivos BMP seleccionados.
- `Agregar archivos`: abre un dialogo para escoger imagenes desde disco.
- `Quitar seleccionados`: elimina los elementos marcados de la lista.
- `Limpiar`: vacia por completo la lista.

Accion que detona:
- Al agregar una imagen valida, la GUI la copia a una carpeta temporal de trabajo.
- Si el archivo no es BMP 24-bit o se excede el limite de 10, se descarta.
- Si el nombre del archivo ya existe en la seleccion, se rechaza para evitar colisiones en la salida.

### Area de procesamiento

- `Checkboxes de transformacion`: permiten seleccionar mas de un efecto al mismo tiempo.
- `Seleccionar todo`: marca las 6 transformaciones.
- `Deseleccionar todo`: limpia la seleccion.
- `Kernel desenfoque grises`: define el tamano del kernel para la transformacion de grises.
- `Kernel desenfoque color`: define el tamano del kernel para la transformacion a color.
- `Threads`: define el numero de hilos enviados al backend.

Accion que detona:
- Los checkboxes se convierten en `--transforms`.
- Los kernels se envian como `--blur-kernel-gray` y `--blur-kernel-color`.
- El boton de ejecucion arma la linea de comando y lanza el binario C.

### Area de salida

- `Directorio de salida`: ruta donde se guardan las imagenes procesadas.
- `Examinar`: abre un selector de carpetas.
- `Abrir salida`: abre el directorio destino en el explorador.
- `Procesar lote`: ejecuta el backend.
- `Log`: muestra el estado de la corrida y los mensajes del backend.

Accion que detona:
- Al terminar la ejecucion, la GUI genera un reporte de texto en `output/gui_last_run.txt`.

## Flujo de datos

1. El usuario arrastra o selecciona entre 1 y 10 BMP validos.
2. La GUI copia esas imagenes a una carpeta temporal conservando el nombre original.
3. La GUI llama a `./image_analyzer` con la lista de transformaciones y los kernels seleccionados.
4. El backend procesa cada imagen y guarda resultados en `output/<threads>_threads/`.
5. Los archivos generados siguen el esquema `NombreOriginal_acronimo.bmp`.

## Acronomos de salida

- `hg`: inversion horizontal en grises
- `vg`: inversion vertical en grises
- `dg`: desenfoque en grises
- `hc`: inversion horizontal a color
- `vc`: inversion vertical a color
- `dc`: desenfoque a color

Ejemplo:
- `Fotoa.bmp` -> `Fotoa_vg.bmp`
- `Fotoa.bmp` -> `Fotoa_vc.bmp`

## Video demostrativo

Esta seccion debe enlazar el video final de demostracion del proyecto.

Sugerencia:
- subir el video a una ruta fija del repositorio o a un enlace compartible
- documentar en el README la ubicacion exacta del video
