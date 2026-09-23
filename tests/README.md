# Batería de tests del engine

Batería de validación del engine organizada por **plataforma**, **nivel** y **categoría** (taxonomía canónica en [`../docs/testing/TAXONOMY.md`](../docs/testing/TAXONOMY.md)). Los tests no son demos de lucimiento: son **tutoriales didácticos verificables**. Cada test incluye un `README.md` con los pasos y, cuando aplica, un script de verificación determinista (canal lateral WinUAE, pixel assertions o equivalencia contra una referencia).

## Ejes

- **Plataforma**: `host` (g++ nativo) · `amiga` (on-target, WinUAE) · `atarist` (futuro) · `megadrive` (futuro).
- **Nivel**: `L0` bare-metal · `L1` unitario host · `L2` integración on-target · `L3` visual.
- **Categoría (dominio)**: core, graphics, field, scene, platform/amiga, ai, sim, board, cards, os, ui, audio, res, parallel.

## Árbol

```
tests/
├── README.md             → este documento (pirámide y taxonomía)
├── host/                 → L1: algoritmos/APIs puras (g++ del host, sin WinUAE)
│   ├── README.md         → índice de categorías
│   └── <categoría>/      → README.md de catálogo + NNN_<nombre>/
├── amiga/                → on-target (WinUAE)
│   └── l0_bare_metal/    → registros custom, bitplanes, copperlist a mano, DMA, Blitter
└── atarist/ · megadrive/ → (futuro)
```

- **Tests HOST** (`tests/host/`): algoritmos y APIs puras que no dependen de hardware. Se compilan con el `g++` del entorno (sin MSVC ni WSL) y corren como binario nativo; son la validación más rápida y determinista. Índice de categorías en [`host/README.md`](host/README.md).
- **Tests L0** (`tests/amiga/l0_bare_metal/`): conocimiento bare metal del Amiga (registros, planos, copper, DMA, Blitter), verificados por canal lateral/pixel assertions. Ver [`amiga/l0_bare_metal/README.md`](amiga/l0_bare_metal/README.md).
- Los niveles `l1_backend/` y `l2_copper_frameplan/` (on-target) se añadirán bajo `tests/amiga/` cuando hagan falta.

## Convenciones de cada test

Cada test vive en `tests/<plataforma>/[<nivel>/]<categoría>/NNN_<nombre>/` y debe contener:

- `src/main.cpp` — código didáctico comentado por capas, sin STL, `gnu++23` freestanding.
- `README.md` — qué hace, qué capas ejercita, qué registros/APIs usa y cómo verificar.
- `verify-<algo>.mjs` (o `.sh`/`.ts`) — script determinista por canal lateral/GDB, cuando aplica.
- `analyze-screenshot.sh` — comprobación de la captura, cuando aplica.

Los tests on-target además exponen `g_eng_run_status`, alcanzan `Ready` y restauran el sistema (o documentan por qué no aplica).

## Numeración

- El ID `HOST-NNN` (host) y el prefijo `NNN_` (on-target) son **únicos y no reutilizables**. Agrupar por categoría **no renumera**.
- Lo valida `tools/check/test-numbering.mjs` (host) en `tools/run-host-tests.sh`.

## Cómo ejecutar

```bash
# Tests host (todos)
bash ./tools/run-host-tests.sh

# Tests host de una categoría
bash ./tools/run-host-tests.sh --category graphics

# Test host concreto
bash ./tools/run-host-tests.sh tests/host/graphics/016_ham_scene
```

Los tests on-target usan el flujo común de demos/tests:

```bash
bash ./tools/build/build-demo.sh tests/amiga/l0_bare_metal/010_display_320x240 --debug
bash ./tools/run/run-demo.sh tests/amiga/l0_bare_metal/010_display_320x240
bash ./tools/analyze/analyze-demo.sh tests/amiga/l0_bare_metal/010_display_320x240
```

Los artefactos se generan en `out/demos/<leaf>/` (build) y `out/run/<leaf>/` (ejecución).

## Cómo añadir un test

1. Elige plataforma, nivel y categoría.
2. Colócalo en `tests/host/<categoría>/NNN_<nombre>/` (host) o `tests/amiga/<nivel>/NNN_<nombre>/` (on-target).
3. Escribe `src/main.cpp` como tutorial: cada paso con su comentario y la capa que toca.
4. Añade el `README.md`.
5. Regístralo en el catálogo de su categoría (`tests/host/<categoría>/README.md`) o en el roadmap on-target.

## Herramientas de verificación usadas

- Canal lateral WinUAE-DBG: `tools/debug/winuae-side-channel.sh`.
- Verificación de símbolos desde el `.map`: patrón de `tools/debug/verify-side-channel-takeover.mjs`.
- Especificación del canal lateral: `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md`.
