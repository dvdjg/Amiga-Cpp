# HOST-493 - Animación rígida 3D

Verifica `eng/scene3d/animation3d.hpp`: `Clip` no propietario con keyframes de traslación q0 y rotación en turns, tracks con target lógico, interpolación lineal, shortest-turns, loop, one-shot y eventos. El test comprueba el punto medio de posición/rotación, que el giro 0.9→0.1 toma el camino corto, que el loop vuelve al inicio, que un one-shot conserva la última pose, que los eventos se consumen una sola vez, los límites de capacidad y la conversión Q4.12 del ratio (sin `from_int(4096)`, que desbordaba y congelaba la interpolación).

Alcance: animación rígida por nodo; no hay skinning, morph targets, clips de asset ni integración automática con `Scene`. La demo `093_rigid_anim` valida el uso con jerarquía y Blitter.

```bash
bash tools/run-host-tests.sh tests/host/sim/493_animation3d
```
