# HOST-056 — coma flotante de 16 bits `MiniFloat16` (`eng/core/math/minifloat.hpp`)

Fija el formato, las conversiones y la aritmética del escalar de 16 bits `MiniFloat16`
(1|5|10, sesgo 15) y comprueba que funciona como escalar de la librería genérica
(`Vec`/`Mat`/`Affine` de `eng/core/math/linalg.hpp`).

## Qué cubre

- **Fronteras del formato**: cero, uno, `2^-14` (mínimo normal), `65504` (máximo
  finito), overflow a ∞, underflow a 0 y el redondeo del borde inferior
  (`[2^-15, 2^-14) -> 2^-14`).
- **Orden total**: los negativos se comparan bien pese a la representación
  signo-magnitud, y `-0 == +0`.
- **Enteros exactos** `0..2048`.
- **Barrido escalar** de `+ - * /` (magnitudes `2^-13..2^15`, ambos signos) contra la
  operación equivalente en `float` sobre las **mismas entradas redondeadas**.
- **Matrices** `2x2`, `3x3` y `4x4`: suma, resta, producto, traspuesta, `Mat*Vec`,
  determinante, inversa analítica 2x2/3x3, resolución `A*x=b` y transformaciones
  afines (`transform`/`compose`). Otra vez contra `float` con entradas idénticas.
- **500 matrices 3x3 aleatorias** (LCG determinista) para barrer muchos casos.
- **Rasgos** `scalar_traits<MiniFloat16>` (`zero`/`one`/`from_int`/`to_int`).

## Cómo se mide el error (importante)

El producto de matrices y `Mat*Vec` acumulan términos; si el resultado cae cerca de
cero por cancelación, el error *relativo* al resultado no acota nada. Por eso esos
casos se miden **normalizados por la suma de `|términos|`** (una cota del error de
redondeo real). Los errores elementales observados son del orden de `2^-11 ≈ 5e-4`,
que es el límite de 10 bits de mantisa; las tolerancias del test (1e-3 a 2e-2 según la
operación) acotan holgadamente y a la vez cazan cualquier regresión gruesa (p. ej. una
normalización o un signo mal).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/056_minifloat16
```
