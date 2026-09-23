# Taxonomía de tests

Organización canónica de la batería de tests del engine por **plataforma**, **nivel** y **categoría**. Complementa [`README.md`](README.md) (pirámide de validación determinista) y las reglas de tests de [`../../AGENTS.md`](../../AGENTS.md) §4. Plan de organización general: [`PLAN_ORGANIZACION_ENGINE.md`](../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md).

## 1. Los tres ejes

| Eje | Valores |
|---|---|
| **Plataforma** | `host` · `amiga` · `atarist` · `megadrive` |
| **Nivel** | `L0` bare-metal · `L1` unitario host · `L2` integración on-target · `L3` visual |
| **Categoría (dominio)** | core-math, core-types, core-containers, graphics-raster, graphics-copper, graphics-composition, graphics-effects, field-scroll, field-tilemap, scene, platform-amiga, ai-decision, ai-navigation, ai-steering, ai-planning, sim, board-chess, board-go, cards, os, ui, audio, res, parallel, debug |

Un test se localiza por su ruta: plataforma + categoría + nivel. El **nivel** no siempre coincide con la carpeta (un test host es L1; un test on-target puede ser L0/L2/L3 y lo declara su README).

## 2. Árbol

```
tests/
├── README.md                 → pirámide + taxonomía (índice de índices)
├── host/                     → L1 (g++ nativo, agnóstico de hardware)
│   ├── README.md             → índice de categorías
│   ├── core/                 → core-math, core-types, core-containers
│   ├── graphics/             → raster, copper, composition, effects
│   ├── field/                → scroll, tilemap, playfield
│   ├── scene/                → actores, representación
│   ├── platform/amiga/       → vocabulario Amiga host-testeable (input, blob, paula…)
│   ├── ai/                   → decision, navigation, steering, planning, perception
│   ├── sim/                  → ecosistema vivo
│   ├── board/                → ajedrez, Go
│   ├── cards/                → póker y variantes
│   ├── os/                   → mini-SO (mensajes, E/S, tiempo, tareas)
│   ├── ui/                   → GUI
│   ├── audio/                → mixer, codecs, streaming
│   ├── res/                  → caché de assets, DynLoader
│   └── parallel/             → concurrencia
├── amiga/                    → L0/L2/L3 on-target (WinUAE)
│   ├── README.md
│   ├── l0_bare_metal/        → registros, bitplanes, copper a mano, DMA, Blitter
│   ├── l1_backend/           → APIs del backend Amiga
│   └── l2_copper_frameplan/  → CopperScheduler, FramePlan, presupuestos
└── atarist/                  → (futuro)
```

Cada carpeta de categoría lleva su propio `README.md` con el **catálogo** (ID → directorio → qué cubre), y `tests/host/README.md` indexa las categorías. El catálogo por categoría sustituye al catálogo único.

## 3. Numeración

- El ID `HOST-NNN` es **único y no reutilizable** en todo `tests/host/` (no por categoría). Agrupar físicamente **no renumera**: los IDs se conservan para no romper las referencias de la documentación.
- Un test nuevo toma el siguiente número libre (máximo + 1) y se coloca en su categoría.
- `tools/check/test-numbering.mjs` valida: sin duplicados, catálogos de categoría y directorios 1:1, y coherencia ID↔directorio.

## 4. Cómo añadir un test

1. Elige plataforma, nivel y categoría.
2. Colócalo en `tests/host/<categoría>/NNN_<nombre>/` (o `tests/<plataforma>/<nivel>/NNN_<nombre>/`).
3. Escribe `src/main.cpp` (host) o el `main.cpp` on-target con `g_eng_run_status` y verificación determinista.
4. Añade el `README.md`.
5. Regístralo en el catálogo de la categoría.
6. Si la API que cubre sube a `engine/`, enlázalo desde el doc-map.

## 5. Cómo correr

- Todos los host: `bash ./tools/run-host-tests.sh`.
- Una categoría: `bash ./tools/run-host-tests.sh --category graphics`.
- Un test: `bash ./tools/run-host-tests.sh tests/host/graphics/016_ham_scene`.
