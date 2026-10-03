# HOST-241 — `BlobBatch` (lote de blobs con estado fijo)

Cubre `eng/platform/amiga/blob_batch.hpp`: la generalización de `OrBlobBatch` a las
operaciones que comparten forma de registros (`Or`, `CookieCut`, `Opaque`). Fija el estado
común una vez (`BLTCON1`, ventanas, módulos) y, por objeto, escribe solo `BLTCON0` (con
`ASH`/`BSH`), los punteros y `BLTSIZE` — la estructura de `DrawObject` del `main.c` de
referencia.

Es host-testable porque la base de registros se inyecta como parámetro (un array local en el
test); no depende del backend ni del emulador.

## Qué comprueba

- **`begin`** deja `DMACON` con `SET` de `MASTER|BLITTER` (sin tocar `BLTPRI`), `BLTAFWM`/
  `BLTALWM` completos y los módulos `A/B/C/D` pedidos; no programa `BLTSIZE`.
- **`one`** cookie-cut: `BLTCON0 = ASH | A|B|C|D | $CA`, `BLTCON1 = BSH`, `BLTAPT` = máscara,
  `BLTBPT` = imagen, `BLTCPT`/`BLTDPT` = destino, `BLTSIZE = (height<<6)|words`.
- Un segundo blob con otro `shift` reescribe `BLTCON0`/`BLTCON1` sin tocar las constantes.
- **OR** (`$FC`, `B=D=destino`, sin `BSH`), **opaco** (`$F0`, sin máscara) y **clear** (`$00`,
  solo canal D) producen la secuencia de registros correcta.

## Ejecutar

```bash
bash ./tools/run-host-tests.sh tests/host/platform/amiga/241_blob_batch
```

## Referencias

- `engine/include/eng/platform/amiga/blob_batch.hpp`
- `engine/include/eng/platform/amiga/blob.hpp` (`OrBlobBatch`, HOST-176)
- `BartmanBasic/main.c` (bucle de BOBs cookie-cut `$CA`)
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (línea «copias, cookie-cut y fast blobs», punto 3)
