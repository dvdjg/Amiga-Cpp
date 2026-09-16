# HOST-060 — ruido procedural genérico (`eng/core/noise.hpp`)

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

Hash de celda **sin multiplicación** (xor/rot/suma, reutiliza `rotl32` de
`core/random.hpp`) para no pagar `__mulsi3` en 68000; interpolación con `smoothstep` de
`core/interp.hpp`. Ligado por `require_division<S>` (el valor de rejilla se normaliza a
`[0,1)`, 1024 niveles). Verificado en el `.o` de m68k: sin `__mulsi3`/`__divsi3` ni
`divs`/`divu`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/060_noise
```
