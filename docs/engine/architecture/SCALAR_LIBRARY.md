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

### 3.b Puntos de extensión (vocabulario interno estable)

Estos nombres son el **contrato estable** de la librería: los algoritmos de `core/` se
escriben solo con ellos, y añadir un escalar = especializarlos.

| Punto de extensión | Papel | Por defecto | `Fixed<s16,E>` |
|---|---|---|---|
| `scalar_traits<S>` | álgebra: `zero`/`one`/`from_int`/`norm_from` | cuerpo (`S{}` / `S{1}`) | `from_int` con el exponente; `norm_from` = `rescale` |
| `numeric_traits<S>` | límites y flags (solo compilación) | — | rango `±(2^(bits-1)−1)·2^-E` |
| `mul_norm(a,b)` | producto normalizado al escalar | `norm_from(a*b)` (identidad en float/MF) | `(a*b).rescale<E>().cast<R>()` |
| `scalar_div<S>::op` / `div_norm(a,b)` | división **explícita** | `a/b` | `(a.v<<E)/b.v` con `divs.w`, saturado |
| `scalar_sqrt<S>::op` | `sqrt` para `length`/`normalize` | ADL `sqrt(S)` | con `fixed_math.hpp` (`isqrt`); sin él, no compila |
| `scalar_sin/cos/sincos/tan/asin/acos/atan2<S>::op` | trigonometría (easings, rotación, apuntado) | ADL (`MiniFloat16`); `sincos`/`atan2` con serie `constexpr` para `float`/`double` | con `fixed_math.hpp` (tablas); sin él, no compila |
| `scalar_exp2/log2/exp/log/pow<S>::op` | exponencial/log (`smooth_damp`, `pow`) | ADL (`MiniFloat16`) | con `fixed_math.hpp` (tablas); sin él, no compila |
| `scalar_const<S>::from(double)` | constante fraccionaria de compilación (p. ej. `1.70158` del `_back`) | `static_cast<S>` | cuantiza a `E` bits fraccionarios |

La trigonometría/exponencial de `Fixed` es **opt-in**: vive en `eng/core/fixed_math.hpp` (no la arrastra `scalar_math.hpp`) y se incluye solo donde se usa, con el tamaño de tabla elegible en compilación.

`mul_norm`/`div_norm` son la bisagra: evitan que cada algoritmo tenga que saber si su
escalar cambia de exponente al multiplicar (fixed) o no (float/MF), y permiten que
`lerp`, `cross2`, `rotate2`, `vscale`, `vlerp`, `remap`… sean **el mismo código** para
todos.

El **producto escalar fusionado** (`dot` de `linalg.hpp`) es el caso modélico: acumula
los productos **exactos** (en el exponente del producto) y normaliza **una vez** con
`norm_from` al escalar destino. Funciona igual con `float`, `MiniFloat16` y `Fixed`
(`dot(Vec<3,q12>, Vec<3,q0>) -> q0`), sin redondear producto a producto ni usar `sqrt`.

Añadir un escalar nuevo = especializar `scalar_traits<S>` y `numeric_traits<S>`; los
demás puntos solo si los algoritmos que se vayan a usar los necesitan.

### 3.c Patrón de apuntado (sin `float`)

Tres pasos, todos resueltos por el escalar: dirección → ángulo → rotación. Con `Fixed` basta incluir `fixed_math.hpp`; con `MiniFloat16`, su propia matemática.

```cpp
using V = eng::math::Vec<2, q12>;                  // o MiniFloat16 / Fixed<s16,6>
const q12 ang = eng::math::angle_to(pos, target);  // atan2 (tabla): ángulo hacia el objetivo
const V   dir = eng::math::from_angle(ang);        // (cos,sin) en una pasada (sincos)
const V   aim = eng::math::rotate2(base, ang);     // gira el cañón/sprite por el ángulo
```

`angle_of(v)` da el ángulo de un vector; `angle_to(a, b) = angle_of(b − a)`. El resultado vive en `(-π, π]`; `wrap_angle` lo pliega si se acumula. El **vector debe caber en el formato** (en 4.12, ±8: para deltas de pantalla en píxeles usa `Fixed<s16,6>` o escala el delta). Ver la demo `110_ylimited_shooter` (torreta que sigue al jugador).

## 4. Límites por algoritmo (lo que hay que leer)

