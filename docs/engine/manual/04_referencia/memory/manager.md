# Referencia — `MemoryManager`

`eng::MemoryManager` (`engine/include/eng/memory/memory_manager.hpp:27`) reúne los **tres bancos tipados** `MemBank<Chip>`/`<Slow>`/`<Fast>`. **No hay `MemoryKind` en runtime**: cada reserva sale de un banco concreto, así que `reserve`/`release`/`free_bytes` no hacen `switch` ni reciben el medio como argumento — el banco es un **tag de plantilla** (`MemoryKind`).

```cpp
eng::MemoryManager mem;
mem.configure(chip_base, chip_bytes, slow_base, slow_bytes, fast_base, fast_bytes);
auto planes = mem.chip().reserve<eng::PlaneTag>(n);   // DMA → Chip (tipado)
auto sim    = mem.fast().reserve<eng::SimTag>(m);     // CPU → Fast (tipado)
```

## Configuración

| Método | Qué hace |
|---|---|
| `configure(chip, chip_bytes, slow, slow_bytes, fast, fast_bytes, align = 16)` (`:30`) | Entrega los buffers por banco (del backend). `slow`/`fast` pueden ser nulos (A500 sin ellos). `configured()` = hay Chip. |
| `configure_backing(chip, slow, fast, fast_bytes, align = 16)` (`:44`) | Enlaza los bancos de Chip/Slow a las **arenas del `MemorySystem`** (mismo buffer y **mismo cursor**) para que arenas y bancos **no se solapen**: un único asignador por medio. Es lo que usa `AmigaBackend::configure_memory`. |
| `chip()` / `slow()` / `fast()` (`:54`) | Los bancos tipados. |
| `has_slow()` / `has_fast()` (`:67`) | Si el banco tiene bytes. |

La elección de banco es una decisión de **setup** (qué buffers entrega el backend): en un A500 sin Fast/Slow, esos bancos quedan con 0 bytes y sus reservas devuelven bloques inválidos.

## Reserva por política — `MemoryPolicy` (`:80`)

Formaliza la elección de banco con **fallback explícito**; el banco efectivo viaja en `Block::kind` (el medio es **dato**, no tipo), así que el consumidor sabe dónde cayó sin duplicar la decisión.

| Política | Bancos (en orden) |
|---|---|
| `ChipRequired` | solo Chip (DMA), sin fallback. |
| `FastRequired` | solo Fast (CPU), sin fallback; inválido si no hay Fast. |
| `FastPreferred` | Fast → Slow. |
| `AnyBank` | Fast → Slow → Chip. |

Los helpers de runtime implementan la elección:

- `fast_or_slow<Tag>(mm, bytes, alignment = 0)` (`:91`) — CPU no DMA: Fast si la hay, si no Slow.
- `any_bank<Tag>(mm, bytes, alignment = 0)` (`:107`) — cualquier RAM: Fast → Slow → Chip (Chip el último, que es el más escaso).
- `reserve<Tag>(mm, policy, bytes, alignment = 0)` (`:131`) — reserva según una `MemoryPolicy` nombrada, reutilizando los dos anteriores.

> Los usos de **CPU intensiva** (descompresión, simulación, pilas) prefieren Fast; los de **DMA** exigen Chip. Ver `docs/engine/architecture/MEMORY_OWNERSHIP.md`.

Volver al [índice de `memory/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
