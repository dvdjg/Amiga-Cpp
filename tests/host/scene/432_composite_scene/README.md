# HOST-432 — fachada de sprites compuestos (`eng::CompositeScene`)

Valida `eng/api/objects.hpp` (F1, nivel A): el juego da de alta contenidos multi-parte y
avanza ticks; el engine materializa cada parte según la política (Sprite HW si su forma lo
admite y hay canal, BOB si no), emite los BOB al `FramePlan` y publica los Sprite HW con
`present(sched, arm_line)` — **sin exponer** intents, canales, placements ni `SpriteManager`.

Cubre: alta/escena llena/`remove`, estado por actor (`facing`/secuencia) y `tick`, caja de
parte espejada, hitboxes por la fachada, emisión con política **por parte** (piernas y parte
grande a BOB; torso de 4 planos y 16 px a par *attached* cocinado en el pool Chip),
`present` con los `SPRxPT/POS` de los dos canales del par, degradación controlada del par sin
pool Chip y modo global `Bob` (todo por Blitter).

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/scene/432_composite_scene
```