| Algoritmo | Escalar | Límite / precaución |
|---|---|---|
| `lerp(a,b,t)` | `MiniFloat16` | pierde el incremento si `\|b-a\| < \|a\|/2048` (early-out de exponente) |
| `remap`/`inv_lerp` | `MiniFloat16` | usan la división del tipo (~1e-3); resultado fuera de `[2^-14, 65504]` **satura** |
| `normalize` | `MiniFloat16` | `length_sq` bajoflow si las componentes < ~0.008 → 0 y división por ∞ |
| `length_sq` | `MiniFloat16` | desborda si las componentes > ~255 |
| `reflect`/`project` | `MiniFloat16` | normal/`onto` no nula; dirección ~10 bits |
| `sqrt`/`exp`/`log`/`sin`… | `MiniFloat16` | ver la tabla de `MINIFLOAT16.md` §7; las constantes fuera de dominio **fallan al compilar** (`if consteval` + `mf16_domain_*`) |
| `value_noise`/`fbm` | `MiniFloat16` | valor de rejilla con 1024 niveles; coordenada constante fuera de `[-2048, 2048]` **falla al compilar**; `period > 0` la hace tileable |
| `lerp`/`smoothstep`/`cross2`/`rotate2`/`vscale`/`vlerp`/`dot` | `Fixed` | **funcionan**: el producto se normaliza con `mul_norm` (el producto de dos fixed cambia de exponente) |
| `smootherstep` | `Fixed` 4.12 | **no compila**: necesita representar el coeficiente 15 y 4.12 llega a ±8 (`require_range`) |
| `remap`/`inv_lerp` | `Fixed` | **funcionan** vía `div_norm` (división explícita y saturante; el núcleo sigue sin `operator/`) |
| `min`/`max`/`abs`/`sign`/`move_towards`/`deadzone` | cualquier `S` | **exactos**: solo comparan y niegan; `move_towards` clava en el objetivo (no vibra al pasarse) |
| `smooth_damp` | `MiniFloat16`/`float` | **no** para fixed (necesita `exp2`); convergencia independiente del framerate |
| `repeat`/`pingpong` | `MiniFloat16` | el cociente de la división lleva ~1e-3 de error y el ajuste de ±1 paso lo absorbe; resultado siempre en rango |
| `ease_*_back`/`bezier2`/`bezier3` | `Fixed` 4.12 | **funcionan**: constantes vía `scalar_const`; los productos intermedios caben en ±8 |
| `dot` fusionado (2-4 pares) | `Fixed` | el acumulador **satura** (3-4 productos de 4.12 superan `s32`) y el estrechado final **satura siempre**, sea cual sea la política de los operandos |
| `normalize`/`length`/`reflect`/`project` | `Fixed` | **no compilan** (sin `sqrt`), por diseño |
| `angle_of`/`from_angle`/`angle_to`/`rotate2(v,ángulo)` | `Fixed`/`MiniFloat16`/`float`/`double` | **funcionan** con la trig del escalar (`atan2`+`sincos`): `angle_of` = ángulo de un vector, `from_angle` = vector unitario, `angle_to` = apuntado hacia un objetivo (`float`/`double` usan `atan2_d`/`sincos_d`, series `constexpr` sin libm); el **vector de entrada** debe caber en el formato (4.12 solo cubre ±8: para deltas de pantalla en píxeles usa un formato mayor como `Fixed<s16,6>` o escala el delta) |
| `value_noise`/`fbm` | `Fixed` | **no compilan**: necesitan división (sin `operator/`) |

El detalle del escalar de 16 bits está en [MINIFLOAT16.md](MINIFLOAT16.md); el modelo del
álgebra, en [MATH_LIBRARY.md](MATH_LIBRARY.md).

## 5. Cabeceras de la capa A

| Cabecera | Contenido |
|---|---|
| `core/linalg.hpp` | `Vec`/`Mat`/`Affine`, `dot`, `transform`, `scalar_traits`, `mul_norm`, `scalar_div`/`div_norm` |
| `core/interp.hpp` | `clamp`/`saturate`/`lerp`/`inv_lerp`/`remap`/`step`/`smoothstep`/`smootherstep`; `smooth_damp` (suavizado exponencial) y `repeat`/`pingpong` (fase); easings `_quad`/`_cubic`/`_back`/`_sine`/`_expo` |
| `core/scalar_ops.hpp` | `min`/`max`/`abs`/`sign`/`move_towards`/`deadzone` (comparación y negación, sin división) |
| `core/geometry.hpp` | `length(_sq)`/`distance(_sq)`/`normalize`/`vscale`/`vlerp`/`cross2`/`perp`/`rotate2` (por `(c,s)` o por **ángulo**)/`angle_of`/`from_angle`/`angle_to`/`project`/`reject`/`reflect` |
| `core/noise.hpp` | `value_noise1/2/3` y `fbm1/2/3` (octavas, `period>0` tileable); hash splitmix32 (2 `__mulsi3` por celda) |
| `core/spline.hpp` | `hermite`/`catmull_rom` y Bézier `bezier2`/`bezier3` (escalar y `Vec<N,S>`) |
| `core/scalar_math.hpp` | puntos de extensión `scalar_sqrt`/`scalar_sin`/`scalar_cos`/`scalar_sincos`/`scalar_tan`/`scalar_asin`/`scalar_acos`/`scalar_atan2`/`scalar_exp2`/`scalar_log2`/`scalar_log`/`scalar_exp`/`scalar_pow` (ADL; `float`/`double` con series `constexpr`) y `scalar_const<S>` |
| `core/numeric_traits.hpp` | rasgos numéricos y guards de compilación |
| `core/scalar.hpp` | tipos generales seleccionables en compilación (`eng::intw`/`real`/`coord`, `ENG_SCALAR_RETRO16`/`RETRO32`/`NATIVE`) |

