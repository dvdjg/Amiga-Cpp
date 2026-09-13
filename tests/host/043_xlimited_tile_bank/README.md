# HOST-043 — `XlimitedTileBank` (banco propio o aliaseado)

Valida el descriptor `eng::field::XlimitedTileBank` (`engine/include/eng/field/xlimited_scene.hpp`),
que representa un banco de bloques X-Limited que puede ser **propio** (reservado en Chip RAM por
los builders) o **aliaseado** a un `incbin` de solo lectura.

## Qué fija

- Transporta la vista de dominio (`eng::TileBankBytes`) y el `MemoryKind`, de modo que la escena ya
  no guarda un `MemoryBlock` crudo para el banco (regla «el campo nace etiquetado»).
- `valid()` y `words()`: las words interleaved que consume el blit `draw_block`; `words()` es la
  **misma** memoria reinterpretada a `u16` (sin copia; comprobación byte a byte, endian-neutral).
- El caso **propio** reserva con `allocate_block<TileBankTag>()` y propaga el `kind` de la arena
  (Chip/Slow), que se conserva en el descriptor.

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/043_xlimited_tile_bank
```

## Relación

- `INTERNAL_TYPE_SYSTEM.md` §1 (campo etiquetado desde el origen) y `CODING_STYLE.md`.
- Uso real: `XlimitedScene` (demos 107/110/111/112/201/202).
