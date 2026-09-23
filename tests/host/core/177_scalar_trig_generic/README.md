# HOST-177: trig escalar genérica (mismo algoritmo con `float` y con `Fixed`)

Demuestra el patrón de escalar del motor: el algoritmo es una **plantilla sobre el escalar
`S`** y las operaciones concretas las aporta el propio escalar a través de los puntos de
extensión de `core/scalar_math.hpp` (`scalar_sin<S>`, `scalar_cos<S>`, `scalar_sincos<S>`),
más `mul_norm` de `core/linalg.hpp`.

La **misma** función `rotate` se instancia con `float` y con `q12` (`Fixed<s16,12>`); el
ángulo va en **radianes** en ambos casos. Después se normalizan los dos resultados a `double`
y se mide la desviación de la versión fixed.

## Qué comprueba

1. **Intercambiabilidad**: `rotate<S>` compila y da resultados coherentes con `S = float` y
   `S = q12`; la desviación entre ambos (sobre 10 ángulos, incluidos negativos) queda por
   debajo del umbral.
2. **Especialización retro exacta**: `scalar_sin<q12>`/`scalar_cos<q12>` usan la tabla
   `kSinTab` del original (4096 pasos), no una aproximación.
3. **`sincos` en una pasada** coincide con `sin`/`cos` por separado.

La especialización vive **solo** en `eng/retro/fixed_trig.hpp` (el núcleo no nombra el
formato Q): el contrato de radianes es el mismo que el de `float`/`double`.

## Salida de referencia

```
OK: trig escalar generica: misma funcion con float y q12 (desviacion^2 max <umbral>).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/177_scalar_trig_generic
```
