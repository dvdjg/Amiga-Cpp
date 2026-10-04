# Referencia — `memory/`

Gestión de memoria (`eng::memory`). El engine **no pide memoria al SO en runtime**: el backend entrega buffers en el *setup* (o el arranque toma rangos físicos) y el engine los administra. Como el A500 objetivo tiene **Chip RAM** (visible por Agnus) y poca más, distinguir el banco no es un detalle de rendimiento: es un requisito de hardware.

```
                 backend (setup)                     eng::memory (runtime)             consumidor
  ┌───────────────────────────────┐   ┌────────────────────────────────────────┐   ┌───────────────┐
  │ AllocMem / rango físico        │──►│ MemoryManager ─ MemBank<Chip|Slow|Fast>│──►│ Block<Tag,K>  │
  │ (sondea cuánta RAM hay)        │   │   └─ BlockPool (first-fit, free)        │   │ Address<K>    │
  └───────────────────────────────┘   │ LinearArena / ScratchArena (bump/LIFO) │   └───────────────┘
                                       └────────────────────────────────────────┘
```

## Páginas

| Página | Qué documenta |
|---|---|
| [`manager.md`](manager.md) | `MemoryManager`, `MemoryPolicy`, y las reservas por política (`fast_or_slow`/`any_bank`/`reserve`). |
| [`arena.md`](arena.md) | `LinearArena` (bump/LIFO), `ChipArena`, `ScratchArena`, `MemorySystem`, `MemoryConfig`, `MemoryReport`, `MemoryBlock`/`ArenaSnapshot`/`ArenaMark`. |
| [`block_pool.md`](block_pool.md) | `BlockPool`: asignador *first-fit* con `free` y fusión de huecos (persistente). |
| [`banks.md`](banks.md) | `MemBank<K>`, `ChipStorage`/`ENG_CHIP_RAM`, `Stack`/`StackPolicy`. |

## Reglas

- **El banco viaja en el tipo**, no en una variable de runtime: `MemBank<MemoryKind::Chip>` entrega `Block<Tag, Chip>` y su `Address<Chip>`; una API que exige Chip **no compila** si le pasas `Address<Fast>` (`docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`).
- **Un único asignador por medio**: las arenas y los bancos que comparten buffer enlazan su cursor (`configure_backing`) para **no solaparse**.
- **DMA ⇒ Chip**: bitplanes, copperlists, sprites, audio y fuentes de Blitter deben estar en Chip. Fast/Slow no son alcanzables por Agnus (`MEMORY_OWNERSHIP.md`).

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
