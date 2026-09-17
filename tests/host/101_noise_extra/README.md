# HOST-101 — worley/cellular y variantes de fbm

Respalda las incorporaciones a `engine/include/eng/core/noise.hpp`.

## Qué cubre

- **`worley2_sq`** (cellular en distancia al cuadrado) y **`worley2`** (euclídea, con
  `sqrt`), con mancha de puntos de característica por celda y rejilla 3×3; opcional
  periódico.
- **`turbulence2`** (`|2n−1|` en octavas, nubes) y **`ridged2`** (`(1−|2n−1|)²`, crestas).
- Rango, determinismo y periodicidad; `float`/`double` y `MiniFloat16`.

## Límites

Necesitan división en el escalar (como el resto de `noise.hpp`): `Fixed` sin `operator/`
no compila (por diseño). `worley2` exige además `sqrt`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/101_noise_extra
```
