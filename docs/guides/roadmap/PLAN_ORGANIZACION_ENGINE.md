# Plan de organización general del engine

Plan vigente para ordenar la **estructura del engine**, las **cabeceras**, la **separación por plataformas**, los **tests** y los **scripts de apoyo**. Es la fuente única del rumbo de organización; las decisiones ya adoptadas se anotan en [`../../engine/architecture/ENGINE_STRUCTURE_REVIEW.md`](../../engine/architecture/ENGINE_STRUCTURE_REVIEW.md) y la estructura de directorios canónica sigue siendo [`../../STRUCTURE.md`](../../STRUCTURE.md).

## 1. Diagnóstico

La sensación de desorden no viene de «todo en cabecera», sino de **falta de ejes de agrupación** (tema, plataforma, nivel de test). Evidencia medida en el repositorio:

| Síntoma | Evidencia |
|---|---|
| Casi todo es cabecera | `engine/src/` solo contiene el backend (ficheros en `amiga_minimal/`); el resto del engine es header-only por diseño (`docs/STRUCTURE.md` §3). |
| Cabeceras mezcladas por tema | `eng/core/` tiene decenas de ficheros sueltos (math, tipos, memoria, E/S, sort, expr…) más `eng/core/util/`. |
| Plataforma inconsistente | `eng/platform/` mezcla `amiga_minimal.hpp`, `audio_paula.hpp` e `input_poll.hpp` **sueltos** con la carpeta `amiga/`. |
| Tests planos | `tests/host/NNN_*` en un solo nivel y un catálogo único; `tools/check/test-numbering.mjs` y `tools/run-host-tests.sh` asumen un nivel. |
| Documentación plana | `docs/engine/architecture/` con decenas de documentos en un solo nivel. |

## 2. Cabeceras: header-only por defecto, `.cpp` con criterio

El header-only es **load-bearing** en este engine y no debe abandonarse en bloque:

1. **Rendimiento m68k.** El build documenta que `-Os` degrada porque rompe las cadenas de `always_inline` y mete `jsr`/`memset` por MOVE de Copper (`tools/build/build-demo.sh`). Una frontera `.cpp` añade un `jsr` por llamada y mata el vocabulario inline.
2. **Freestanding sin librería.** Las demos no enlazan una `libeng`; el engine es un conjunto de cabeceras.
3. **Testabilidad host gratis.** `tools/run-host-tests.sh` compila **solo** `tests/host/<categoría>/NNN/src/main.cpp` con `-I engine/include` y **no enlaza `engine/src`**. Mover lógica de dominio a `.cpp` la deja sin test host (rompe la regla de cierre de `docs/testing/README.md`).

Criterio (canónico en [`HEADER_POLICY.md`](../../engine/architecture/HEADER_POLICY.md)):

```
┌──────────────────────────────────────────────────────────────┬───────────────┐
│ Caso                                                          │ Destino        │
├──────────────────────────────────────────────────────────────┼───────────────┤
│ Plantilla / constexpr / tipo-valor pequeño                    │ Cabecera      │
│ Ruta caliente por frame (copper, blit, raster, scroll)        │ Cabecera      │
│ No-plantilla, frío, pesado y grande                           │ .cpp          │
│ Backend de máquina                                            │ .cpp          │
└──────────────────────────────────────────────────────────────┴───────────────┘
```

Adoptar `.cpp` en el dominio exige antes que el host pueda enlazarlo (`tools/build/build-host-lib.sh` → `out/host-lib/libeng.a`). El problema real de una cabecera gigante no se arregla con `.cpp`, se arregla **partiéndola por tema** (§3).

## 3. Cabeceras estructuradas por temas

Objetivo: subdividir `eng/core/` por tema **sin romper consumidores** (patrón ya usado en la familia de playfields: la cabecera antigua queda como paraguas). Árbol destino:

```
engine/include/eng/
├── core/
│   ├── math/        aritmética, fixed, linalg, geometry, scalar, tablas, ruido, expr
│   ├── types/       tipos base, vistas con tag, unidades, byte order, CRC
│   ├── data/        ct_array, mesh3d, polygon, sort, utf8, rtc
│   └── util/        contenedores y algoritmos (se queda)
├── cpu/m68k/        (se queda: es de CPU, la comparten Amiga/Atari/Megadrive)
├── retro/           (se queda)
└── platform/
    └── amiga/       (todo el vocabulario Amiga, ver §4)
```

