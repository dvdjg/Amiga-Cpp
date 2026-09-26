# HOST-337 — capa declarativa y regiones con **técnica genérica** — F4c (modelo)

Respalda `engine/include/eng/scene/world.hpp`: la técnica de cada región es **genérica**, no solo
scroll:

- **modo de display** (`graphics::composition::SceneMode`: `Standard`/`Ham`/`Ehb`/
  `DualPlayfield`/`CopperChunky`) **×**  **scroll** (`ScrollKind`: `None`/`Fine`/`BlitterColumns`/
  `CopperRing`/`CopperSplit`), con **coste declarado** (`RegionCost`/`region_cost`);
- `WorldRegion { top, bottom, playfield, mode, scroll, planes }` y `World::add_region/region`;
- `Layer` **pide** `ScrollKind` y `LayerPlayfield` (no elige hardware).

Así una región puede ser un DPF con scroll por Copper (`CopperRing`), otra **copper-chunky** en
los 48 px inferiores (`mode=CopperChunky`, `planes=0`), o un playfield con **scroll por columnas
de Blitter** (técnica tipo *robocod*, demo 112). El planner (F4c) valida con `region_cost` y
**degrada** (p. ej. `CopperSplit`→`CopperRing`→`Fine`) con `ConfigError`. Ver
`OBJECT_SYSTEM.md` §15.8.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/337_layer_plan
```