## 6. Verificación

- Host: `tests/host/059_scalar_math` (interp/geometry con `double`, `MiniFloat16` y
  `q12`), `tests/host/060_noise` (`value_noise`/`fbm` y periodicidad), 057/058 para el
  escalar de 16 bits, `tests/host/064_spline_easing` (splines y easings polinómicos/
  trigonométricos) y `tests/host/065_scalar_ops` (`min`/`max`/`abs`/`sign`/`move_towards`/
  `deadzone`, easings `_back`/`smooth_damp`, `repeat`/`pingpong` y Bézier).
- Hardware sin `float`: `tests/l0_bare_metal/020_math_scalars` reejecuta el vocabulario con
  `MiniFloat16`, `q12` y `q8` (8.8) más las operaciones entre tipos, en el 68000 y con el
  veredicto por canal lateral (`verify-math.sh`).
- Codegen 68000: `tools/analyze/codegen-report.mjs` compila sondas de las funciones nuevas
  (`c_fx_*`/`c_mf_*`) y **falla** si aparecen libcalls de libgcc, instrucciones 68020 o si
  los helpers de gameplay no quedan inlineados (incluido en la pasada de tests host).
- Demo: `demos/amiga/083_fbm_noise` construye un mapa de altura con `fbm2<MiniFloat16>`
  en hardware (build/run/analyze OK) — ejemplo canónico de `noise.hpp` y verificación por
  demo del escalar.
- Demo: `demos/amiga/084_mf_rotation` compone una rotación 3D con `sin`/`cos` de
  `MiniFloat16` y transforma coordenadas `q0` con `eng/retro/minifloat_fixed`
  (self-test de `sin`/`exp`/`sqrt` en hardware) — verificación por demo de
  `minifloat_math.hpp`.
- Fixed: `eng/core/fixed_math.hpp` (`sin`/`cos`/`sqrt`/`exp2`/`log2`/`exp`/`log`/`pow`
  para `Fixed<s16,E>`, con tablas compartidas) está respaldado por HOST-104 y
  **verificado por demo** en `demos/amiga/110_ylimited_shooter` (self-test en `init`,
  sin `float`).

## 7. Tabla función × escalar (generada)

Esta tabla se genera desde **una sola fuente** (`tools/check/scalar-support.mjs`); no se
edita a mano. El chequeo `node tools/check/scalar-support.mjs` (integrado en la pasada de
tests host) falla si la doc se desincroniza del contrato, y `--write` la regenera.

<!-- SCALAR-TABLE:START -->

