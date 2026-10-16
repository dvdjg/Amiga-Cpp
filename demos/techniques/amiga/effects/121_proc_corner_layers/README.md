# Demo 121 - PROC STRUCTURES (mundo cocido + motor de tiles X-Limited)


## Búsqueda previa

Evaluado el sistema de actores del engine (`ActorStore`, `actor_emit`, `build_sprite_intents`,
`docs/engine/architecture/OBJECT_SYSTEM.md`) antes de escribir esta demo. Se usa el camino
**directo** del playfield corkscrew (`add_world_bitmap_masked`/`add_world_window`) a propósito:
es una demo-técnica del motor de tiles y de la fontanería del actor planar (bandas de
subpíxel + oclusión) y sirve de banco de verificación del Blitter. La vía de juego (actor
retenido + animación) es `eng/scene/actor.hpp`; su cableado con `Representation::Layer` a
X-Limited está pendiente (§14.8 de `OBJECT_SYSTEM.md`) y esta demo es la candidata a migrar.

Tutorial de la **cadena completa de un mundo de tiles** con el engine: generación procedural
**en compilación** (`constexpr`), **cocido a tileset indexado** y fondo pintado por el
**motor de tiles X-Limited** (`playfield::XlimitedScene`, corkscrew) con el Blitter — sin
C2P ni composición por píxel en runtime.

- **Generación por esquinas (dual-grid)**: `OccupancyMap` + `stamp_river`/`stamp_disc`
  definen río N→S, lago e isla; por celda se elige una de las **16 transiciones** pintadas
  con materiales del atlas (`paint_corner_materials`). Las orillas casan por construcción.
- **Estructuras multi-tile por casación de bordes**: caminos (rectas, codos, fondo de
  saco) con **puente** sobre el cauce, **muralla de castillo 3×3** (esquinas, torre, puerta
  sur alineada al camino, patio), **casas grandes 2×2** (variantes A/B de puerta espejada)
  y **árboles grandes 2×2** (copa continua + tronco) — las partes casan porque comparten
  seed y se pintan por partes (`terrain_paint.hpp`; cubierto por `HOST-448`).
- **Cocido a tileset** (`bake_layered_map`): cada celda se compone UNA vez (suelo → decal →
  overhead) y las celdas idénticas se **deduplican**; el mundo queda en ~50 piezas únicas +
  rejilla de índices (formato del motor de tiles).
- **Fondo por el motor de tiles**: `scene.fill()` blitea las celdas desde el banco
  interleaved al anillo del corkscrew; la cámara es fija. HUD de **1 bitplane** con nombre
  + versión + contador de frames impreso **por palabras** (`Surface::draw_text_words`).
- **Actor por Blitter con oclusión**: bicho de 16×16 (cuerpo 31, color exclusivo) que
  camina 1 px/frame por el carril y **pasa por debajo de las copas**. Por frame: (a) se
  restaura la **ventana de 2 palabras** que ocupaba (copia barata desde el fondo del carril)
  y (b) se dibuja el actor desplazado (`add_world_bitmap_masked`) con **16 bandas de
  subpíxel precomputadas** y máscara `actor & ~copas` (bajo copa no se escribe).
- **Auto-verificación de render** (`COHERENCIA_DE_SETUP.md` §22): el `init` dibuja y
  **relee** el bitmap (cuerpo 31, integridad de las 16 bandas en Chip, 18 sondas) y el bucle
  relee un píxel por frame (3 strikes) — es la guarda que localizó la pérdida de plano 0 al
  mezclar copias + racha cookie-cut en un plan (ver
  `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`; la demo mitiga
  con **dos planes** por frame).

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --warp
node tools/debug/measure-fps.mjs 121_proc_corner_layers A500_debug --json
```

Ablaciones de medición (scaffolding compilado, coste cero por defecto):
`EXTRA_DEFINES='-DK121_SKIP_ACTOR'` desactiva los blits del actor;
`EXTRA_DEFINES='-DK121_FREEZE_ACTOR_RENDER'` los desactiva dejando al actor dibujado tras el
auto-chequeo (sonda estática para capturar el render sin carreras de lectura);
`EXTRA_DEFINES='-DK121_STATIC_ACTOR'` congela la x del actor en 96.

## Estado (ver [`VALIDATION.md`](VALIDATION.md))

Generación + cocido + motor de tiles + actor en hardware; medidas (47,67 fps estables,
flicker 0 candidatos, regresión OK), visión local y alcance acotado en `VALIDATION.md`.
