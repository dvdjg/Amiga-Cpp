# Roadmap de propiedad de memoria (correcciones por orden de importancia)

Plan de correcciones para llevar el engine al modelo de [`MEMORY_OWNERSHIP.md`](../../engine/architecture/MEMORY_OWNERSHIP.md):
**una sola puerta de reserva**, banco en el **tipo**, vistas no propietarias seguras y **liberación
ordenada**. Cada paso es verificable por sí mismo; el orden es por dependencia y valor.

Estado real mapeado (2026-09): conviven **dos familias de reserva** que hay que unificar.

```text
  familia A (tipada, por banco, nuevo)         familia B (arena suelta, legado)
  ─────────────────────────────────────       ─────────────────────────────────
  MemorySystem (chip/slow/fast)                 LinearArena / BlockPool
  ├─ MemBank<Chip>  (BlockPool)                 ├─ .chip.allocate_block<Tag>()
  │   reserve<Tag> -> Block<Tag, Chip>          ├─ .slow / .fast .allocate_block<Tag>()
  │   release(block)                            └─ .frame  (Chip, scratch)
  ├─ MemBank<Slow> / MemBank<Fast>
  └─ frame: ChipArena (scratch de frame)
```

## Fase 0 — Blindar la frontera DMA (rápida, sin refactor)

**Valor:** evita el error más peligroso (pasar memoria no-Chip a DMA) antes de tocar asignadores.

1. Auditar los usos de `Address<Chip>::from_storage(...)` **fuera** de `typed.hpp`/`ChipStorage` y
   comprobar que cada uno cita su procedencia (arena Chip, `Bitmap`, `.MEMF_CHIP`). Los que no,
   migrar a `Block::mem_view_chip()` (que valida el banco) o `ChipStorage`.
2. Igual con los constructores `BlitSource`/`BlitDest` desde puntero crudo: revisar quién los usa
   fuera del camino `ChipView<Tag>`.
3. Añadir criterio al gate `raw-pointer-members`/`casts`: `from_storage` solo permitido en los
   ficheros-frontera declarados (`casts-frontier.txt`).

**Evidencia:** `node tools/analyze/cast-audit.mjs --check`, grep de `from_storage` acotado, y una
demo que intente (y no compile) pasar `Fast` a una API Chip (test negativo en host).

## Fase 1 — Unificar la puerta de reserva y de liberación (núcleo de (a))

**Valor:** una única API de reserva y una única de liberación; prepara el resto.

**Estado (2026-09): base hecha.** `MemBank<K>::reserve<Tag>`/`release` + `Assets`
(`add`/`create`/`release`/`reset_phase`, libera en orden inverso) son la puerta; `SfxMixer` ya la usa
(salida Chip obligatoria, plugins Any). Falta migrar los dueños restantes (punto 3).

1. Homogeneizar la API de reserva sobre `MemBank<K>::reserve<Tag>()`:
   - `LinearArena::allocate_block<Tag>()` **delega** en el banco cuando la arena es de respaldo de
     un `MemBank` (ya hay `configure_backing`), o se mantiene solo para *scratch* sin `free`.
   - Retirar progresivamente `MemBank::reserve(void*)`/consumidores que abren `pool()` directo.
2. Definir el par **reserve/release** tipado: `MemBank<K>::release(const Block<Tag,K>&)` (existe) y
   una fachada `ResourceStore`/`Assets` para el juego (`load<Sprite>`, `create<Bitmap>`, `retain`,
   `release`, `reset_phase`). **Hecho**: `eng::Assets` (`eng/api/assets.hpp`).
3. Reservas de fase: `frame`/`setup` con semántica explícita (arena reiniciable) y `reset_frame`.

**Evidencia:** HOST-340 ampliado (idempotencia del `free`, puntero ajeno, reserve/release de
`MemBank`) + build de demos de audio (057/058/061/068…).

## Fase 2 — Teardown ordenado y comprobación de DMA pendiente