Migración segura: mover el fichero, dejar en la ruta antigua una cabecera-paraguas que incluya la nueva, migrar consumidores de forma incremental y retirar el paraguas cuando no queden referencias. El gate `tools/check/engine-tree.mjs` impide volver a aplanar `eng/`.

## 4. Separación por plataformas: modelo de tres anillos

Hoy hay **un solo backend** (`AmigaBackend`) cuyo nombre confunde *Amiga* con *este backend mínimo*. El modelo objetivo separa tres anillos:

```
┌─ Anillo 0: DOMINIO (agnóstico de máquina) ────────────────────────────┐
│ eng/core, field, scene, graphics/{composition,effects,drivers,tilemap} │
│ ai, sim, board, cards, ui, os, input, audio (mixer/planos), res,       │
│ parallel, task, debug, hw                                              │
│  → no conoce registros, DMA ni Copper. Habla de intenciones portables: │
│    Visual / CopperIntent / SpriteIntent / Effect, y de capacidades     │
│    (RasterCaps).                                                       │
├─ Anillo 1: VOCABULARIO DE CHIPSET (por familia de máquina) ────────────┤
│ eng/cpu/m68k          → CPU (la comparten Amiga, Atari ST y Megadrive) │
│ eng/platform/amiga    → registros custom, Copper, Blitter, Paula, CIA, │
│                          decodificación de entrada                     │
│ eng/platform/atarist  → (futuro) Shifter, YM2149, MFP, IKBD            │
│ eng/platform/megadrive→ (futuro) VDP, YM2612                           │
├─ Anillo 2: BACKEND (implementación por objetivo) ──────────────────────┤
│ engine/src/platform/amiga/       backend Amiga (OCS/ECS/AGA)           │
│ engine/src/platform/atarist/    (futuro)                               │
│ engine/src/platform/megadrive/  (futuro)                               │
└────────────────────────────────────────────────────────────────────────┘
```

Qué es común y qué es específico dentro de Amiga:

| Nivel | Contenido | Cómo se modela |
|---|---|---|
| Común a todo Amiga (OCS/ECS/AGA) | Mapa de registros custom `$DFF000`, Copper, Blitter, Paula (4 canales), CIA-A/B, Chip/Slow/Fast, ROM kernel | `eng/platform/amiga/` + backend |
| OCS (A500) | Bus de 16 bits, sin `FMODE`, 512 KB Chip + 512 KB trapdoor | `HardwareProfile a500_1mb_slow` |
| AGA (A1200/A4000/CD32) | Fetch 32/64 bits (`FMODE`), más bitplanes/paleta, Fast RAM, Akiko (CD32) | macro `K_AGA` + `raster_caps()` |
| Timing | PAL 50 Hz / NTSC 60 Hz | `HardwareProfile::pal` |

Conclusión: **A1200 no es un backend distinto; es el mismo backend Amiga con otro perfil/macro.** La diferencia Amiga/Atari/Megadrive sí es un backend distinto, y el seam ya existe: los concepts `DisplayDriver`/`GraphicsDriver` (`graphics/driver.hpp`), `GameModule`/`GameIdle` (`engine.hpp`), las capacidades (`RasterCaps`) y las intenciones portables.

Acciones:

1. Consolidar el anillo 1: `audio_paula.hpp` → `eng/platform/amiga/paula.hpp`, `input_poll.hpp` → `eng/platform/amiga/input_poll.hpp`, `amiga_minimal.hpp` → `eng/platform/amiga/backend.hpp` (con paraguas de compatibilidad durante la migración, ya retirados).
2. Contrato de backend explícito en `eng/platform/backend.hpp` (concept que reúne lo que `Engine` usa por duck-typing): soportar Atari ST = satisfacer el contrato, no copiar el backend Amiga.
3. Unificar el perfil de máquina con `eng::hw` (el `HardwareProfile` del backend duplica conceptualmente `eng::hw::probe`).
4. Gate `tools/check/platform-boundaries.mjs`: las cabeceras de dominio no pueden incluir `eng/platform/<familia>` ni referenciar registros `$dff`. `cpu/m68k` queda permitido.

