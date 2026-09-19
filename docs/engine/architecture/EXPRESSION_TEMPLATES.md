# Expression templates lite (`eng/core/expr.hpp`)

`eng::math::et` es una capa **pequeña** de *expression templates* para el engine: construye el
árbol de una expresión aritmética en tiempo de compilación y lo evalúa **una sola vez**, al
convertirlo al tipo final. Evita materializar un temporal por operador (`a + b*c - d` crearía
tres valores intermedios) y deja al compilador ver la expresión entera para mantener
operandos en registros. La pieza objetivo en un 68000 no es escribir menos, sino **menos
construcciones y destrucciones** de escalares con formato propio (`Fixed`, `MiniFloat16`) y
mejor presión de registros.

No es una librería de álgebra diferida al estilo Eigen/Blitz: es una capa **lite** para las
expresiones que de verdad se encadenan en rutinas calientes, y convive con el álgebra escrita
a mano de `linalg.hpp` (`dot`, `Mat*Vec`, `transform`) sin sustituirla.

## 1. Modelo

```text
   val(a) + val(b) * val(c) - val(d)
        │
        ▼
        Bin<Sub, Bin<Add, Val<a>, Bin<Mul, Val<b>, Val<c>>>, Val<d>>   (tipo, en compilación)
        │
        eval<S>()          ──► un único valor S       (resultado completo)
        eval_into(dst,e)   ──► dst componente a componente (un solo bucle)
```

Un **nodo** deriva de `Expr<Derived>` (CRTP) y expone dos operaciones:

- `value()`: el valor **completo** del subárbol.
- `at(i)`: el **componente `i`** del subárbol. Una hoja escalar lo ignora (se difunde); una
  hoja contenedor devuelve su componente; un nodo binario compone.

| Pieza | Papel |
|---|---|
| `Expr<D>` | Base CRTP: `self()`, `eval<S>()`, `assign_to(dst)`. |
| `Val<T>` | Hoja: guarda un escalar o un contenedor **por valor**. |
| `Bin<Op,L,R>` / `Negate<L>` | Nodos binario y unario. |
| `Add`/`Sub`/`Mul`/`Div` | Etiquetas de operación (`apply(a, b)`), genéricas sobre el tipo. |
| `val(x)` | Envuelve un valor como hoja (si ya es expresión, la devuelve). |
| `evaluate<S>(e)` | Atajo de `e.eval<S>()`. |
| `eval_into(dst, e)` | Evaluación **fusionada** por componente. |
| `converter<S>` | Conversión final al tipo destino (especializada para `Fixed`). |
| `et_get`/`et_count`/`et_set` | Puntos de personalización de un contenedor. |

Los nodos guardan sus operandos por valor: no hay punteros, heap, virtuales ni STL, y todo es
`constexpr`. El árbol existe solo como tipo; en código generado no queda rastro de él.

## 2. Uso

### Escalares

```cpp
using namespace eng::math;
using namespace eng::math::et;

Fixed<s16, 12> a{...}, b{...}, c{...}, d{...};
const Fixed<s16, 12> r = evaluate<Fixed<s16, 12>>(val(a) + val(b) * val(c) - val(d));
```

El destino fija el tipo: `eval<S>` convierte el resultado con `converter<S>`. Para `float`,
`double` y `MiniFloat16` la conversión es el `static_cast` del lenguaje; para `Fixed` es
**reescalar** al exponente destino, **moldear** a su representación y **re-etiquetar** su
política (un `static_cast` no cambia de exponente).

### Cálculo matricial (evaluación fusionada)

```cpp
Vec<3, float> p{...}, v{...}, out{};
et::eval_into(out, et::val(p) + et::val(v) * 2.0f - et::val(w));   // un solo bucle

Mat<2, Fixed<s16, 12>> A{...}, B{...}, D{};
et::eval_into(D, et::val(A) * val(A));    // producto por componente, reescalado al destino
```

`eval_into` recorre **una vez** el destino y pide a la expresión el componente `i`. Así una
cadena `a + b*k - c` sobre `Vec`/`Mat` no construye ningún vector ni matriz temporal: se
resuelve en el mismo `for` que escribe el resultado.

### Difusión (`broadcast`)

Una hoja escalar dentro de una expresión de contenedor se difunde: `eval_into(vec, val(3.5f))`
llena el vector con `3.5f`.

## 3. Añadir tipos y contenedores

- **Escalar nuevo**: si tiene `+`, `-`, `*` (y `/`), funciona sin cambios. Si su conversión al
  tipo destino no es un `static_cast` (cambio de exponente, reetiquetado de política…), se
  especializa parcialmente `converter<MiEscalar>` (es el caso de `Fixed`).
- **Contenedor nuevo**: definir `et_get(c, i)`, `et_count(c)` y `et_set(dst, i, x)`. Hay
  implementaciones para `Vec<N,S>` y `Mat<N,S>` (el `Mat` indexa por filas: `i = fila*N +
  columna`). Nada más: el resto del árbol es genérico.

## 4. Acumulador ancho para `Fixed`

Un escalar con formato propio complica la suma dentro del árbol: el producto `a * b` **suma**
exponentes (`Fixed<s16,E>` × `Fixed<s16,E>` → `Fixed<s32,2E>`), así que `a + b*c` (término en
`E` más producto en `2E`) tendría sumandos de exponente distinto y no podría sumarse.

La solución está en `et_add`/`et_sub`, el punto de personalización de `+`/`-` **dentro de la
expresión**. Para `Fixed`, promueven los dos operandos al **tipo común ancho** —representación
común (`common_repr`, que en un producto ya es la ancha) y **mayor** exponente— y suman ahí:

