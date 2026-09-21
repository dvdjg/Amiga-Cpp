# HOST-258: cargador HUNK y detección de formato

Test host del cargador **HUNK** (`engine/include/eng/res/hunk.hpp`) y de la **detección de
formato** del `DynLoader` (`engine/include/eng/res/dynloader.hpp`), que acepta `.englib` y HUNK.

## Qué comprueba

1. `HunkImage::load` de un HUNK a mano (2 hunks: `HUNK_CODE` + `HUNK_DATA`) reserva los segmentos
   en una `LinearArena`, copia los datos y expone `hunk_count`, `hunk(i).type/size` y `entry()`.
2. **Relocación** `HUNK_RELOC32` aplicada: la celda del `code` pasa a valer la **base** del `data`.
3. **Relocación** `HUNK_RELOC32SHORT` (campos de 16 bits) aplicada.
4. **Símbolo** local (`HUNK_SYMBOL`): `symbol("foo")` = base del code; inexistente → `nullptr`.
   **Exports propios**: `export_count`/`export_hash`/`export_address` permiten **enumerar y ligar**
   los símbolos del módulo sin conocer los nombres (el pipeline los genera; no se depende de que un
   HUNK de terceros traiga `HUNK_SYMBOL`).
5. `DynLoader`: detecta HUNK (`format == Hunk`), resuelve símbolos y **falla sin `pool`**.
6. Imagen corta o `magic` inválido → `false` (estado `Error`).

No ejecuta código: el HUNK del test es un blob en RAM con una celda relocable y un símbolo.

## Salida de referencia

```
OK: HUNK (segmentos, relocaciones y simbolos) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/258_hunk_loader
```