| Función | `float`/`double` | `MiniFloat16` | `Fixed` 4.12 | Test que lo respalda |
|---|---|---|---|---|
| clamp / saturate / step | si | si | si | HOST-059 |
| lerp | si | si (pierde incremento si |b-a| < |a|/2048) | si | HOST-059 |
| smoothstep | si | si | si | HOST-059 |
| smootherstep | si | si | no (coef 15 > rango ±8) | math-diag smootherstep_q12_range |
| inv_lerp / remap | si | si | si (div_norm) | HOST-059 |
| dot fusionado (2-4 pares) | si | si | si (acumulador saturado) | HOST-059 |
| cross2 / rotate2 / vscale / vlerp | si | si | si (rotate2 por ángulo con fixed_math) | HOST-059/104 |
| length / normalize / reflect / project | si | si (limites de rango) | si (fixed_math) | HOST-059/104 |
| value_noise / fbm | si | si (coord <= 2048) | no (necesita division) | HOST-060 |
| mul_add / mac (FMA) | — | si (1 redondeo) | si (1 redondeo) | HOST-057/059 |
| hermite / catmull_rom | si | si | si (catmull usa div_norm) | HOST-064 |
| hermite / catmull_rom (Vec<N>) | si | si | si | HOST-064 |
| ease_in/out/in_out_quad/_cubic | si | si | si | HOST-064 |
| ease_in/out/in_out_sine/_expo | si | si (necesita sin/cos/exp2) | si (fixed_math) | HOST-064/104 |
| min / max / abs / sign | si | si | si | HOST-065 |
| move_towards | si | si | si | HOST-065 |
| deadzone | si | si | si | HOST-065 |
| smooth_damp | si | si | si (fixed_math, exp2) | HOST-065/104 |
| repeat / pingpong | si | si | si (div_norm) | HOST-065 |
| ease_in/out/in_out_back | si | si | si | HOST-065 |
| bezier2 / bezier3 | si | si | si | HOST-065 |
| bezier2 / bezier3 (Vec<N>) | si | si | si | HOST-065 |
| wrap_angle / angle_diff | — | si | si (fixed_math) | HOST-057/104 |
| sqrt / sin / cos / exp2 / log2 | — | si | si (fixed_math) | HOST-057/104 |
| exp / log / pow | — | si (minifloat_math) | si (fixed_math) | HOST-104 |
| tan / asin / acos / atan2 | solo atan2 | si | si (fixed_math) | HOST-057/104 |
| sincos (una pasada) | si | si (minifloat_math) | si (fixed_math) | HOST-057/104 |
| angle_of / from_angle / angle_to (apuntado) | si | si | si (fixed_math) | HOST-059/104 |
| transform (MF × fix) | — | ratio MF (|m| <= 8) | coordenada | HOST-058 |
| stats::mean / variance / stddev | si | si | si (sum/mean con acumulador s32; stddev con fixed_math) | HOST-093/104 |
| dsp::Adsr / OnePole / DelayLine / osc_* | si | si | si (osc_sine con fixed_math) | HOST-102/104 |

<!-- SCALAR-TABLE:END -->

## 8. Generalización a 32/64 bits

El vocabulario de §3.b ya es agnóstico del escalar. La deuda pendiente es el **ancho de
palabra**: `fixed_math` y `scalar_div` solo están especializados para `Fixed<s16,E>`, y las
multiplicaciones pasan por `mul16` pensado para `muls.w`. El plan para añadir `Fixed<s32,E>`
(con tablas y series), un tipo general seleccionable en compilación (`s16` en 68000, `s32`
en 68020, `int`/`float` en host) y la política `bool` vs byte está en
[REFACTOR_SCALAR_GENERICO.md](../../guides/roadmap/REFACTOR_SCALAR_GENERICO.md).

**Estado**:
- **F0** (`s64/u64`, `core/scalar.hpp` con `eng::intw`/`real`/`coord`, regla `bool` vs byte en
  `CODING_STYLE`) y **F1** (`div_norm` y `sqrt` de `Fixed<s32,E>`, en host/32 bits; vetados en
  m68k por libcalls) entregados.
- **F2** (`fixed_math.hpp` genérico por `Repr`: tablas dimensionadas y series) entregado: la
  trigonometría/exponencial de `Fixed<s32,E>` funciona en host/32 bits con la misma maquinaria
  que el `s16` (que conserva su semántica).
- **F3** (nombres genéricos `mul_wide`/`mulu_wide`/`div_wide` sobre `arith<R>`) entregado y
  migrados `mesh3d`, `collision` y `retro/lib2d`; el camino 68000 es idéntico.
- **F4** (barrido `bool` vs byte) auditado: el engine ya usaba `bool` para booleanos y
  `u8`/`s8` para cuentas, índices y layout/ABI; el único ajuste es el flag de depuración
  `m_dbg_ink_visible` de `field/xlimited.hpp`, que pasa a `bool`.
- **F5** (tipos generales en simulación) entregado: HOST-136 compila el mismo algoritmo contra
  `eng::real`/`coord`/`intw` y `tools/run/run-scalar-modes.sh` lo ejecuta en RETRO16, RETRO32 y
  NATIVE para comparar la precisión (muestra que el ancho da rango, no resolución fraccionaria).
- **F6** (codegen 68020) entregado: la sonda compila también a `-mcpu=68020` sin libcalls.

Todo respaldado por HOST-135 (matriz de escalares: `double`/`float`/`MiniFloat16`/
`Fixed<s16,12>`/`Fixed<s32,12>`/`Fixed<s32,24>`, incluida la trig/exp/log de `Fixed<s32>`) y
HOST-136 (`eng::real`/`coord`/`intw` en los tres modos).
Pendientes menores: extender la tabla §7 con la columna de 32 bits, adoptar `eng::real`/`coord`
en demos concretas y completar la migración de todos los `mul16` a `mul_wide`.
