# HOST-390 — cámara toroidal (`Camera2D::reset_ring`)

Valida la **cámara de un mundo que envuelve** (§7): `Camera2D::reset_ring(period_x, period_y,
viewport)` mantiene la posición en `[0, period)` por **envoltura** (en uno o ambos ejes), en
contraste con `reset(...)` (mundo **acotado**, que **recorta**). Es representación pura (sin
hardware): el motor de tiras ya envuelve por su lado (mapa toroidal `MapWords`), y esta cámara da la
representación **acotada** del mundo.

Cubre: envoltura X/Y (`0-1 → period-1`, `period-1+1 → 0`), pasos mayores que el período (módulo),
una cámara **acotada** que recorta, y el caso mixto (envoltura X + recorte Y).

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/scene/390_ring_camera
```
