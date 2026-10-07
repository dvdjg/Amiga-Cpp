# HOST-429 — sprite compuesto (`eng/scene/composite_actor.hpp`)

Valida el **contenido multi-parte** (`graphics/composite_visual.hpp`) y su capa de juego (F1 del
`ROADMAP_JUEGO_SPRITES_BOBS.md` §5): avance de secuencias por ticks de juego (loop, salto a la
siguiente secuencia y `finished`), geometría de cada parte en pantalla con **espejo** por
`facing_left`, **hitboxes** del frame (explícitas o `bounds` por defecto) y la materialización de
las partes: una `SpriteIntent` por parte apta (dos si el contenido es un par *attached* derivado
del arte) y `BlitJob`s de BOB con el frame vigente.

Cubre: secuencia idle en loop con eventos, `walk → idle` por `next`, secuencia sin `loop` que queda
`finished`, geometría normal/espejada de partes y hitboxes, `bounds` por defecto cuando el frame no
trae `hits`, intents del par *attached* (ATTACH solo en el impar), job cookie-cut de la parte BOB
(minterm/words/height) y `composite_part_data` con el offset de frame (`frame_stride`). Sin
planner (`part_repr` vacío), el materializador de BOB dibuja todas las partes.

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/scene/429_composite_actor
```
