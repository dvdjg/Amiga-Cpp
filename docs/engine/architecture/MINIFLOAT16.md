# `MiniFloat16`: coma flotante de 16 bits para 68000

`eng::math::MiniFloat16` (`engine/include/eng/core/minifloat.hpp`) es un escalar de
**16 bits** en coma flotante con **rango dinámico amplio** (no es fixed-point) y coste
pensado para un 68000 sin FPU. Se usa donde interesa representar magnitudes de escalas
muy distintas —geometría 3D, transformaciones, proyección, culling— sin pagar el
soft-float de `libgcc`: en un 68000 `float` se emula con `__addsf3`/`__mulsf3`/
`__divsf3`, libcalls de cientos de ciclos y con pila de por medio; `MiniFloat16` cabe
en un registro de datos y usa aritmética de 16 bits nativa y tablas.

Es, además, un escalar de primera clase para la librería genérica de álgebra lineal:
`scalar_traits<MiniFloat16>` está especializado en `eng/core/linalg.hpp`, así que
`Vec<N>`, `Mat<N>` y `Affine<N>` funcionan con **el mismo código** que `float` o que el
fixed-point `Fixed<...>`.

## 1. Formato

```
   bit 15  14 13 12 11 10   9 .......... 0
   ┌─────┬──────────────┬─────────────────┐
   │  S  │   exponente  │     mantisa     │
   └─────┴──────────────┴─────────────────┘
     1          5               10           = 16 bits (2 bytes)

   valor = (-1)^S · (1 + mantisa/1024) · 2^(exponente − 15)
```

- **Sesgo 15** (como el `half` IEEE), con **1 implícito** en la mantisa.
- El campo exponente **0 es cero** (no hay denormales) y el campo **31 es ∞**
  (no hay NaN).
- El signo está en el bit alto en **signo-magnitud**: comparar los `raw` como enteros
  con signo daría un orden equivocado para negativos; los operadores de comparación
  ya aplican la clave de orden correcta.
- Se serializa tal cual en un `u16` (`raw`), por lo que empaquetar vectores de
  `MiniFloat16` no cuesta nada extra (2 bytes por componente).

## 2. Rango y precisión

| Magnitud | Valor |
|---|---|
| Cero | `raw = 0x0000` (y `-0` se canoniza a `+0`) |
| Mínimo normal | `2^-14 ≈ 6.10·10^-5` (`raw = 0x0400`) |
| Máximo finito | `65504` (`raw = 0x7BFF`) |
| ULP junto a 1.0 | `2^-10 ≈ 9.77·10^-4` |
| Error relativo de una operación | `≈ 2^-11 ≈ 4.9·10^-4` (≈ 3 dígitos decimales) |
| Bits de mantisa | 10 (+1 implícito) |

El rango dinámico útil es, por tanto, `|x| ∈ [2^-14, 65504]` más el cero. Alrededor de
`2^-14` hay redondeo de borde: un valor en `[2^-15, 2^-14)` se redondea a `2^-14` y por
debajo de `2^-15` a cero (no hay denormales). Por arriba, superar el máximo satura a ∞.

## 3. Qué valores son exactos

Con 10 bits de mantisa, en cada octava `[2^k, 2^(k+1))` son exactos los múltiplos de
`2^(k-10)`. En particular:

- **Enteros exactos de 0 a 2048**; a partir de ahí, múltiplos de 2 (hasta 4096), de 4
  (hasta 8192), de 8, etc., hasta múltiplos de 32 en `[32768, 65504]`.
- Fracciones con denominador potencia de dos dentro de 10 bits (0.5, 0.25, 0.125…),
  siempre que la mantisa no se llene: `1.5`, `1.75`, `0.375` son exactos.

No son exactos los valores decimales "redondos" (`0.1`, `0.2`, `1.1`…) ni las
fracciones de denominador grande: caen en el redondeo de `≈5·10^-4`.

## 4. Coste en 68000

Toda la aritmética es inline y **no llama a la biblioteca** (verificado en el `.o` de
m68k: no hay `__mulsf3`, `__divsf3`, `__mulsi3` ni `__divsi3`):

