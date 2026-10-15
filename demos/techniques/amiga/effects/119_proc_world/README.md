# Demo 119 — PROC WORLD (terreno procedural de `eng::gfx::proc`)

Monta y muestra en un Amiga 500 un **mundo de terreno generado proceduralmente** por
`engine/include/eng/graphics/proc/` (la librería de bitmaps procedurales; ficha canónica:
[`PROCEDURAL_BITMAPS.md`](../../../../../docs/engine/architecture/PROCEDURAL_BITMAPS.md)):

- **Atlas de materiales** (22 materiales × 3 variantes, paleta de 16 colores) y **set de 42
  piezas** de terreno con bordes casados (`Edge`/`Feature`), cocinados en el host con la
  misma librería (`MiniFloat16`, determinista byte a byte; lo ancla el golden `HOST-443`).
- **Colocador** (`terrain::place_world`): río de norte a sur con meandros (las curvas se
  derivan con `TerrainTileSet::add_variant`), camino con **puente** donde cruza el cauce,
  castillo 3×3 con **torre** y **puerta**, aldea, cueva, sierra y lago; todas las uniones
  se validan con `layout_is_connected`.
- **Composición y display**: el mapa 10×7 tiles se compone en un framebuffer indexado y se
  publica UNA vez con `eng::IndexedDisplay` (C2P por el engine en carga). El frame es
  estático: coste ~0 por frame.

## Por qué el atlas/set están precocinados

La generación completa del atlas en un 68000 cuesta **~3,4 G de ciclos** (medido con el
periférico de depuración: el ruido paga `__mulsi3` en cada hash y una textura de 32×32
ronda 50 M de ciclos; detalle y evidencias en [`VALIDATION.md`](VALIDATION.md)). Por eso la
demo aplica el patrón del repo para assets de coste alto (como la 201 con su banco de
tiles): **cocinar en host** (`src/prebuild.sh` + `tools/gen_assets.cpp`, hook de
`build-demo.sh`) y **encajar en carga** lo que sí es barato: encajar los 42 KB de piezas,
colocar el mundo y publicar. La carga medida en la demo es ~**1,1 M de ciclos** (~0,16 s).

```text
  host:  gen_assets (misma librería, MiniFloat16)  →  out/assets/proc-world/proc_world_assets.h
  amiga: encajar set → place_world (casación) → componer (10×7) → present (C2P) → VBlank
```

## Franja de versión

La franja inferior de 16 px (norma del repo) muestra `PROC WORLD v<major>.<minor>.<build>`
y `LOAD <NNNN>KC PIECES 45`. El contador de frames/fps vive en la ventana de depuración de
WinUAE y en la telemetría del runner.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/effects/119_proc_world
node tools/debug/measure-fps.mjs 119_proc_world A500_debug --json
```

Regenerar los assets cocinados (borra el header): `rm out/assets/proc-world/proc_world_assets.h`.

## Contrato que ilustra

- El **mismo** contenido que los dumps host (`out/playground/proc-bitmaps/`): semillas
  compartidas (atlas `0x7E5A1`, set `0xBEE5`, mapa `0xBEE5`) y generación determinista.
- Las piezas encajan por construcción (`can_place`/`layout_is_connected`): el test host
  `HOST-442` verifica las uniones y `HOST-443` congela el render.
- Display verificado en hardware: captura + zonas evaluadas con el modelo de visión local
  (Ollama) y regresión con gate de parpadeo — ver [`VALIDATION.md`](VALIDATION.md).
