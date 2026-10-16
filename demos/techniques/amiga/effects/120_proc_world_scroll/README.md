# Demo 120 — PROC WORLD SCROLL (mundo de biomas con scroll X-Limited)

Tutorial de la **integración del mundo procedural con el driver de scroll**: el mapa de
biomas de 40×28 celdas (generado **en compilación**, como en la 119) se recorre con el
**corkscrew X-Limited** del engine — solo se blittea la columna que entra al cruzar cada
límite de 16 px; el resto de la pantalla no se toca.

- **Mundo**: `generate_biome_world` constexpr (40×28 celdas de 16 px, paleta de 32 colores,
  ~160 piezas) — el `build.args` sube los límites `constexpr` (~1,5 min extra de build); la
  vía OFFLINE de la 119 (`tools/gen_assets.cpp`) emite los mismos datos.
- **Banco de tiles**: `indexed_tiles` (1 B/píxel, stride 256): la escena convierte el
  tilebank indexado constexpr en el banco de bloques interleaved al arrancar, con
  **5 planos estándar** (sin EHB). El builder indexado es genérico en planos.
- **Scroll**: `XlimitedScene` + `ScrollConsts` (divisores en compilación; sin `__udivsi3`
  en el camino caliente), `TileLayerMap` toroidal en X, cámara 1 px/frame.
- **HUD**: franja de 48 px (misma profundidad que el campo: la zona solo conmuta BPLxPT)
  con `PROC WORLD SCROLL v1.0.<build>` + contador de frames (norma del repo).

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --warp
node tools/debug/measure-fps.mjs 120_proc_world_scroll A500_debug --json
```

## Estado (ver [`VALIDATION.md`](VALIDATION.md))

Render y scroll **VERIFICADOS** (captura + visión). El **presupuesto de 50 fps queda NO
VERIFICADO**: en el engine actual el coste por frame del camino de composición del scroll
está inflado (~201 k ciclos/frame, 1,42 campos) y afecta también a la referencia 201
(22,6 fps debug); es deuda de optimización del camino X-Limited (no de la integración:
esta demo rinde MEJOR que la referencia). Queda registrado con medidas en la validación.