| Operación | Mecanismo | Coste orientativo |
|---|---|---|
| `+` / `-` | alineado por desplazamientos + tabla `mf16_clz8` de 256 B | decenas de ciclos |
| `*` | una `mulu.w` (16×16→32) + normalización | ~40-70 ciclos (dominado por `mulu.w`) |
| `/` | tabla `mf16_rcp` de 1024 entradas (2 KB) + `mulu.w` | ~50-80 ciclos; evita el `divu.w` (~140) |
| comparar | clave de orden (unas ALU) | ~10 ciclos |

Las conversiones `float → MiniFloat16` y `MiniFloat16 → float` son solo extracción/
construcción de bits (sin libcall), y se reservan para **constantes y depuración**; la
aritmética `float` en sí es lo caro y no debe aparecer en el camino caliente.

El precio en memoria son las dos tablas: **256 B** (renormalización) + **2 KB**
(recíprocos) de datos constantes, compartidas por todas las instancias.

## 5. Reglas de uso (rangos seguros)

1. **Trabajar dentro de `[2^-14, 65504]`.** Fuera de ahí el resultado satura (a 0 o a
   ∞) y deja de ser utilizable. Si el problema necesita un rango mayor, `MiniFloat16`
   no es la herramienta.
2. **Evitar acumulaciones largas.** Al sumar un valor muy pequeño a uno grande, si la
   diferencia de exponentes llega a ~11 el pequeño desaparece (early-out del `+`). Una
   suma de muchos términos debe agruparse por magnitud o hacerse en un acumulador más
   ancho.
3. **Cuidado con la cancelación.** Al restar dos valores casi iguales solo quedan los
   ~10 bits de la mantisa original: el resultado es correcto en términos absolutos,
   pero su error *relativo* es grande. No usar `a - b` para estimar una diferencia
   pequeña (p. ej. `(x+1) - x`).
4. **No hay NaN.** La división por cero y el overflow dan ∞; las operaciones con ∞ se
   resuelven de forma pragmática (`∞+x = ∞`, `x/∞ = 0`, `∞·0 = 0`) y no se propaga un
   patrón de error. Comprobar con `is_inf()`/`is_zero()` cuando el dominio lo requiera.
5. **Usarla como tipo de valor, no de control.** Es adecuada para posiciones,
   direcciones, factores de escala y resultados intermedios de una transformación;
   no para índices, contadores ni comparaciones de igualdad exacta sobre decimales.

## 6. Uso con la librería genérica

```cpp
#include <eng/core/linalg.hpp>
using eng::math::MiniFloat16;
using M3 = eng::math::Mat<3, MiniFloat16>;

M3 rot = M3::identity();             // scalar_traits aporta el 1 y el 0 reales
eng::math::Vec<3, MiniFloat16> p = {MiniFloat16(1.0f), MiniFloat16(2.0f), MiniFloat16(3.0f)};
eng::math::Vec<3, MiniFloat16> q = rot * p;       // Mat*Vec con normalización propia
M3 inv = ...;                                      // inversa analítica, etc.
```

La conversión desde `float` es **explícita** (`MiniFloat16(1.0f)`), para que un
`float` no se cuele en el camino caliente por accidente. En 68000, `sizeof(MiniFloat16)
== 2` y las matrices de `MiniFloat16` ocupan un cuarto que las de `float`.

## 7. Estado y verificación

`MiniFloat16` está **NO VERIFICADA por demo** (todavía no tiene consumidor en
`demos/`). Su corrección la respalda el test host
[`tests/host/056_minifloat16`](../../../tests/host/056_minifloat16/README.md), que
compara la aritmética y las operaciones de matrices 2x2/3x3/4x4 (incluida la inversa)
contra `float` sobre entradas idénticas. Al integrarla en una demo, actualizar esta
sección y la marca del comentario de cabecera.

Las funciones matemáticas clásicas (`sqrt`, `log`, `exp`, potencias y trigonometría)
**no forman parte todavía de la API**; hoy la única vía es operar con los cuatro
operadores básicos.
