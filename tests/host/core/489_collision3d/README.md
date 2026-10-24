# HOST-489 - Colisión 3D mínima

Verifica `eng/core/util/collision3d.hpp` con `float` y `Fixed<s16,4>`: solape esfera-esfera (distancia² sin `sqrt`), esfera-AABB por punto más cercano, AABB-AABB, rayo-AABB por slabs y rayo-esfera con la primera intersección y normal. El rayo usa `scalar_sqrt` del escalar; el test incluye `fixed_math.hpp` para la especialización de `Fixed`.

Alcance: no cubre OBB/SAT 3D, broadphase, cápsulas, heightfield ni contacto con penetración; esos quedan para la siguiente pasada de G6. El test es host puro.

```bash
bash tools/run-host-tests.sh tests/host/core/489_collision3d
```
