# Ejecucion en cluster MPI heterogeneo (ARM64 + x86_64)

## Arquitectura del cluster
- Usuario MPI: `mpiu`
- NFS compartido: `/mirror`
- Proyecto: `/mirror/image_parallel`
- Master: `ub0`
- Nodos actuales:
1. `ub0` - ARM64/aarch64 (Ubuntu 24.04)
2. `ub1` - ARM64/aarch64 (Ubuntu 24.04)
3. `ub2` - x86_64 (Ubuntu 24.04)
- Nodo futuro: `ub3` - x86_64
- MPI requerido (custom): `/opt/mpich-4.2.0`

## Reglas importantes
- No usar OpenMPI.
- No usar MPICH de `apt`.
- Compilar y ejecutar con `/opt/mpich-4.2.0`.
- Antes de compilar o correr:

```bash
source /etc/profile.d/mpich-custom.sh
```

## Estructura esperada en NFS
- Input: `/mirror/image_parallel/input`
- Output: `/mirror/image_parallel/output`
- Logs por rank: `/mirror/image_parallel/logs`

El programa crea `output/` y `logs/` si no existen.

## Compilacion (C) por arquitectura

### ARM (desde ub0 o ub1)
```bash
source /etc/profile.d/mpich-custom.sh
cd /mirror/image_parallel
make clean
make build-arm CC=/opt/mpich-4.2.0/bin/mpicc
```
Genera: `/mirror/image_parallel/image_processor_arm`

### x86_64 (desde ub2)
```bash
source /etc/profile.d/mpich-custom.sh
cd /mirror/image_parallel
make clean
make build-x86 CC=/opt/mpich-4.2.0/bin/mpicc
```
Genera: `/mirror/image_parallel/image_processor_x86`

## Wrapper por arquitectura
Archivo: `/mirror/image_parallel/run_arch.sh`
- Detecta `uname -m`
- Ejecuta `image_processor_arm` en `aarch64`
- Ejecuta `image_processor_x86` en `x86_64`

## Ejecucion MPI mixta
```bash
source /etc/profile.d/mpich-custom.sh
cd /mirror/image_parallel

cat > mixed_hosts <<'EOF_HOSTS'
ub0:2
ub1:2
ub2:4
EOF_HOSTS

/opt/mpich-4.2.0/bin/mpiexec -bootstrap ssh -n 8 -f mixed_hosts /mirror/image_parallel/run_arch.sh
```

Opcional (hilos por rank y transforms):
```bash
/opt/mpich-4.2.0/bin/mpiexec -bootstrap ssh -n 8 -f mixed_hosts \
  /mirror/image_parallel/run_arch.sh --threads 2 --transforms all
```

## Como agregar ub3
1. Asegurar SSH sin password de `ub0` a `ub3`.
2. Instalar mismo MPICH custom en `ub3` en `/opt/mpich-4.2.0`.
3. Compilar binario x86 en `ub3` o copiar `image_processor_x86` validado.
4. Actualizar machinefile:

```text
ub0:2
ub1:2
ub2:4
ub3:4
```

5. Ajustar `-n` al total de slots.

## Logging y salida
Cada rank imprime y registra:
- rank
- size total
- hostname
- arquitectura
- imagenes asignadas
- tiempo del rank

Logs por rank:
- `/mirror/image_parallel/logs/rank_<rank>_<hostname>.log`

Resultados de imagen:
- `/mirror/image_parallel/output/<threads>_threads/*.bmp`

## Troubleshooting

### Permission denied en NFS
- Verificar permisos/ownership de `/mirror/image_parallel` para usuario `mpiu`.
- Probar crear archivo manual desde cada nodo en `input`, `output`, `logs`.

### host key changed
- Limpiar host key vieja en `ub0`:
```bash
ssh-keygen -R ubX
ssh mpiu@ubX
```

### rank 0 of 1
- Se ejecuto sin `mpiexec` o con `-n 1`.
- Validar comando y machinefile.
- Confirmar que usas `/opt/mpich-4.2.0/bin/mpiexec`.

### wrong architecture executable
- Si aparece `Exec format error`, el nodo intento correr binario de otra arquitectura.
- Validar `run_arch.sh` y existencia de ambos binarios en `/mirror/image_parallel`.

### mpiexec incorrecto
- Ejecutar:
```bash
which mpiexec
/opt/mpich-4.2.0/bin/mpiexec -version
```
- Debe resolver a `/opt/mpich-4.2.0/bin/mpiexec`.

### librerias MPI incorrectas
- Validar wrapper de compilacion:
```bash
which mpicc
/opt/mpich-4.2.0/bin/mpicc -show
```
- Recompilar siempre con `CC=/opt/mpich-4.2.0/bin/mpicc`.
