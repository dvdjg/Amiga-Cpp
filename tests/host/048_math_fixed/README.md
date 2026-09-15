# HOST-048 — escalar fixed-point genérico (`eng::math::Fixed`)

Test **tutorial** del núcleo de la librería de matemáticas
(`engine/include/eng/core/fixed.hpp`). El exponente y la política van **en el tipo**, y
la aritmética los combina en compilación.

## 1. Tipos y formatos

```cpp
using q12 = Fixed<s16, 12>;   // 4.12:  1.0 == 4096   (RATIO)
using q0  = Fixed<s16, 0>;    // entero (LONGITUD)
using q24 = Fixed<s32, 24>;   // 8.24: acumulador exacto de productos 4.12
const q12 a {4096};           // 1.0
const q0  p {10};             // coordenada 10
```

## 2. Aritmética: los exponentes deciden la coherencia

```cpp
a * a                    -> Fixed<s32, 24>   // exponentes SUMAN, representacion ensancha
(a * a).norm<12>()       -> Fixed<s32, 12>   // un solo desplazamiento
(a * a).norm<12>().narrow<s16>() -> q12      // y de vuelta a 16 bits
a + a                    -> q12              // mismo exponente
a + p                    -> ERROR            // 4.12 + entero NO compila
from_int<s16>(3)         -> q0               // conversion explicita
to_int(q12 {8192})       -> 2                // 8192/4096 = 2
```

## 3. Precisión mixta (metaprogramación)

```cpp
using q14 = Fixed<s16, 14>;      // 2.14 (otra precision)
q12 {a} * q14 {b}                -> Fixed<s32, 26>   // exponente 12+14
Fixed<s16,12> + Fixed<s32,12>    -> Fixed<s32,12>    // promueve a la comun
```

La coherencia la resuelve `mul_repr`/`common_repr` en compilación: no hay conversiones
implícitas ni comprobaciones en runtime.

## 4. Políticas: mismo layout, distinta matemática

```cpp
q12_round            // Fixed<s16,12, RoundPolicy>  (redondeo al mas cercano)
q12_sat              // Fixed<s16,12, SaturatePolicy>
sizeof(q12) == sizeof(q12_round) == sizeof(q12_sat)  // 2 bytes: layout intacto
a.retag<RoundPolicy>()   // misma memoria, otra politica (coste cero)
```

## 5. Producto escalar fusionado

```cpp
dot<12>(a, b, c, d)   // (a*b + c*d) >> 12  UNA normalizacion
```

No es un truco: acumular en el exponente del producto es **exacto** (mismo exponente) y
normalizar una vez es **más preciso** que redondear cada producto. El test lo fija con
un caso donde difieren (`8191*1 + 1*1`).

## 6. Coste (medido con `node tools/analyze/codegen-report.mjs`)

```
construccion            instr  muls.w  shifts  libcalls
c_mul_q12                   3       1       0         0
c_mul_mixed                 3       1       0         0     <- 4.12 * 2.14
c_norm_trunc                4       0       1         0
c_norm_halfup               5       0       1         0
c_norm_even                17       0       1         0
c_dot2                      8       2       1         0
c_dot3                     11       3       1         0
c_transform3 (3x3)         44       3       1         0
c_matmul3 (3x3)            45       3       1         0
```

## Ejecutar

```
CXX="C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64\bin\g++.exe" \
  bash tools/run-host-tests.sh tests/host/048_math_fixed
node tools/analyze/codegen-report.mjs
```