Detalle en [`../../engine/architecture/PLATFORM_LAYERS.md`](../../engine/architecture/PLATFORM_LAYERS.md).

## 5. Tests: taxonomía de tres ejes

- **Plataforma:** `host` · `amiga` · `atarist` · `megadrive`.
- **Nivel:** `L0` bare-metal · `L1` unitario host · `L2` integración on-target · `L3` visual.
- **Categoría (dominio):** core-math, core-types, core-containers, graphics-raster, graphics-copper, graphics-composition, graphics-effects, field-scroll, field-tilemap, scene, platform-amiga, ai-decision, ai-navigation, ai-steering, ai-planning, sim, board-chess, board-go, cards, os, ui, audio, res, parallel, debug.

Árbol destino:

```
tests/
├── README.md                 → pirámide + taxonomía (índice de índices)
├── host/                     → L1 (g++ nativo, agnóstico)
│   ├── README.md             → índice de categorías
│   ├── core/       README.md + NNN_.../
│   ├── graphics/   README.md + NNN_.../
│   ├── field/      README.md + NNN_.../
│   ├── scene/      README.md + NNN_.../
│   ├── platform/amiga/  README.md + NNN_.../
│   ├── ai/  sim/  board/  cards/  os/  ui/  audio/  res/   (cada una con README.md)
├── amiga/                    → L0/L2/L3 on-target (WinUAE)
│   ├── README.md
│   ├── l0_bare_metal/        (hoy tests/amiga/l0_bare_metal)
│   ├── l1_backend/
│   └── l2_copper_frameplan/
└── atarist/                  → (futuro)
```

Reglas: los IDs `HOST-NNN` se conservan (son estables y citados en docs); agrupar físicamente no obliga a renumerar. El catálogo único se parte en un `README.md` por categoría y `tests/host/README.md` indexa categorías. `tools/check/test-numbering.mjs` y `tools/run-host-tests.sh` se adaptan al árbol anidado. Detalle en [`../../testing/TAXONOMY.md`](../../testing/TAXONOMY.md).

## 6. Documentación

- `docs/STRUCTURE.md` es la fuente autoritativa: refleja los tres anillos y el árbol de tests.
- Nuevos: `docs/engine/architecture/PLATFORM_LAYERS.md`, `docs/engine/architecture/HEADER_POLICY.md`, `docs/testing/TAXONOMY.md`.
- Reorganizar `docs/engine/architecture/` por tema (subcarpetas con README índice), manteniendo el README raíz como índice general.
- Anotar las decisiones en `ENGINE_STRUCTURE_REVIEW.md` y enlazar desde `DOC-MAP-PRINCIPAL.md`.

## 7. Scripts de apoyo

| Script | Cambio |
|---|---|
| `tools/check/test-numbering.mjs` | Recursivo + catálogos por categoría |
| `tools/run-host-tests.sh` | Glob recursivo + `--category` |
| `tools/check/platform-boundaries.mjs` | **Nuevo**: frontera dominio ↔ plataforma |
| `tools/check/engine-tree.mjs` | **Nuevo**: estructura temática de `eng/` |
| `tools/check/header-impl.mjs` | **Nuevo** (advisory con baseline): funciones no-plantilla grandes en cabecera |
| `tools/build/build-host-lib.sh` | **Nuevo**: `engine/src` (sin backends) → `out/host-lib/libeng.a` |

## 8. Fases

Estado: **1-5 hechas**; además, el troceo de las cabeceras de clase única (`compose.hpp`, `xlimited_playfield.hpp`, `world.hpp`) está hecho (ver `ENGINE_STRUCTURE_REVIEW.md` D3/D13) y `header-impl.mjs` corre `--strict` en CI.

1. **Documentar la decisión** (este plan + PLATFORM_LAYERS + HEADER_POLICY + TAXONOMY + STRUCTURE).
2. **Consolidar el anillo de plataforma** (`eng/platform/amiga/`, backend canónico, `BackendConcept`).
3. **Split temático de `eng/core/`** (`math/`, `types/`, `data/`).
4. **Reestructurar `tests/host/`** por categoría y adaptar las herramientas.
5. **Gates de frontera** (`platform-boundaries.mjs`, `engine-tree.mjs`).
