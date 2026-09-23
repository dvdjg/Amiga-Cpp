# HOST-176: lote de BOBs OR intercalado (`eng::amiga::OrBlobBatch`)

Test host de `engine/include/eng/platform/amiga/blob.hpp`: la secuencia de registros del
lote de BOBs OR intercalado portada de `DrawObject` de `effects/bobs3d`. El lote es
host-testable porque la base de registros se **inyecta como parámetro** (`begin(custom, ...)`);
aquí se pasa un array local y se comprueba lo que se programa.

## Qué comprueba

1. `begin`: `DMACON` (SET de MASTER|BLITTER, sin tocar `BLTPRI`), `BLTCON1=0` (corrige el bug
   BSH de bobs3d), `BLTAFWM/BLTALWM` completos, `BLTAMOD=0` (atlas denso), `BLTBMOD=BLTDMOD=26`.
2. `one`: `BLTCON0 = ASH | A|B|D | A_OR_B`, `BLTSIZE = (96<<6)|3`, y `BLTAPT/BLTBPT/BLTDPT`
   con el valor del puntero (extremo a extremo).
3. Que las constantes del lote **no cambian** entre BOBs y `end()` devuelve `true`.

Los offsets de palabras del array son los registros custom (`0x040/2`, `0x096/2`, …), de modo
que el test verifica el mismo contrato que ejecuta el hardware.

## Salida de referencia

```
OK: OrBlobBatch (secuencia de registros del lote) validado.
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/platform/amiga/176_or_blob_batch
```
