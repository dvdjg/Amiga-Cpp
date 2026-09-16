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

## 7. Funciones matemáticas

`engine/include/eng/core/minifloat_math.hpp` añade `sqrt`, `exp`, `exp2`/`pow2`, `log`,
`log2`/`log10`, `pow`, `hypot`, trigonometría (`sin`, `cos`, `tan` y `sincos`) e
inversas (`atan`, `atan2`, `asin`, `acos`) en `eng::math`, implementadas **solo con
aritmética de 16 bits**: nada de `float` ni de
`libgcc` (en el `.o` de m68k se ve `muls.w` y las tablas, sin `divs`/`divu` ni
`__mulsf3`/`__divsf3`). Son la misma idea que el resto del tipo: reducción de rango +
serie de Taylor minimax evaluada en Horner, con el compromiso precisión/coste
documentado. Las rutinas calientes (`exp`, `sqrt`, `log`, `pow`, `sin`/`cos`/`tan`,
`asin`/`acos` y los helpers) llevan `[[gnu::always_inline]]`: en 68000 la llamada
(`jsr`/`rts` + salvar registros) puede costar más que el cálculo; con el inline forzado
ninguna de ellas emite símbolo propio.

| Función | Algoritmo | Rango fiable | Error observado (vs `float`) |
|---|---|---|---|
| `sqrt` | exponente par/impar + Newton | `[2^-14, 65504]` | ~1.0e-3 rel |
| `exp` | `z = x·log2e` en Q4.11, `2^f` en Q1.14, `2^n` por exponente | `[-11, 11]` (satura fuera) | ~6e-4 rel |
| `exp2`/`pow2` | núcleo `2^z` directo (sin el `·log2e`) | `[-15, 16]` (satura fuera) | ~5e-4 rel (exacto en enteros: `2^10 = 1024`) |
| `log` | `x = m·2^k` + serie de `atanh` | `(0, 65504]` | ~2.5e-3 rel (~1.4e-2 abs en el extremo) |
| `log2`/`log10` | exponente + `log(m)·log2(e)` / `·log10(e)` | `(0, 65504]` | ~8e-3 / ~4e-3 abs (exactos en potencias de 2) |
| `pow` | entero `\|e\| <= 64`: cuadrado y multiplicación (exacto); si no `exp(e·log(base))` | `base > 0`; entero admite base negativa | exacto (entero); ~1e-2 rel (crece con `\|e·log(base)\|`) |
| `sin`/`cos` | Cody-Waite + Taylor | `\|x\| <= 2π` (más allá pierde bits) | ~2e-3 abs |
| `tan` | `sin/cos` | como `sin`/`cos`, evitando los polos | ~1e-2 rel lejos del polo |
| `sincos` | una sola reducción para los dos | como `sin`/`cos` | igual que `sin`/`cos` |
| `atan` | minimax en `[0,1]` + `atan(1/x)` | todo `x` (±∞ → ±π/2) | ~1.2e-3 abs |
| `atan2` | `atan(y/x)` + cuadrante | todo `(y,x) != (0,0)` | ~2.1e-3 abs |
| `asin`/`acos` | `atan2` + `sqrt` | `[-1, 1]` | ~1.8e-3 abs |
| `hypot` | escalado `r = menor/mayor` + `sqrt(1+r²)` | todo `x,y` | ~2e-3 rel |

El punto a retener es que `exp` **no** usa el clásico "partir por la mitad y elevar al
cuadrado": ese método amplifica el error relativo por `2^s` y dejaba el extremo en
~4% de error. Resolver la reducción en punto fijo (Q4.11/Q1.14 con `muls.w`) mantiene
los ~10 bits en todo el rango. `pow` con exponente entero evita `exp`/`log` por
completo (y por eso `pow(2,10) = 1024` es exacto).

Dominios inválidos (contrato sencillo, sin NaN): `sqrt(x < 0)`, `log(x <= 0)`,
`asin/acos` fuera de `[-1,1]` y `pow` con base negativa y exponente no entero producen
∞ (±∞ en `log(0)`), y las saturaciones a 0/∞ son explícitas en el rango del tipo.

## 8. Interoperabilidad con coma fija (`fix`/`fix88`)

`engine/include/eng/retro/minifloat_fixed.hpp` (namespace `eng::retro`) combina
`MiniFloat16` con el fixed retro. Vive en `retro/` porque el vocabulario `fix` (4.12) y
`fix88` (8.8) es retro; el núcleo (`core/`) no conoce esas convenciones.

