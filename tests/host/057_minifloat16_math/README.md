# HOST-057 — matemáticas clásicas sobre `MiniFloat16` (`eng/core/minifloat_math.hpp`)

Fija `sqrt`, `exp`, `log`, `pow` y trigonometría (`sin`/`cos`/`tan`) implementadas
**solo con aritmética de 16 bits** (sin `float`, sin `libgcc`), comparándolas contra
`std::` sobre **entradas idénticas** (ya redondeadas a `MiniFloat16`).

## Qué cubre

- **Valores simples/exactos**: `sqrt(4)=2`, `sqrt(1)=1`, `exp(0)=1`, `log(1)=0`,
  `pow(a,0)=1`, `sin(0)=0`, `cos(0)=1`, `sin(π/2)≈1`, `cos(π)≈-1`.
- **Barridos** sobre todo el rango finito (sqrt/pow/log) y `[-11,11]` (exp):
  - `sqrt` rel ≈ `1.0e-3`, `exp` rel ≈ `6.3e-4`, `log` rel ≈ `2.5e-3`
    (abs ≈ `1.4e-2` en el extremo), `pow` rel ≈ `1.1e-2`.
- **Trigonometría** en `[-2π, 2π]`: `sin` abs ≈ `1.0e-3`, `cos` abs ≈ `2.0e-3`,
  `tan` rel ≈ `9.2e-3` (con `|cos| > 0.1`; cerca del polo se degrada).
- **Fronteras/dominios**: `sqrt(x<0)→∞`, `log(0)→−∞`, `log(x<0)→+∞`,
  `exp(20)→∞`, `exp(−20)→0`, `pow(0,+)→0`, `pow(0,0)=1`, base negativa → ∞.

## Cómo está implementado (y por qué es rápido)

- **`exp`**: `z = x·log2e` en Q4.11 con `muls.w`; `z = n + f`, `2^f` con serie en
  Q1.14 y `2^n` ajustando el exponente. Sin el "partir y elevar al cuadrado", que
  amplificaba el error por `2^s`.
- **`log`**: separa el exponente (`m` en [1,2)) y usa la serie de `atanh`.
- **`sqrt`**: Newton sobre la mantisa normalizada (exponente par por construcción).
- **`sin`/`cos`**: reducción Cody-Waite (`π/2 = 1.5 + c2 + c3`; `x − n·1.5` es exacto
  por Sterbenz) y Taylor en `[-π/4, π/4]`.

Verificado en el `.o` de m68k: usa `muls.w` y tablas, sin `divs`/`divu` ni libcalls de
coma flotante.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/057_minifloat16_math
```
