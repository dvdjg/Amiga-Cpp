# Referencia — `core/math/`

Matemáticas **genéricas sobre el escalar**. La regla del módulo: la cabecera no fija `float`/`Fixed`/`s16`; usa el **vocabulario genérico** y recibe el escalar como parámetro de plantilla. Así el mismo algoritmo se instancia con `float` (host) o `Fixed` (m68k).

## El escalar — `scalar.hpp`

El modo se elige en compilación (`scalar_mode`): *retro16* (`intw = s16`, `real = Fixed<s16,12>`, `coord = Fixed<s16,0>`, `scalar.hpp:51`), *retro32* (`intw = s32`, `scalar.hpp:59`) o *native* (`real = float`, `coord = int`, `scalar.hpp:69`). El código de juego casi nunca lo toca: trabaja con `real`/`coord` a través de la fachada.

## Álgebra genérica — `linalg.hpp`

`scalar_traits<S>` describe cómo multiplicar/dividir y qué accuracy tiene cada escalar (`linalg.hpp:56`); hay especializaciones para `float` y `double` (`linalg.hpp:78`, `:101`). Sobre él se construyen `Vec<N,S>`/`Mat` y las operaciones normalizadas `mul_norm`/`div_norm` (`linalg.hpp:148`, `:162`), que evitan desbordes al operar en punto fijo. `geometry.hpp` añade primitivas (punto-rect, distancias, `div_norm`).

## Punto fijo — `fixed.hpp`

`Fixed<R, Exp, Policy = DefaultPolicy>` (`fixed.hpp:160`) es un entero con la coma en `Exp` bits. La política (`rounding::HalfEven`/`HalfUp`/`TowardNegInf`, `overflow::Saturate`/`Wrap`, `fixed.hpp:60`) decide redondeo y desbordes; `DefaultPolicy` recoge el comportamiento por defecto (`fixed.hpp:105`). `limits<R>` da los extremos de la representación (`fixed.hpp:90`). `fixed_math.hpp` y `fixed_affine.hpp` añaden funciones y afinidades en punto fijo.

## `MiniFloat16` — `minifloat.hpp`

`MiniFloat16` (`minifloat.hpp:80`) es un flotante de 16 bits: **mismo rango útil en menos bytes**, sin FPU. Implementa conversión a/desde `float` (`minifloat.hpp:220`, `:262`) y `operator+`/`-`/`*`/`/` con tablas `constexpr` (recíproco `mf16_rcp`, `minifloat.hpp:152`). Se usa donde un número real de 32 bits desperdiciaría memoria en un A500.

## Resto del módulo

| Cabecera | Contenido |
|---|---|
| `interp.hpp` | `clamp`, `saturate`, `lerp`, `inv_lerp`, `remap`, `step`, `smoothstep`/`smootherstep`, easings (`interp.hpp:46`). |
| `sinetable.hpp` | `SineTable<Amp, Steps>`: tabla seno `constexpr` (`sinetable.hpp:61`). |
| `noise.hpp` | `value_noise1/2/3` + `fbm` con `noise_traits<S>` (`noise.hpp:142`). |
| `isqrt.hpp` / `inv_sqrt.hpp` | raíz cuadrada entera / 1/√x. |
| `fast_div.hpp` | división por potencias de dos. |
| `random.hpp` | PRNG determinista (`u32` exacto en host y m68k). |
| `spline.hpp`, `light.hpp`, `expr.hpp`, `numeric_traits.hpp`, `scalar_ops.hpp` | interpolación por splines, iluminación, expresión genérica, traits numéricos y operaciones de escalar. |

Volver al [índice de `core/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