**Valor:** elimina *use-after-free* del Blitter/Copper/Paula.

1. Orden de teardown canónico (doc §"Setup, runtime y frame" paso 4) como **helper** único:
   escenas/caches → desactivar display/DMA → bloques raíz del backend.
2. `Block` **move-only** (borrar copia): copiar un `Block` = dos dueños del mismo bloque. Es gratis
   y elimina una clase de bug.
3. Comprobación de **DMA pendiente** antes de liberar: consultar `FramePlan`/`BlitQueue`/copper activo
   y rechazar el `release` (o `wait`) hasta que no haya trabajo vivo. En build de diagnóstico, el
   `Block`/vista lleva un **token de generación** del store y la liberación invalida las vistas.

**Evidencia:** HOST del token de generación (vista a bloque liberado → trapa en debug) + demo de
doble buffer que no libera el buffer aún visible.

## Fase 3 — Migrar dueños y consumidores a la única puerta

**Valor:** cierra el modelo; ya no hay dos formas de reservar.

**Estado (2026-09): fachada y escena migradas.** La cadena `compose`→`Scene`→`Bitmap`/`copper::Plan`/
`DoubleBuffer`/`SpriteManager`/`xlimited_*`/`tile_scroll`/`effects` y las demos pasan a
`MemoryManager&` con `chip().reserve` (mismo cursor que la arena vía `configure_backing`, **sin
solape**). `res::load(MemoryManager)` es la puerta normal.

**Desbloqueo del pool propio de `MemBank` (free real): HECHO.** `res::load`/`load_file` (única
puerta), `TextBlitScratch`, `AssetCacheBackend` y todas las reservas de escena usan el **banco**;
`AmigaBackend::configure_memory` pasa a `MemoryManager::configure` (pool propio, `free` real) y
`Budget`/`MemoryReport` leen del banco. `Device::memory_manager()` expone la puerta. Sin `headroom`
en `res::load` (el pool alinea la base una vez).

Diagnóstico de la migración: `graphics/bitmap.hpp` (`Bitmap::init`),
`graphics/sprite_manager.hpp`, `copper/double_buffer.hpp`, `graphics/composition/scene.hpp`,
`copper/plan.hpp`, `drivers/tile_scroll.hpp`, `field/{xlimited_scene,xlimited_composer,plane_view,
flat_playfield,mirror_playfield,canvas_playfield,soft_dpf}.hpp` — **hecho**.

1. Homogeneizar la API de reserva sobre `MemBank<K>::reserve<Tag>()` (hecho en fachada/escena).
2. `Assets` como par reserve/release del juego (`create`/`release`/`reset_phase`) — **hecho**.
3. Reservas de fase: `ScratchArena` + `reset_frame` — **hecho** (Fase 6).

**Evidencia:** suite host verde (salvo el fallo pre-existente de `382_audio_compressor_cli`); builds
m68k de 052/086/100/113/117/209 y del resto de la escena; 113/086 READY.

## Fase 4 — Diagnóstico y presupuesto

**Valor:** observabilidad y control de crecimiento.

**Estado (2026-09): panel de memoria hecho.** `debug::Telemetry` + `telemetry_from(MemoryManager,
MemorySystem, Telemetry)` rellenan el panel desde los **bancos** (reservas reales: `used/capacity`)
+ la **scratch de frame** + la **fragmentación** del pool (`chip_slots`/`chip_slots_max`).
`App::telemetry()` lo expone; `MemoryReport` ya lee del banco. HOST-384 lo fija.

1. `MemoryReport`/presupuesto por banco (Chip persistente, Chip scratch, Fast, Slow) con picos y
   bytes retenidos; exponerlo por el canal lateral. **Hecho** (panel + `Budget`; `peak` del pool
   queda en 0 — pendiente si hace falta).
2. Diagnóstico de banco/owner/tamaño/alineación/estado/causa de fallo **sin exponer punteros** a la
   app (doc §"Consistencia"). **Parcial**: banco/usado/fragmentación sí; owner/causa, pendientes.
