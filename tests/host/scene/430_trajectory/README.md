# HOST-430 — trayectorias, formaciones y pool de entidades (`eng/scene/`)

Valida la capa de movimiento de F2 del `ROADMAP_JUEGO_SPRITES_BOBS.md` §5:

- `trajectory.hpp`: seguimiento **genérico sobre el escalar** (se ejercita con `s32` y `float`),
  polilínea relativa/absoluta con duración por punto, `loop` y `finished`; ruta **Bézier** con
  extremos exactos, punto medio `(p0+3p1+3p2+p3)/8` y **tangente** (orientación).
- `formation.hpp`: oleadas con `delay_ticks` por miembro y offset respecto al origen; el cruce de
  delays por ticks acumulados no pierde miembros (incluido un salto de 5 ticks).
- `entity_pool.hpp`: `spawn` con/sin trayectoria (posición del primer punto), avance con
  `prev_x/prev_y`, fin de proyectil, culling por rectángulo y pool lleno (`0xffff`).

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/scene/430_trajectory
```
