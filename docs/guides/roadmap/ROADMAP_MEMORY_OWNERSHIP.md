# Roadmap de propiedad de memoria (correcciones por orden de importancia)

Plan de correcciones para llevar el engine al modelo de [`MEMORY_OWNERSHIP.md`](../../engine/architecture/MEMORY_OWNERSHIP.md):
**una sola puerta de reserva**, banco en el **tipo**, vistas no propietarias seguras y **liberación
ordenada**. Cada paso es verificable por sí mismo; el orden es por dependencia y valor.

Estado real mapeado: conviven **dos familias de reserva** que hay que unificar. El backend Amiga
configura actualmente pools propios de `MemoryManager` sobre los bloques raíz, mientras conserva
`MemorySystem` para arenas y scratch; esa separación evita el solapamiento de los consumidores
migrados, pero no convierte automáticamente todos los owners en liberables. `AssetCache` conserva
un `MemoryBlock` con su `MemoryKind` efectivo; una generación invalida las vistas tras evict/reload y
las leases DMA bloquean la liberación. El detalle está en
[`memory-ownership-inconsistencies.md`](../../debugging/investigaciones/memory-ownership-inconsistencies.md).

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

**Estado (2026-09): hecha.** El ctor crudo implícito de `BlitPtr`/`BlitPtr` (`const u16*` →
`Address<Chip>`) se ha **retirado**; ahora es la función **nombrada**
`BlitPtr::from_storage`/`BlitPtr::from_storage` (como `Address<Chip>::from_storage`), de modo
que el acto de certificar Chip se lee como tal. Todos los usos internos migrados. Las fronteras DMA
declaradas (bitplanes, BOB, patrón, tile, copper, backend) están en `tools/check/casts-frontier.txt`
con su razón (14 ficheros), y el gate `cast-audit` las exime.

1. Auditoría de `Address<Chip>::from_storage`/`BlitPtr`/`BlitPtr`: **hecha** (ctor crudo nombrado).
2. Constructores de puntero crudo fuera del camino `ChipView`: **retirados** (salvo `from_storage`).
3. Criterio en el gate: `casts-frontier.txt` declara las fronteras DMA con su procedencia.

## Fase 1 — Unificar la puerta de reserva y de liberación (núcleo de (a))

**Valor:** una única API de reserva y una única de liberación; prepara el resto.

**Estado: base parcial.** `MemBank<K>::reserve<Tag>`/`release` + `Assets`
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
   y elimina una clase de bug. **Hecho** para `eng::Block`.
3. Comprobación de **DMA pendiente** antes de liberar. **Parcial**: `AssetCache` exige una lease Chip
   y rechaza `evict`/`shutdown` mientras siga activa; el owner debe adquirirla durante el uso DMA.
   Las vistas de caché llevan generación y quedan invalidadas en evict/reload/shutdown.
   Falta el helper global que consulte `FramePlan`/`BlitQueue`/Copper activo.

**Evidencia:** HOST-254/330 cubre lease DMA, rechazo de liberación, generación invalidada y recarga;
los dobles buffers gráficos verifican por separado que no se libera el buffer visible.

## Fase 3 — Migrar dueños y consumidores a la única puerta

**Valor:** cierra el modelo; ya no hay dos formas de reservar.

**Estado: fachada, escena y caché parcialmente migradas.** La cadena `compose`→`Scene`→`Bitmap`/`copper::Plan`/
`DoubleBuffer`/`SpriteManager`/`xlimited_*`/`tile_scroll`/`effects` y las demos pasan a
`MemoryManager&` con `chip().reserve`; `res::load(MemoryManager)` es la puerta normal. La
integración productiva ya tiene liberación física para los owners gráficos, audio y caché Amiga.
`AssetCache` conserva ahora `MemoryBlock` y banco efectivo, valida vistas por generación y retiene los
owners durante lecturas asíncronas y leases DMA.

**Pool propio de `MemBank` (free real): parcial.** `BlockPool` y sus métricas ya soportan `free`
real, y numerosos consumidores usan `MemoryManager`; `AmigaBackend::configure_memory` configura
pools persistentes separados de las arenas de scratch y `AssetCacheBackend::free()` devuelve la
reserva física usando el `MemoryKind` del `MemoryBlock`. La caché mantiene contabilidad por banco
efectivo y vistas con generación. `Budget`/`MemoryReport` leen del banco, pero
eso no implica que toda reserva productiva sea liberable. Ver MEM-001..MEM-007.

Diagnóstico de la migración: `graphics/bitmap.hpp` (`Bitmap::init`),
`graphics/sprite_manager.hpp`, `copper/double_buffer.hpp`, `graphics/composition/scene.hpp`,
`copper/plan.hpp`, `drivers/tile_scroll.hpp`, `field/{xlimited_scene,xlimited_composer,plane_view,
flat_playfield,mirror_playfield,canvas_playfield,soft_dpf}.hpp` — **hecho**.

