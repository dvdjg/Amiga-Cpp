# Demo 063 — sprites compuestos (`eng::CompositeScene`, F1)

Tutorial de la fachada de **sprites compuestos**: el juego describe contenido multi-parte
(`CompositeVisual`: partes con su `Visual`, offsets respecto al ancla, frames por parte y
secuencias) y estado (`CompositeState`: posición, secuencia, espejo); el engine decide la
materialización de cada parte (**Sprite HW** si su forma lo admite, **BOB** si no), emite los
BOB al `FramePlan` y publica los Sprite HW con **una sola llamada** (`present`) — sin que el
juego vea intents, canales, placements ni `SpriteManager`.

Qué muestra: dos personajes de tres partes (piernas y torso **BOB** con dos frames de andar,
cabeza **Sprite HW**) cruzan la pantalla en direcciones opuestas, giran al llegar a los bordes
(espejo) y alternan las secuencias `idle`/`walk` cada 64 frames. Fondo de estrellas y luna
(bitplanes estáticos fuera de las cajas de borrado) y suelo liso (el borrado por caja exige
fondo liso bajo los personajes).

API: `eng/api/api.hpp` (`CompositeScene`, `CompositeVisual`, `Box`) + `scene::clear_box` y el
backend Amiga. El borrado del rastro es por caja (política del juego, como en `BobLayer`).

Validación: HOST-429 (contenido/estado/materializadores) y HOST-432 (fachada completa, par
*attached* y degradación); demo con captura y visión local (dos personajes, tres partes cada
uno, sin artefactos).

```bash
bash tools/build/build-demo.sh demos/features/engine/amiga/063_composite_actors --debug
bash tools/run/run-demo.sh demos/features/engine/amiga/063_composite_actors --keep-running
```
