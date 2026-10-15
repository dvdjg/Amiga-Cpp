# Demo 119 — PROC WORLD (terreno procedural de `eng::gfx::proc`)

Monta y muestra en un Amiga 500 un **mundo de terreno generado proceduralmente** por
`engine/include/eng/graphics/proc/` (la librería de bitmaps procedurales; ficha canónica:
[`PROCEDURAL_BITMAPS.md`](../../../../../docs/engine/architecture/PROCEDURAL_BITMAPS.md)):

- **Atlas de materiales** (22 materiales × 3 variantes, paleta de **32 colores** — 8 familias
  de tono de 4 niveles — y **paso de halftone**: las texturas se cuantizan a `2·levels−1`
  pasos y los intermedios se pintan como tableros 2×2 entre colores vecinos, así una familia
  de 4 colores muestra 7 tonos aparentes). Set de **69 piezas** cocinadas (catálogo de 45 +
  rotaciones de relleno) y **78 en runtime** (el colocador deriva 9 orientaciones de
  río/caminos con `add_variant`). Todo se cocina en el host con la misma librería
  (`MiniFloat16`, determinista byte a byte; lo ancla el golden `HOST-443`).
- **Colocador** (`terrain::place_world`, con `PlaceParams`): río de norte a sur con meandros
  y **afluente** que desemboca en T desde el borde superior; camino con **puente** donde
  cruza el cauce y **sendero en L hasta la puerta** del castillo (cruce en T + fondo de
  saco); castillo 3×3 con **torre** y **puerta**; aldea, cueva, sierra y **lago junto al
  cauce**; todas las uniones se validan con `layout_is_connected`.
- **Composición y display**: el mapa 10×7 tiles se compone en un framebuffer indexado y se
  publica UNA vez con `eng::IndexedDisplay<5>` (32 colores, 5 planos; C2P por el engine en
  carga). El frame es estático: el coste por frame es solo la telemetría (ver abajo).

## Por qué el atlas/set están precocinados

La generación completa del atlas en un 68000 cuesta **~3,4 G de ciclos** (medido con el
periférico de depuración: el ruido paga `__mulsi3` en cada hash y una textura de 32×32
ronda 50 M de ciclos; detalle y evidencias en [`VALIDATION.md`](VALIDATION.md)). Por eso la
demo aplica el patrón del repo para assets de coste alto (como la 201 con su banco de
tiles): **cocinar en host** (`src/prebuild.sh` + `tools/gen_assets.cpp`, hook de
`build-demo.sh`) y **encajar en carga** lo que sí es barato: encajar las piezas, colocar el
mundo y publicar. La carga medida en la demo es ~**1,2 M de ciclos** (~0,17 s).

```text
  host:  gen_assets (misma librería, MiniFloat16)  →  out/assets/proc-world/proc_world_assets.h
  amiga: encajar set → place_world (casación) → componer (10×7) → present (C2P 5 planos) → VBlank
```

## Franja de versión y telemetría viva

La franja inferior de 16 px (norma del repo) muestra `PROC WORLD v<major>.<minor>.<build>`
(el **build lo inyecta** `build-demo.sh` con el contador persistente `out/.build-number`),
`LOAD <NNNN>KC PIECES 78` y un **contador de frames** (`F nnnnnn`) que se refresca cada
frame por **parcheo planar mínimo** (`IndexedDisplay::patch_text`: solo se reescriben por
planos los dígitos que cambian, sin C2P) — ~1-2 celdas de 8×8 por frame.

**Bajo un SO vivo la espera de VBlank es ACTIVA** (`set_vblank_sleep(false)`, variante 2 del
212): con `Wait()` el libro de display del SO reescribe `INTENA` y borra el `VERTB` del
engine → la tarea queda dormida para siempre (frame congelado a los pocos frames, PC en el
`STOP` de exec). Ver
[`212-vblank-irq-apagada-por-el-so.md`](../../../../../docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md).

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/119_proc_world --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/119_proc_world --warp
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/effects/119_proc_world
node tools/debug/measure-fps.mjs 119_proc_world A500_debug --json
```

Regenerar los assets cocinados (borra el header): `rm out/assets/proc-world/proc_world_assets.h`.

## Contrato que ilustra

- El **mismo** contenido que los dumps host (`out/playground/proc-bitmaps/`): semillas
  compartidas (atlas `0x7E5A1`, set `0xBEE5`, mapa `0xBEE5`) y generación determinista
  (el set de la demo añade además las rotaciones de relleno del generador host).
- Las piezas encajan por construcción (`can_place`/`layout_is_connected`): el test host
  `HOST-442` verifica las uniones y `HOST-443` congela el render (paleta, texturas, piezas
  y mapa con dos semillas).
- Display verificado en hardware: captura + zonas evaluadas con el modelo de visión local
  (Ollama) y regresión con gate de parpadeo — ver [`VALIDATION.md`](VALIDATION.md).
