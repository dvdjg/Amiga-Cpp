# Referencia — bancos, Chip estático y pilas

## `MemBank<Bank>` — `mem_bank.hpp:35`

Un banco de un `MemoryKind` concreto: posee un `BlockPool` de su medio y entrega reservas **ya tipadas por el banco** (`Block<Tag, K>` con `Address<K>`). Así una API que exige Chip **no compila** si le pasas `Address<Fast>`: el banco viaja en el **tipo** (etiqueta vacía, coste cero), sin nombres de caso ni comprobaciones en runtime.

```cpp
eng::MemBank<eng::MemoryKind::Chip> chip;
chip.configure(chip_base, chip_bytes);
eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> planes = chip.reserve<eng::PlaneTag>(n);
eng::Address<eng::MemoryKind::Chip> dma = planes.address();   // solo esto es DMA
```

| Método | Qué hace |
|---|---|
| `configure(base, size, align = 16)` (`:45`) | Asocia el buffer del banco. `size == 0` deja el banco vacío (Fast/Slow en un A500). |
| `reset()` (`:52`) | Reinicia la config del banco si posee pool propio; no libera el buffer raíz. |
| `configure_backing(LinearArena&)` (`:56`) | El pool **delega** en la arena (mismo buffer y cursor) — evita el solape arena/banco. |
| `reserve<Tag>(bytes, alignment = 0)` (`:61`) | Devuelve `Block<Tag, K>`; inválido si no cabe o el banco está vacío. |

El banco **se conoce en runtime** (los tamaños se fijan en el setup), pero eso no impide el tipado: creas las tres instancias y las que no tengan bytes devuelven bloques inválidos.

## `ChipStorage` y `ENG_CHIP_RAM` — `chip_storage.hpp:48`

Un array global puede acabar en `.data`/`.bss` (donde pueda) y **no** ser alcanzable por DMA. Para que Agnus lo vea, la tabla debe vivir en Chip RAM: se declara con `ENG_CHIP_RAM`, que la coloca en la sección `.MEMF_CHIP`.

```cpp
ENG_CHIP_RAM eng::ChipStorage<eng::CopperTag, 4096> g_copper;
eng::Scheduler sched { g_copper.view() };
eng::Address<eng::MemoryKind::Chip> dma = g_copper.address();   // solo esto es DMA
```

`ChipStorage<Tag, Capacity>` **coloca** el búfer y **certifica** su procedencia, entregando una `Address<Chip>` (sin `cast` a mano): `block()` = `Block<Tag, Chip>`, `address()` = dirección DMA, `view()` = vista con el tag. Para datos de fichero, `INCBIN_CHIP` los incrusta en `.INCBIN.MEMF_CHIP` con la misma garantía.

> `ENG_CHIP_RAM` se activa con el macro `ENG_AMIGA` (lo define el build de Amiga). **No** basta `__m68k__`: Mega Drive y Atari ST son m68k pero no tienen Chip RAM.

## `Stack` y `StackPolicy` — `stack.hpp`

`Stack` (`stack.hpp:23`) es una pila de CPU: un bloque de un banco + su **tope** alineado (el valor que se carga en `SP`); `valid()` = hay memoria.

```cpp
eng::Stack s = eng::fast_or_slow_stack(mm, 8192u);
if (s.valid()) { /* cargar s.top en SP */ }
```

| Helper | Qué hace |
|---|---|
| `stack_from_block(block, bytes, align = 8)` (`:47`) | Tope (base + `bytes`) alineado a partir de un bloque ya reservado. |
| `stack_from<Bank>(bank, bytes, align = 8)` (`:53`) | Reserva en el banco `Bank` (compile-time). |
| `fast_or_slow_stack(mm, bytes, align = 8)` (`:59`) | **Fast si la hay, si no Slow** (decisión de medio en runtime). |

`StackPolicy` (`:63`) formaliza banco/tamaño/fallback en un valor (`bank`, `bytes`, `align`) para no repetir la elección; el banco efectivo queda en `Stack::block.kind`. El banco por defecto es **Fast** (CPU-privada: IRQs y tareas no compiten con el bus de Agnus); `ChipRequired` si la pila debe ser DMA-visible.

Volver al [índice de `memory/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
