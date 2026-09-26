# HOST-337 — capa declarativa y regiones (`World`/`Layer`) — F4c (modelo)

Respalda `engine/include/eng/scene/world.hpp`: la capa **pide** (no elige hardware):

- `LayerScroll { Static, Fine, XLimited, XYUnlimited }` y `LayerPlayfield { Any, Pf1, Pf2,
  SpriteLayer }` con `set_scroll`/`set_prefer`/`scroll()`/`prefer()`;
- `WorldRegion { top, bottom, playfield, mode, planes }` y `World::add_region`/`region(i)`/
  `region_count`, con rechazo de regiones inválidas (`bottom <= top`).

Es el **modelo** para «la capa pide, el planner dispone»: un DPF de 208 px + una banda de 48 px
a 4 planos se expresan como dos `WorldRegion`. La **validación/degradación** (una `XYUnlimited`
por banda, presupuesto de Copper/planos) es el planner (F4c), ver `OBJECT_SYSTEM.md` §15.8.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/337_layer_plan
```
