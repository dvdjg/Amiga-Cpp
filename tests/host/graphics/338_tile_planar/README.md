# HOST-338 — decode de tiles indexados → planos (`decode_2bpp_planar`)

Respalda `engine/include/eng/graphics/tile_planar.hpp`: convierte una imagen con `planes` planos
de bits **secuenciales** (plano 0 fila a fila, plano 1 a continuación…) a **planos contiguos** con
fila alineada (`row_bytes`) y `plane_stride` propio — el formato del Blitter y de los drivers de
tiles. Es un decoder de assets **genérico** (N bits por píxel empaquetado por planos). Cubre:

- copia de planos y offset `plane_stride`;
- filas de 16 px (`row_bytes=2`) y 3 planos;
- rechazo de argumentos inválidos (ancho no múltiplo de 8, puntero nulo).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/338_tile_planar
```
