# HOST-101 — worley/cellular y variantes de fbm

Respalda las incorporaciones a `engine/include/eng/core/math/noise.hpp`.

## Qué cubre

- **`worley2_sq`** (cellular en distancia al cuadrado) y **`worley2`** (euclídea, con
  `sqrt`), con mancha de puntos de característica por celda y rejilla 3×3; opcional
  periódico.
- **`turbulence2`** (`|2n−1|` en octavas, nubes) y **`ridged2`** (`(1−|2n−1|)²`, crestas).
- Rango, determinismo y periodicidad; `float`/`double` y `MiniFloat16`.

## Límites

Normalizan con `div_norm` (la división explícita del escalar), así que funcionan con
cualquier escalar que la ofrezca, incluido `Fixed<s32,E>` (host/68020). El fixed 4.12 (±8)
queda fuera por **rango**: la rejilla tiene 1024 niveles. `worley2` exige además `sqrt`
(`fixed_math`).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/101_noise_extra
```
