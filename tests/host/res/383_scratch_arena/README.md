# HOST-383 · scratch_arena (arena de scratch LIFO)

Test host de `eng::ScratchArena` (`eng/memory/arena.hpp`): la arena **temporal** de
fase/frame — *bump* con **marcadores** (`mark()`/`release(mark)`) que liberan un tramo
completo en orden inverso (LIFO).

## Qué valida

- `mark()` guarda el cursor; `release(mark)` libera **solo** el tramo desde ahí (anidable).
- Un nivel externo sobrevive al `release` del interno; re-reservar reutiliza el cursor.
- `release` de un marcador "futuro" es no-op (no avanza el cursor).
- `clear()` reinicia la scratch entera.
- `MemorySystem::reset_frame()` limpia **solo** `frame`; los bancos persistentes (`chip`/`slow`) no se tocan.

## Dos vidas útiles

- **Persistente** (assets, escena): `BlockPool` con `free` en cualquier orden (HOST-340).
- **Scratch de fase/frame**: `ScratchArena` con `mark`/`release` (este test).

Ver `MEMORY_OWNERSHIP.md` y `ROADMAP_MEMORY_OWNERSHIP.md` (Fase 6).