3. `Result`/`Expected` en las APIs nuevas de reserva (unificar con `MeshStatus`/`AudioPlan::Status`).

## Fase 5 — Aridad de anchura (helpers acotados, no `Number<Tag>`)

**Valor:** quitar los `static_cast` de módulos/strides sin ocultar desbordamientos.

1. Helpers de dominio **acotados** para el borde hardware: p. ej. `Offset16`/`mod16(a,b)` para
   módulos de Blitter/Copper (`row_bytes - words*2 → s16`), con **aserción de rango en debug**.
2. Sustituir los `static_cast<s16>` de `bob_save_box`/`bob_restore_box`/`bob_erase_box` y afines por
   el helper (un solo sitio con la regla y el rango).
3. **No** introducir un `Number<Tag>` genérico: el repo tipa buffers/direcciones/roles, y deja las
   anchuras como enteros (doc `typed.hpp`, `INTERNAL_TYPE_SYSTEM.md` §3.3).

**Evidencia:** HOST del helper (rango, truncado documentado) y `cast-audit` a la baja.

## Fase 6 — Asignador persistente con `free` real (rediseño del pool)

**Motivo.** `LinearArena` es *bump* (LIFO): reservar gráficos, luego sonido y liberar gráficos **no
se puede** sin `clear()` total. La memoria del engine se reparte en **dos vidas útiles** distintas:

| Vida | Herramienta | Semántica |
|---|---|---|
| Persistente (assets, escena, buffers de larga vida) | `BlockPool` | reserve + **free** en cualquier orden |
| Scratch de frame/fase | `LinearArena` | bump + `mark/release` (LIFO) |

**Estado (2026-09): `BlockPool` con free real — hecho.**

- `allocate` es *first-fit* sobre huecos, `free` marca y **fusiona**; ya **no** delega `free` en la
  arena cuando hay buffer propio. `configure_backing` queda **solo para scratch** (free = no-op
  documentado).
- La **base se alinea una vez** al crear el pool: elimina el padding acumulativo (el «peyote» de
  `LinearArena::allocate`, bug demo 201).
- Tabla de huecos **configurable** (`BlockPoolT<kMaxSlots>`, alias `BlockPool` = 64); `slots_left()`
  expone la fragmentación.
- HOST-340 ampliado: reservar gráficos+sonido, **liberar gráficos con sonido vivo**, reutilizar el
  hueco; base desalineada; alineación a 64; coalescencia.

**Pendiente de esta fase:**

1. **`ScratchArena`** (bump + `mark()`/`release(mark)`): separar `MemorySystem` en persistente (pool)
   y scratch de frame; `reset_frame()` solo limpia la scratch. **Hecho** (`arena.hpp`:
   `ScratchArena`, `ArenaMark`; `MemorySystem::frame` es `ScratchArena`; `reset_frame()`; HOST-383).
2. **Migrar `MemBank`/`Assets`** a pool propio (hoy `MemBank` ya usa `BlockPool`; confirmar que el
   backend no lo enlaza con `configure_backing` para los bancos persistentes). **Bloqueado por diseño**:
   mientras la cadena de escena use `MemorySystem.chip.allocate_block` (arena), el banco **debe**
   compartir cursor (`configure_backing`) para no solapar; el pool propio llega **después** de migrar
   la escena a `MemBank` (Fase 3).
3. Quitar el `+16 headroom` de `res::load` (ya no hace falta con base alineada del pool; la arena
   *bump* sigue necesitándolo hasta migrar).

## Orden recomendado

Fase 0 → 1 → 2 son el núcleo de (a) y van juntas. La 3 es la migración amplia. La 4 (diagnóstico) y
la 5 (anchuras) son mejoras incrementales. La 5 no debe empezarse antes de la 3, para no tocar dos
veces los mismos ficheros. La **Fase 6** (asignador con `free` real) es la base que hace utilizables
a las demás para recursos persistentes; su primer paso (`BlockPool`) está hecho.
