# Librería de escalares: base genérica (MF, float, double, fixed…)

Este documento fija **cómo se estructura** la matemática del engine para que los
algoritmos se escriban **una vez** y sirvan con cualquier escalar (`MiniFloat16`,
`float`, `double`, `Fixed<...>`, un complejo de usuario…). Es la base sobre la que se
añaden interpolación, geometría, ruido, splines, etc.

## 1. Objetivo

- **Un algoritmo, muchos escalares**: `lerp`, `normalize`, `smoothstep`… no nombran el
  tipo; lo reciben como parámetro de plantilla `S`.
- **Coste visible y sin sorpresas**: el escalar solo aporta operaciones (`+ - * /`,
  comparaciones) y rasgos; el algoritmo no conoce hardware ni asigna.
- **Límites explícitos**: cada algoritmo declara su dominio y lo **comprueba al
  compilar** cuando el dato lo permite; con `MiniFloat16` las limitaciones numéricas
  quedan escritas y no se descubren por sorpresa en runtime.

## 2. Capas

```
   ┌───────────────────────────────────────────────────────────────┐
   │ A. Algoritmos genéricos  (eng/core/*.hpp, header-only)         │
   │    linalg · interp · geometry · (futuro: noise/spline/stats)   │
   │    solo plantillas; NO conocen el escalar concreto             │
   ├───────────────────────────────────────────────────────────────┤
   │ B. Rasgos del escalar (puntos de extensión)                    │
   │    scalar_traits<S>   álgebra: zero/one/from_int/norm_from     │
   │    numeric_traits<S>  límites: max_finite/min_normal/epsilon…  │
   │    scalar_sqrt<S>     sqrt para length/normalize               │
   ├───────────────────────────────────────────────────────────────┤
   │ C. Escalares concretos                                         │
   │    Fixed (fixed.hpp) · MiniFloat16 (minifloat.hpp)             │
   │    + matemáticas del escalar (minifloat_math.hpp)              │
   │    optimizaciones de CPU: eng/cpu/m68k/* (muls.w, tablas)      │
   ├───────────────────────────────────────────────────────────────┤
   │ D. Backend / código no-plantilla  (eng/src/platform/…)         │
   │    registros, DMA, copper…: lo que NO puede ser plantilla      │
   └───────────────────────────────────────────────────────────────┘
```

Notas de estructura:

- **A es header-only** porque son plantillas y el engine es freestanding (sin enlazado
  de librería, todo `inline`/`constexpr`). No hay `.cpp` para un algoritmo mientras sea
  plantilla.
- **Las especializaciones viven en la capa B**: añadir un escalar = especializar sus
  rasgos, no tocar los algoritmos.
- **Las optimizaciones por CPU también son especializaciones** (capa C), p. ej.
  `eng/cpu/m68k/arith.hpp` fuerza `muls.w`. Un `.cpp` solo aparece para código que **no
  puede** ser plantilla: el backend de plataforma (D) o una rutina ASM concreta en
  `support/`. Esa es la separación pedida (cabeceras con especializaciones; `.cpp` para
  lo no-plantilla).

## 3. Límites en compilación (`numeric_traits.hpp`)

`numeric_traits<S>` describe el rango y la precisión del escalar (todo `double`, **solo
en compilación**; no genera código):

| Campo | Significado |
|---|---|
| `max_finite` | mayor valor representable |
| `min_normal` | menor valor normal (ulp en el extremo) |
| `epsilon` | ulp relativo cerca de 1.0 |
| `is_fractional` | representa fracciones |
| `has_division` | tiene división (el `Fixed` del núcleo **no** la tiene a propósito) |
| `has_inf` / `has_nan` | satura a ∞ / tiene NaN |
| `to_double(S)` | valor como `double` para las comprobaciones |

Sobre ellos se apoyan los guards, que convierten un uso inseguro en un **error de
compilación con mensaje**:

```cpp
// ¿cabe el intervalo en el escalar? (consteval, usable en static_assert)
static_assert(eng::math::range_fits<er::q12, -7.9, 7.9>());
static_assert(!eng::math::range_fits<er::q12, -10.0, 10.0>());

// dentro de un algoritmo: exige el rango / la división que necesita
template <typename S> constexpr S remap(...) {
    eng::math::require_division<S>();      // si S no divide -> mensaje claro
    ...
}
```

Para valores **constantes** hay comprobación de dominio en la propia evaluación
constante (`if consteval` + `in_range`), que avisa si una llamada a una función con
dominio acotado se hace con una constante fuera de rango; con valores de runtime el
algoritmo satura según su contrato. El dominio se documenta además en cada cabecera.

Añadir un escalar nuevo = especializar `scalar_traits<S>` (álgebra), `numeric_traits<S>`
(límites) y `scalar_sqrt<S>` solo si se usan `length`/`normalize`.

## 4. Límites por algoritmo (lo que hay que leer)

| Algoritmo | Escalar | Límite / precaución |
|---|---|---|
| `lerp(a,b,t)` | `MiniFloat16` | pierde el incremento si `\|b-a\| < \|a\|/2048` (early-out de exponente) |
| `remap`/`inv_lerp` | `MiniFloat16` | usan la división del tipo (~1e-3); resultado fuera de `[2^-14, 65504]` **satura** |
| `normalize` | `MiniFloat16` | `length_sq` bajoflow si las componentes < ~0.008 → 0 y división por ∞ |
| `length_sq` | `MiniFloat16` | desborda si las componentes > ~255 |
| `reflect`/`project` | `MiniFloat16` | normal/`onto` no nula; dirección ~10 bits |
| `sqrt`/`exp`/`log`/`sin`… | `MiniFloat16` | ver la tabla de `MINIFLOAT16.md` §7; las constantes fuera de dominio **fallan al compilar** (`if consteval` + `mf16_domain_*`) |
| `value_noise`/`fbm` | `MiniFloat16` | valor de rejilla con 1024 niveles; en `fbm` la amplitud de octava profunda puede bajoflow a 0 |
| `remap`/`inv_lerp`/`normalize` | `Fixed` | **no compila** (sin `operator/`), por diseño |

El detalle del escalar de 16 bits está en [MINIFLOAT16.md](MINIFLOAT16.md); el modelo del
álgebra, en [MATH_LIBRARY.md](MATH_LIBRARY.md).

## 5. Cabeceras de la capa A

| Cabecera | Contenido |
|---|---|
| `core/linalg.hpp` | `Vec`/`Mat`/`Affine`, `dot`, `transform`, `scalar_traits` |
| `core/interp.hpp` | `clamp`/`saturate`/`lerp`/`inv_lerp`/`remap`/`step`/`smoothstep`/`smootherstep` |
| `core/geometry.hpp` | `length(_sq)`/`distance(_sq)`/`normalize`/`vscale`/`vlerp`/`cross2`/`perp`/`rotate2`/`project`/`reject`/`reflect` |
| `core/noise.hpp` | `value_noise1/2/3` (lattice [0,1), interpolado con `smoothstep`) y `fbm2` (octavas); hash sin multiplicación (reutiliza `random.hpp`) |
| `core/numeric_traits.hpp` | rasgos numéricos y guards de compilación |