El caso de uso es transformar **coordenadas fijas** con una matriz de ratios MF:

```
   Mat<N, MiniFloat16> (RATIO)  ×  Vec<N, Coord12> (LONGITUD)  ->  Vec<N, Coord12>
```

con `Coord12 = Fixed<s16,12>` y `Coord88 = Fixed<s16,8>`: el tag de escala es el
exponente de `Fixed`, así que 4.12 y 8.8 no se pueden mezclar sin un tipo nuevo. La
matriz MF se convierte **una vez** a 4.12 y cada fila acumula los productos
`ratio·coordenada` en 32 bits con `muls.w`, normalizando con **un único** desplazamiento
`>> 12`: la fracción de la coordenada (4.12, 8.8 o entero `q0`, el `Vec2` de `lib2d`) es
independiente de la de la razón, la coordenada no pasa por la mantisa de 10 bits del MF y
no se redondea producto a producto (producto escalar fusionado, como el `dot` de
`linalg`). Como la razón se guarda en 4.12, las entradas de la matriz deben caber en
`[-8, 8]`. Verificado en el `.o` de m68k: `muls.w`, sin `divs`/`divu` ni libcalls de coma
flotante.

| Función | Qué hace |
|---|---|
| `fixed_to_mf` / `mf_to_fixed<Frac>` | conversión tipada `Fixed<s16,Frac>` ↔ MF |
| `fix_to_mf` / `mf_to_fix`, `fix88_to_mf` / `mf_to_fix88` | igual, con `s16` crudo |
| `mul_fixed` / `mul_fix` / `mul_fix88` | `ratio · valor` -> mismo fixed |
| `transform(m, p)` / `transform(m, p, t)` | `M·p` y `M·p + t`, tipado |
| `transform_fix` / `transform_fix88` | idem, con `s16` crudo |
| `transform_point(m4, p3)` | homogéneo 4x4: `M·(p,1)` -> `Vec<4,Coord>` (la `w` permite proyectar) |
| `project(m4, p3)` | proyección: `M·(p,1)` y división por `w`, devuelve **MF** (cabe pantalla) |

Con coordenadas `q0` (pixeles enteros, `Vec2` de `lib2d`) el mismo `transform` sirve de
atajo 2D: `transform(mf_mat_2x2, v2)`.

Todas las conversiones son explícitas y **saturan** al salir del rango del destino (nada
de envolver). Errores medidos (HOST-058 vs `float`): conversión ~5e-4 rel; `transform`
3x3 ~1e-4 abs (muy por debajo de la resolución 1/4096 del fixed), identidad exacta y
saturación correcta fuera de rango.

## 9. Estado y verificación

`MiniFloat16` está **verificada por demo**: `demos/amiga/083_fbm_noise` usa
`fbm2<MiniFloat16>` (`eng/core/noise.hpp`) para construir un mapa de altura en el
emulador (build/run/analyze OK), ejercitando la aritmética, `from_int` y las
comparaciones en hardware. Su corrección la amplían los tests host:

- [`tests/host/056_minifloat16`](../../../tests/host/056_minifloat16/README.md):
  formato, conversiones, aritmética y operaciones de matrices 2x2/3x3/4x4 (incluida la
  inversa) contra `float`.
- [`tests/host/057_minifloat16_math`](../../../tests/host/057_minifloat16_math/README.md):
  `sqrt`/`exp`/`log`/`log2`/`log10`/`pow`/`hypot`/`sin`/`cos`/`tan`/`sincos` e inversas
  contra `std::`, más identidades de composición (`sin²+cos²=1`, `exp(log x)=x`,
  `sqrt(x)²=x`, `pow(x,2)=x·x`, `sin(asin x)=x`, `atan(tan x)=x`, `hypot(x,0)=|x|`).
- [`tests/host/058_minifloat_fixed`](../../../tests/host/058_minifloat_fixed/README.md):
  puente con `fix`/`fix88` (conversiones, producto mixto y `transform` de coordenadas
  fijas con matriz MF) contra `float`.
- [`tests/host/060_noise`](../../../tests/host/060_noise/README.md): `value_noise`/`fbm`
  con `MiniFloat16` contra `double` (el mismo camino que usa la demo 083).

Las funciones de `minifloat_math.hpp` (trascendentes) siguen cubiertas solo por el test
host 057; al usarlas en una demo, actualizar esta sección.
