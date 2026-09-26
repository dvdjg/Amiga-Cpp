# HOST-329 — actores en el mundo retenido (`World` + `ActorStore` + `emit`)

Respalda la parte de actores de `engine/include/eng/scene/world.hpp`. Cubre:

- `reset_actors(...)` (presupuesto de representación) y `add_actor(desc)` con la
  representación elegida por el engine (aquí BOB, porque no hay canales de sprite);
- acceso por `ActorId` (`actor(id)`), `actors()` (el `ActorStore`) y `z`;
- `emit(plan, targets, clip)`: emite los actores al `FramePlan` (orden por superficie/`z`),
  añadiendo los jobs de Blitter; `0` si el mundo está vacío.

El camino de representación por hardware (sprites/copper por objeto) ya está cubierto por
HOST-072 (patrón de jobs del BOB) y HOST-003 (reparto de sprites).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/329_world_actors
```