1. Homogeneizar la API de reserva sobre `MemBank<K>::reserve<Tag>()` (hecho en fachada/escena).
2. `Assets` como par reserve/release del juego (`create`/`release`/`reset_phase`) — **hecho**.
3. Reservas de fase: `ScratchArena` + `reset_frame` — **hecho** (Fase 6).

**Evidencia:** HOST-254, HOST-330/331/332/353/386/388 validan owners, políticas de banco, leases,
invalidación de vistas y la tabla no propietaria; la suite completa queda con una revisión diferida,
HOST-382/ACP1 (nunca ha funcionado; fuera del alcance actual). Los tests relevantes de audio y gráficos pasan; builds
m68k de 052/086/100/113/117/209 y del resto de la escena; 113/086 READY.

## Fase 4 — Diagnóstico y presupuesto

**Valor:** observabilidad y control de crecimiento.

**Estado (2026-09): panel de memoria hecho.** `debug::Telemetry` + `telemetry_from(MemoryManager,
MemorySystem, Telemetry)` rellenan el panel desde los **bancos** (reservas reales: `used/capacity`)
+ la **scratch de frame** + la **fragmentación** del pool (`chip_slots`/`chip_slots_max`).
`App::telemetry()` lo expone; `MemoryReport` ya lee del banco. HOST-384 lo fija.

1. `MemoryReport`/presupuesto por banco (Chip persistente, Chip scratch, Fast, Slow) con picos y
   bytes retenidos; exponerlo por el canal lateral. **Hecho**: panel + `Budget` + **pico**
   (`BlockPool::peak_bytes` → `Telemetry::chip_peak`).
2. Diagnóstico de banco/owner/tamaño/alineación/estado/causa de fallo **sin exponer punteros** a la
   app (doc §"Consistencia"). **Hecho (banco/uso/pico/fragmentación/causa)**: `MemBank::Status`
   (`Ok`/`BankAbsent`/`NoSpace`/`Fragmented`) + `status_name`; owner nominal (Tag) viaja en el tipo.
3. `Result`/`Expected` en las APIs nuevas de reserva (unificar con `MeshStatus`/`AudioPlan::Status`).
   **Parcial**: `MemBank::Status` y `Block::valid()` (reserva sin excepciones); el `Result` genérico
   para las APIs de recurso queda cuando exista `ResourceStore`.

## Fase 5 — Aridad de anchura (helpers acotados, no `Number<Tag>`)

**Valor:** quitar los `static_cast` de módulos/strides sin ocultar desbordamientos.

**Estado (2026-09): hecha.** `eng::graphics::mod16(s32)`/`mod16u(u32)` (en `blit_job.hpp`) convierten
un módulo a `s16` (registro `BLTxMOD`) **comprobando el rango** (`ENG_ASSERT`, sin coste en release).
Sustituidos los `static_cast<s16>(row_bytes·planes − words·2)` de `bob.hpp` (5 funciones),
`blit_job.hpp`, `pattern_fill.hpp` y `field/{canvas,contiguous,xlimited}_playfield`. HOST-385.

**Criterio (descartado explícitamente):** **no** se restringe la anchura a potencias de 2. Los
módulos reales no lo son (`40·4 − 42 = 118`; `words` = 20/40/21/41) y **no hay división** que
ahorrar en el hot path (los módulos son resta + `mulu16`). Limitar a potencias de 2 rompería el
display estándar sin ganancia. Lo que sí se formaliza: `mod16` como único punto de la regla (con
detección de truncado en debug) y los módulos **invariantes** (`row_bytes`/`planes`/`words` de la
demo) calculados una vez en setup.

1. `mod16`/`mod16u` con **aserción de rango en debug**. **Hecho**.
2. Sustituir los `static_cast<s16>` de `bob_*`/`pattern_fill`/`field/*`. **Hecho**.
3. **No** un `Number<Tag>` genérico (el repo tipa buffers/direcciones/roles; las anchuras son
   enteros) y **no** restringir a potencias de 2 (justificado arriba).

**Evidencia:** HOST-385 y `cast-audit` a la baja (los `static_cast<s16>` de módulos ya no cuentan).

## Fase 6 — Asignador persistente con `free` real (rediseño del pool)

**Motivo.** `LinearArena` es *bump* (LIFO): reservar gráficos, luego sonido y liberar gráficos **no
se puede** sin `clear()` total. La memoria del engine se reparte en **dos vidas útiles** distintas:

| Vida | Herramienta | Semántica |
|---|---|---|
| Persistente (assets, escena, buffers de larga vida) | `BlockPool` | reserve + **free** en cualquier orden |
| Scratch de frame/fase | `LinearArena` | bump + `mark/release` (LIFO) |

**Estado (2026-09): `BlockPool` con free real; integración de owners — parcial.**

