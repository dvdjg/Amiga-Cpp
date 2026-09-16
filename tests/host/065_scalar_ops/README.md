# HOST-065 — primitivas de escalar, easing `_back` y Bézier

Respalda `eng/core/scalar_ops.hpp` (`min`/`max`/`abs`/`sign`/`move_towards`/`deadzone`),
los easing `_back` de `eng/core/interp.hpp` (`ease_in/out/in_out_back`), `smooth_damp` y
`repeat`/`pingpong`, y `bezier2`/`bezier3` de `eng/core/spline.hpp`. Los MISMOS algoritmos
con `double`, `MiniFloat16` y `q12` (fixed 4.12), contra una referencia en `double`.

## Qué cubre

- **`min`/`max`/`abs`/`sign`**: exactos en los tres escalares (solo comparan y niegan).
- **`move_towards`**: clava en el objetivo (no vibra al pasarse) y avanza exactamente el
  paso; **`deadzone`** descuenta la zona muerta conservando el signo.
- **`_back`**: error `~4e-4` (q12) / `~2.4e-3` (MF); extremos exactos (`0→0`, `1→1`) y
  `in_out_back(0.5)=0.5`.
- **`smooth_damp`**: `2^(−rate·dt)` (independiente del paso); no aplica a fixed (sin `exp2`).
- **`repeat`/`pingpong`**: rango `[0,len)` / `[0,len]` y rebote; `repeat` usa `div_norm`.
- **`bezier2`/`bezier3`**: pasa por los extremos y `bezier2(0.5)` = media ponderada.

## Cómo está implementado

`mul_norm` normaliza el producto al escalar (identidad en `float`/`MiniFloat16`, `rescale`
en fixed) y las constantes fraccionarias de `_back` se construyen con
`scalar_const<S>::from` (cuantizadas al exponente del fixed). `bezier` usa de Casteljau
(seis `lerp`, sin constantes enteras) para valer también en fixed.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/065_scalar_ops
```
