# HOST-431 — secuenciador de valores y eventos (`eng/core/util/sequence.hpp`)

Valida la línea temporal genérica de F8 del `ROADMAP_JUEGO_SPRITES_BOBS.md` §5:

- `KeyTrack`: interpolación por claves con `Ease` (`Step`, `Linear`, `EaseIn`), retención antes
  de la primera y después de la última clave, orden y capacidad; ejercitada con **`float` y
  `Fixed<s16,12>`** (la cabecera no fija escalar).
- `EventTrack`: eventos `(tick, id)` en orden, con rechazo fuera de orden y de capacidad.
- `Sequence` + `SequenceRunner`: ventanas de eventos por `advance`, orden por pista, cruce del
  final sin `loop` (`finished`), `loop` con una vuelta por avance y `seek` (depuración).

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/core/431_sequence
```
