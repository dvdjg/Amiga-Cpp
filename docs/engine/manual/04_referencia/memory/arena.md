# Referencia — arenas

Una **arena** gestiona un bloque que **otro le entrega** (el backend o el sistema superior): no pide memoria al SO. La `LinearArena` (`engine/include/eng/memory/arena.hpp:97`) es *bump allocation* — cada `allocate` devuelve el siguiente trozo alineado y **no hay liberación individual**; `clear()` reinicia el cursor. Es ideal para recursos de escena, scratch de frame y buffers cocinados: evita fragmentación y hace **visible el coste** de memoria.

```
 backend entrega el bloque           LinearArena (no pide memoria)            consumidor
 ────────────────────────            ────────────────────────────             ──────────
 [ pool base · cursor · límites ] ─► allocate(size, align) ──► MemoryBlock {data,size,kind}
                                     reset() / mark·release ─► cursor atrás (LIFO)
```

## Tipos

| Tipo | Qué es |
|---|---|
| `MemoryBlock` (`:40`) | Resultado de una reserva: `{data, size, kind}`. Inválido si `data == nullptr`; helpers `buffer<Tag>()`/`view<Tag>()`/`block<Tag>()` dan la vista tipada (evita `static_cast`). |
| `ArenaSnapshot` (`:71`) | Foto inmutable (base/capacity/used/peak/remaining/kind) para overlays, logs y profiler sin exponer punteros mutables. |
| `ArenaMark` (`:82`) | Marcador de la arena de scratch (usado por `mark`/`release`): el cursor antes de un tramo. Guardarlo es gratis y **anidable**. |
| `LinearArena` (`:97`) | Arena *bump* sobre un bloque dado. `reset(base, size, kind)` reasocia (no libera la anterior). |
| `ChipArena` (`:231`) | `LinearArena` especializada para Chip RAM. |
| `ScratchArena` (`:259`) | Arena de scratch de frame: se vacía entera con `clear`/`reset_frame` (LIFO total). |

## Sistema de memoria — `MemorySystem` (`:282`)

Reúne las tres arenas y expone el ciclo de frame:

```cpp
struct MemorySystem {
    ChipArena   chip;   // persistente (Chip)
    LinearArena slow;   // persistente (Slow)
    ScratchArena frame; // temporales del frame

    void reset_frame(); // invalida lo reservado en `frame`; NO toca chip/slow
};
```

`reset_frame()` se llama al empezar/terminar cada frame: los recursos **persistentes** (`chip`/`slow`) sobreviven; solo se recicla la scratch de frame.

## Configuración e informe

`MemoryConfig` (`:296`) pide al backend los bloques que administrará el engine: `chip_bytes`, `slow_bytes`, `frame_bytes`, `fast_bytes` (Fast = solo CPU; 0 = no reservar). No representa toda la RAM del Amiga, solo lo que el engine gestiona.

`MemoryReport` (`:304`) es el resultado: un `ArenaSnapshot` por arena + flags `chip_ok`/`slow_ok`/`frame_ok`/`fast_ok`; `ok()` = las tres obligatorias reservadas.

Volver al [índice de `memory/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
