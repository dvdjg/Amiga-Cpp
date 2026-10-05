# Referencia — `BlockPool`

`eng::BlockPool` (= `BlockPoolT<64>`, `engine/include/eng/memory/block_pool.hpp:38`) es el asignador **persistente** del engine: un *first-fit* **con `free`** y fusión de huecos, sobre un buffer del llamador y **sin heap**. A diferencia de la arena *bump* (`LinearArena`, LIFO y solo scratch), permite reservar y **liberar recursos individualmente y en cualquier orden** (gráficos, sonido, …).

```
 buffer del llamador (Chip/Fast/Slow)         BlockPool
 ───────────────────────────────────         ──────────────────────────
 [ base (alineada) ················ ]  allocate(size,align) → hueco first-fit
                                        free(ptr)            → marca libre + fusiona
 fragmentación visible: huecos libres = free_bytes()
```

## Detalles que importan

- **Base alineada una sola vez**: al crear el pool, la base se alinea a `align` (el padding de cabecera se descarta), así el padding de alineación **no se acumula** por reserva y una reserva que cabe siempre cabe (es el fallo que `LinearArena::allocate` evita).
- **`kMaxSlots`** (`BlockPoolT<kMaxSlots>`, def. 64) es el nº máximo de huecos/segmentos que el pool distingue: cada reserva que no llena un hueco exacto lo parte en dos. Agotados los slots, una reserva que no encaje **falla** (no corrompe) — súbelo si el juego fragmenta mucho en runtime (el setup no es caliente).
- **Genérico**: opera sobre cualquier buffer y lleva su `MemoryKind` (Chip/Fast/Slow/Any) como **dato**.

## Constructores

| Constructor | Uso |
|---|---|
| `BlockPoolT(base, size, kind = Any, align = 16)` (`:51`) | Pool propio sobre un buffer (persistente). |
| `BlockPoolT(LinearArena& arena, kind)` (`:59`) | **Delega** en una arena existente (mismo buffer y cursor): arena y banco no se solapan. **`free` pasa a no-op** (la arena es *bump*), así que este modo es solo para *scratch*; un recurso persistente usa un pool sobre buffer propio. Ver `MEMORY_OWNERSHIP.md`. |

Volver al [índice de `memory/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
