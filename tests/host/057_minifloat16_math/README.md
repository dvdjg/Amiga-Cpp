# HOST-057 — matemáticas clásicas sobre `MiniFloat16` (`eng/core/minifloat_math.hpp`)

Fija `sqrt`, `exp`, `log`, `log2`/`log10`, `pow`, `hypot`, trigonometría
(`sin`/`cos`/`tan` + `sincos`) e inversas (`atan`/`atan2`/`asin`/`acos`) implementadas
**solo con aritmética de 16 bits** (sin `float`, sin `libgcc`), comparándolas contra
`std::` sobre **entradas idénticas** (ya redondeadas a `MiniFloat16`).

## Qué cubre

- **Valores simples/exactos**: `sqrt(4)=2`, `exp(0)=1`, `log(1)=0`, `log2(8)=3`,
  `pow(a,0)=1`, `sin(0)=0`, `cos(0)=1`, `atan(1)≈π/4`, `hypot(3,4)≈5`.
- **`pow` con exponente entero**: camino exacto por cuadrado y multiplicación
  (`pow(2,10)=1024`, `pow(-2,3)=-8`, `pow(-2,4)=16` exactos) y base negativa.
- **Barridos** sobre todo el rango finito (sqrt/pow/log/log2/log10) y `[-11,11]` (exp):
  - `sqrt` rel ≈ `1.0e-3`, `exp` rel ≈ `6.3e-4`, `log` rel ≈ `2.5e-3`,
    `log2` abs ≈ `7.9e-3`, `log10` abs ≈ `4.4e-3`, `pow` rel ≈ `1.1e-2`,
    `hypot` rel ≈ `2.1e-3`.
- **Trigonometría** en `[-2π, 2π]`: `sin` abs ≈ `1.0e-3`, `cos` abs ≈ `2.0e-3`,
  `tan` rel ≈ `9.2e-3` (con `|cos| > 0.1`), `atan` abs ≈ `1.2e-3`, `atan2` abs
  ≈ `2.1e-3`, `asin`/`acos` abs ≈ `1.8e-3`, `sincos` = `sin`/`cos` en una pasada.
- **Fronteras/dominios**: `sqrt(x<0)→∞`, `log(0)→−∞`, `log(x<0)→+∞`,
  `exp(20)→∞`, `exp(−20)→0`, `pow(0,+)→0`, `pow(0,0)=1`, base negativa con exponente
  no entero → ∞, `asin/acos` fuera de `[-1,1]` → ∞, `hypot(0,0)=0`, `hypot(∞,·)=∞`.

## Cómo está implementado (y por qué es rápido)

- **`exp`**: `z = x·log2e` en Q4.11 con `muls.w`; `z = n + f`, `2^f` con serie en
  Q1.14 y `2^n` ajustando el exponente. Sin el "partir y elevar al cuadrado", que
  amplificaba el error por `2^s`.
- **`log`/`log2`/`log10`**: separan el exponente (`m` en [1,2)) y usan la serie de
  `atanh`; `log2` es exacto en potencias de dos.
- **`sqrt`/`hypot`**: Newton sobre la mantisa normalizada; `hypot` escala por el mayor
  para no desbordar al elevar al cuadrado.
- **`sin`/`cos`/`sincos`**: reducción Cody-Waite (`π/2 = 1.5 + c2 + c3`; `x − n·1.5` es
  exacto por Sterbenz) y Taylor en `[-π/4, π/4]`; `sincos` reduce una sola vez.
- **`atan`**: minimax de grado 9 en `x²` sobre `[0,1]` y `atan(x)=π/2−atan(1/x)`;
  `asin`/`acos` se apoyan en `atan2` + `sqrt`.
- **Inline forzado** (`[[gnu::always_inline]]`) en `exp`/`sqrt`/`log`/`pow`/trig y los
  helpers: en el `.o` de m68k no emiten símbolo propio (quedan fundidos con el
  llamador) y no hay símbolos indefinidos.

Verificado en el `.o` de m68k: usa `muls.w` y tablas, sin `divs`/`divu` ni libcalls de
coma flotante.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/057_minifloat16_math
```
