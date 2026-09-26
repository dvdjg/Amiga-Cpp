# HOST-360: contenedor AUZX

Test host de `engine/include/eng/audio/auzx.hpp`: cabecera de **32 bytes** (magic `AUZX`,
version, `compression`, rate, canales, bits, total de muestras, tamaño de chunk, nº de chunks,
offset de la tabla, offset de datos, checksum) + **índice de chunks** (entradas `{offset,size}`
de 8 bytes) para acceso aleatorio (precarga por trackloader).

## Qué comprueba

1. **Parseo** de un fichero sintético de 2 chunks: campos de cabecera y tamaños.
2. **Acceso a chunks**: `chunk(i)` devuelve el payload correcto; fuera de rango → vacío.
3. **Rechazos**: buffer corto, magic/versión/canales inválidos e índice que se sale del fichero.

## Salida de referencia

```
OK: contenedor AUZX (cabecera, indice de chunks y rechazos).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/360_auzx
```
