# HOST-444 — noise_grid

Test host de la **rejilla de nodos de ruido** (`NoiseGrid2` y los atajos
`fbm2_cached`/`turbulence2_cached`/`ridged2_cached` en `eng/core/math/noise.hpp`).

## Qué cubre

- **Equivalencia bit a bit** con las funciones libres (`fbm2`/`turbulence2`/`ridged2`): mismas
  operaciones, mismo orden (misma celda, mismo hash, mismos `smoothstep`/`lerp`). Se comparan
  24×24 coordenadas × seeds {1, 0xABCDEF} × octavas {1,3,4} × periodos {1,2,4} con **dos
  escalares** (`MiniFloat16` y `float`); cualquier diferencia es fallo (para MF se compara el
  valor exacto, no una tolerancia).
- **Repliegue**: `period = 0` (no tileable) y rejilla que no cabe (`period 32` con 6 octavas →
  `build` devuelve `false`) caen a la función libre con resultado idéntico.
- **Reconstrucción**: alternar parámetros (seed/octavas/periodo) reconstruye la rejilla
  estática y sigue coincidiendo.
- **Periodicidad**: `f(x)` y `f(x + period)` coinciden (tileable).

## Por qué existe

En la generación de tiles el ruido de rejilla recalcula 4 hashes de esquina por píxel y
octava; con la rejilla se calculan una vez (`p²` nodos por octava) y por píxel queda solo la
interpolación. Es la diferencia entre decenas de segundos y milisegundos en un 68000 (el
atlas del proc tarda ~20× menos en la carga de la demo 119). La equivalencia exacta garantiza
que ninguna validación visual cambia: el `golden` de `HOST-443` lo ancla también.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/444_noise_grid
```
