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
- **HUD**: franja de 48 px **de UN bitplane** (paleta propia de 2 colores; la zona de
  overlay conmuta BPLCON0/BPLxPT + COLOR00/01) con `PROC WORLD SCROLL v1.0.<build>` +
  contador de frames (norma del repo). El texto se imprime con `Surface::draw_text_words`:
  **dos glifos por palabra de 16 px**, ocho escrituras de palabra por par (glifos + fondo de
  golpe), nunca píxel a píxel — el HUD de depuración debe ser lo más sencillo posible.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --warp
node tools/debug/measure-fps.mjs 120_proc_world_scroll A500_debug --json
```

## Ablaciones de medición (scaffolding compilado, coste cero por defecto)

Las macros de build `K120_SKIP_COMPOSE`, `K120_SKIP_UPDATE`, `K120_SKIP_INSTALL` y
`K120_SKIP_HUD` saltan una etapa del frame para cuantificar su coste con `measure-fps` y
`EXTRA_DEFINES` (p. ej. `EXTRA_DEFINES='-DK120_SKIP_HUD=1'`). Son la sonda de presupuesto de
la demo (procedimiento A/B en `docs/guides/optimization/METODO_OPTIMIZACION.md`); sin define,
el código compila la ruta normal y no tienen coste en runtime.

## Estado (ver [`VALIDATION.md`](VALIDATION.md))

Render, scroll y **cadencia VERIFICADOS en el alcance probado**: 48,7 fps (debug y release,
1,027 campos/frame) con el HUD incremental y cero candidatos de flicker; la referencia
estática 119 mide 49,85 fps con el mismo bucle. El techo de 37 fps anterior era el HUD
completo, no el camino de plan/composición (ablacionado: por debajo del ruido). Queda
registrado con medidas y visión en la validación.