```text
   a (E)  +  b*c (2E)          et_add promueve a a 2E (desplazamiento exacto)
        │
        ▼
   Fixed<s32, 2E>  =  a·2^E + b·c        acumulación EXACTA en el exponente del producto
        │
   eval<Fixed<s16,E>>            converter reescala 2E -> E y moldea s32 -> s16 (una vez)
        ▼
   Fixed<s16, E>
```

Subir exponente es un **desplazamiento a la izquierda exacto** (no se pierde precisión); la
representación se ensancha al tipo común (p. ej. `s16` → `s32`). La normalización (y el
redondeo o la saturación) ocurre **una sola vez**, al convertir el resultado al tipo destino.
Es el modelo de acumulación de `dot`/`transform`, pero expresable con operadores.

El orden de conversión importa y está cuidado: al **subir** exponente se ensancha primero la
representación y luego se desplaza; al **bajar**, se desplaza con redondeo y luego se moldea.
Hacerlo al revés desbordaría la representación estrecha antes de ensancharla.

Un límite que sigue en pie: encadenar **productos de productos** (`(a*b)*c`) sumaría
exponentes otra vez y ensancharía la representación más allá de `s32`, con el coste de 64 bits
(prohibido en m68k). El acumulador ancho está pensado para sumas de productos y términos
—el caso `dot`—, no para álgebra anidada arbitraria.

## 5. Qué ahorra, medido

`node tools/analyze/expr-asm-compare.mjs` compila la misma expresión con operadores sueltos y
con `et`, y compara el `.s` (68000, `-O2`, `-fomit-frame-pointer`):

| Función | Instr. | Escr. pila | Llamadas | Lectura |
|---|---:|---:|---:|---|
| `mf16_ops` (`a + b*c - d`, escalar) | 639 | 157 | 0 | referencia |
| `mf16_et` (misma, con árbol) | 604 | **104** | 0 | **−5 % instrucciones, −34 % pila** |
| `vec3_mf16_ops` (`a + b*k - d`) | 565 | 47 | 0 | referencia |
| `vec3_mf16_et` (misma, fusionada) | 634 | 71 | 0 | **peor**: +12 % instr., más pila |
| `vec3_float_ops` (soft-float) | 56 | 27 | 9 | desenrollado |
| `vec3_float_et` | 44 | 22 | 3 | bucle no desenrollado |

Conclusiones medidas, que corrigen la intuición inicial:

- **El beneficio está en el escalar**, no en el contenedor pequeño. En `MiniFloat16` el árbol
  evita temporales que no caben en registro: menos instrucciones y **un tercio menos de
  escrituras a pila**. Es un resultado real y medible.
- **En un `Vec`/`Mat` diminuto el árbol no ayuda (y puede empeorar)**: g++ ya elimina los
  temporales de la cadena de operadores por sí solo, y la recursión por componente del árbol
  añade trabajo. La fusión estilo Eigen paga en contenedores **grandes** o con copias caras,
  que no es el caso de `Vec<3>`/`Mat<2>` (agregados minúsculos).
- **Con soft-float** (`float` sin FPU) el árbol desenrollado puede quedar como bucle; la
  ventaja/desventaja es marginal frente al coste de las llamadas.

Por eso este sistema **no se adopta a ciegas**: se mide por caso con la herramienta de arriba.
Su valor inmediato es el escalar (`Fixed`/`MiniFloat16`) en cadenas de 3+ operaciones; en
contenedores se evalúa antes de darlo por ganancia.

## 6. Límites (por diseño)

- **Mismo escalar**: el árbol no mezcla tipos de escalar (`float` con `Fixed`, p. ej.); eso
  sigue siendo un error de dominio, igual que en `linalg.hpp`. Un literal crudo (`2`) tampoco
  promociona a `Fixed`: hay que darle su tipo (`Fixed<...>::from_int(2)` o una Q).
- **`Fixed`, exponente y ancho**: el acumulador ancho (§4) cubre `a + b*c`, pero no productos
  anidados (`(a*b)*c`), que pedirían 64 bits.
- **Explícito, no implícito**: se construye con `val(...)` y se evalúa con `.eval<S>()` o
  `evaluate<S>(...)`. No hay conversión implícita ni `operator=` mágico; la evaluación ocurre
  donde se pide.
- **No es una panacea**: el mayor enemigo en un A500 sigue siendo el **número de operaciones**,
  no los temporales. Expression templates reducen copias, pero no sustituyen a usar fixed-point
  cuando el rango lo permite, tablas de recíprocos/senos, evitar divisiones o reducir el álgebra
  a mano. **Mídelo** con la herramienta de §5 antes de darlo por ganancia.

## 7. Verificación

- **HOST-207** (`tests/host/207_math_expr/`): plegado `constexpr`, `MiniFloat16`, `Fixed`
  (suma, producto reescalado, `a + b*c` con acumulador ancho, suma de productos, destino con
  otro exponente) y fusión de `Vec`/`Mat` (`eval_into`, `assign_to`, difusión, `Vec<Fixed>`
  con acumulador ancho).
- **Sonda de codegen** (`c_math_expr_ops` en `tools/analyze/codegen-report.mjs`): el camino
  caliente compila para 68000 **sin libcalls** de 32/64 bits ni instrucciones 68020.
- **Comparativa de `.s`** (`tools/analyze/expr-asm-compare.mjs`, §5): cuantifica lo que ahorra
  la fusión y deja constancia de que en contenedores pequeños no compensa.

Modelo general del álgebra y del escalar: [MATH_LIBRARY.md](MATH_LIBRARY.md) y
[SCALAR_LIBRARY.md](SCALAR_LIBRARY.md).
