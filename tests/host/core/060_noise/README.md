# HOST-060 — ruido procedural genérico (`eng/core/math/noise.hpp`)

Respalda `value_noise1/2/3` y `fbm2` (suma de octavas), genéricos sobre el escalar. El
MISMO algoritmo se ejecuta con `double` y con `MiniFloat16` sobre las mismas entradas.

## Qué cubre

- **Determinismo**: dos llamadas idénticas dan el mismo `raw`; seeds distintos dan
  valores distintos.
- **Rango**: `value_noise2`/`fbm2` se mantienen en `[0,1]` en un barrido amplio.
- **MF vs `double`**: misma rejilla; diferencias ~`1.5e-3` (1D), `2.2e-3` (2D),
  `2.0e-3` (3D) y `4.6e-3` (fbm de 5 octavas).
- **fbm de 7 octavas**: finito y en `[0,1]`.

## Cómo está implementado

Hash de celda **splitmix32** (buena difusión en los bits altos, que son los que se
leen) combinando ejes con `rotl32` de `core/random.hpp`; interpolación con `smoothstep`
de `core/interp.hpp`. Ligado por `require_division<S>` (el valor de rejilla se normaliza
a `[0,1)`, 1024 niveles). En 68000 el hash paga dos `__mulsi3` por evaluación; en una
demo de ruido se usa en el precalculado, no por píxel y frame.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/060_noise
```