- `allocate` es *first-fit* sobre huecos, `free` marca y **fusiona**; ya **no** delega `free` en la
  arena cuando hay buffer propio. `configure_backing` queda **solo para scratch** (free = no-op
  documentado). El backend actual usa `configure()` para los pools persistentes de
  `MemoryManager` y conserva arenas separadas para `MemorySystem`/scratch.
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
2. **Completar `MemBank`/`Assets`**: `MemBank` ya usa `BlockPool` y el backend configura el pool
   persistente por separado; `AssetCache` conserva y libera `MemoryBlock` según el banco efectivo.
   Falta extender la validación generacional a todos los consumidores de vistas. Las arenas quedan
   reservadas para scratch y fases completas, no para recursos desalojables.
3. Quitar el `+16 headroom` de `res::load` (ya no hace falta con base alineada del pool; la arena
    *bump* sigue necesitándolo hasta migrar).

## Plan de cierre — DMA y lifecycle

Este es el orden de trabajo para cerrar los hallazgos MEM-001..MEM-010 sin introducir heap ni
propietarios duplicados:

1. **Teardown ordenado del backend**: detener servicios, silenciar y esperar Paula, esperar el Blitter,
   rechazar el cierre mientras `AssetRuntime` tenga lecturas/leases abiertas y después liberar raíces.
   `configure_memory()` usa el mismo cierre; validado con builds y demos 057/061.
2. **Owners gráficos explícitos**: `copper::DoubleBuffer`, `copper::Plan` y `composition::Scene`
   liberan bloques Chip, distinguen buffers propios de adjuntos y hacen rollback en inicialización
   parcial/repetida; queda pendiente la coordinación de todas las vistas retenidas por consumers.
3. **Owners de audio explícitos**: `AudioSystem` debe liberar el buffer P61 y `SfxMixer` debe
   hacer rollback de reservas parciales y devolver sus bloques después de parar la IRQ/mixer.
4. **Caché física**: `AssetCache` conserva el bloque/banco efectivo; `AssetTable` se invalida en
   `reset_phase`; HOST-386 cubre esa invalidación. Leases move-only impiden evict mientras un
   consumidor retenga una vista.
5. **Frontera DMA de Paula**: `lease_dma` solo admite Chip; `AudioSystem::play_music_asset` retiene el
   owner hasta `stop_music`, y el backend espera DMA idle en teardown. SFX retiene una lease CPU, ya
   que el mixer lee la muestra por CPU. Los BlitQueue assets Chip exigen lease mientras se procesa el plan.
6. **Pruebas de lifecycle**: rollback, reinicialización, evict/reload, doble liberación, vista
   invalidada y liberación con Blitter/Copper/Paula activos. HOST-254/330 cubren caché; ampliar pruebas
   de consumidores hardware queda pendiente.

El cierre requiere evidencia de código y tests; reservar en Chip y liberar el bloque raíz al final
del proceso no cuenta como lifecycle completo.

## Fase 7 — FastPreferred y memoria ejecutable

**Objetivo.** Cuando el arranque detecta Fast RAM, el engine la ofrece automáticamente a trabajo de
CPU para reducir contención con Agnus, sin trasladar allí buffers Chip/DMA ni obligar a cada
consumidor a elegir banco.

1. **Selección por política**: añadir `FastPreferred`/`FastRequired` con fallback explícito y banco
   efectivo en el handle. Aplicarlo a datos CPU-only y scratch; DMA conserva `ChipRequired`.
2. **Presupuesto de arranque**: detectar capacidad, reservar primero el stack configurado y
   dimensionar después los pools Fast persistente/scratch dejando margen a Exec y servicios.
3. **Pila principal**: evolucionar `FAST_STACK=1` fijo a `StackPolicy` (banco, tamaño, fallback),
   conservando base+tamaño para restaurar SP y liberar la reserva al salir. No cambiar SSP en un
   proceso AmigaDOS en modo usuario.
4. **Estáticos y código principal**: auditar ELF/HUNK/linker. La ubicación Fast debe decidirse antes
   de ctors/`main` por los flags de segmento que respete el loader; copiar globals ya inicializados
   no es relocalización válida.
5. **DynLoader por segmento**: reservar code/data/BSS individualmente, respetar `HUNKF_CHIP`, aplicar
   Fast como preferencia a segmentos CPU-only y relocalizar tras conocer todas las bases.
6. **Evidencia**: A/B con y sin Fast; verificar rangos reales, fallback, stacks, relocaciones,
   coste CPU/DMA y liberación de módulos.

El contrato técnico y las limitaciones actuales están en
[`FAST_RAM_POLICY.md`](../../engine/architecture/FAST_RAM_POLICY.md).

## Orden recomendado

Fase 0 → 1 → 2 son el núcleo de (a) y van juntas. La 3 es la migración amplia. La 4 (diagnóstico) y
la 5 (anchuras) son mejoras incrementales. La 5 no debe empezarse antes de la 3, para no tocar dos
veces los mismos ficheros. La **Fase 6** (asignador con `free` real) es la base que hace utilizables
a las demás para recursos persistentes; su primer paso (`BlockPool`) está hecho.
