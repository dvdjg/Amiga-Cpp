# Roadmap: escalar genérico y portabilidad a 32/64 bits

Plan para que la librería de plantillas del engine sea **agnóstica del ancho de palabra**:
en 68000 se conservan las optimizaciones actuales (`s16`, `muls.w`, tablas), y al compilar
para 68020 nativo o para host/x64 se usan tipos nativos (`int`, `float`, `double`) y
`Fixed` de 32 bits, sin duplicar algoritmos. Estado vigente de la matemática en
[SCALAR_LIBRARY.md](../../engine/architecture/SCALAR_LIBRARY.md) y
[MATH_LIBRARY.md](../../engine/architecture/MATH_LIBRARY.md).

## 1. Objetivo

- **Un algoritmo, muchos anchos**: lo mismo que hoy se hace con varios escalares
  (`double`/`MiniFloat16`/`q12`) se generaliza a `Fixed` de 32 bits y a enteros nativos.
- **Selección en compilación**: un «tipo general, equivalente al `int` de C» que es
  `s16` en 68000, `s32` en 68020 y `int`/`float` en host. Se puede **simular la versión de
  16 bits en host** (lo que ya pasa) o sacar la versión full x64.
- **`Fixed` de 32 bits** (`Fixed<s32,E>`) con su división, `sqrt`, tablas y series.
- **Sin regresión** en el camino 68000: el gate de codegen sigue exigiendo `muls.w`/`divs.w`
  y cero libcalls.

## 2. Estado actual (lo que ya es genérico y lo que no)

Lo que **ya es genérico** (una sola fuente, parametrizada por `S`):

```
   eng/core/                         puntos de extensión (contrato estable)
   linalg.hpp  interp.hpp            scalar_traits<S>   zero/one/from_int/norm_from
   geometry.hpp scalar_ops.hpp        numeric_traits<S>  limites y flags
   spline.hpp  noise.hpp  stats.hpp   scalar_sqrt/sin/cos/.../<S>::op
   dsp.hpp      random.hpp            scalar_const<S>
```

Están respaldados por `scalar_traits`/`numeric_traits` y ya se prueban con `double`,
`MiniFloat16`, `q12`/`q8` y un escalar de usuario (HOST-052/059/064/065/093/102/104). El
núcleo `Fixed<Repr,Exp,Policy>` también es genérico: `wide<s32>` y `limits<s32>` existen y
suma/producto/rescale/cast funcionan con `Fixed<s32,E>` (verificado).

Lo que **está acoplado**:

| Zona | Acoplamiento | ¿Debe generalizarse? |
|---|---|---|
| `fixed_math.hpp` | tablas y `scalar_sin/cos/tan/asin/acos/atan2/exp2/log2/sqrt` **solo** para `Fixed<s16,E>` | **sí** (por `Repr` y por `E`) |
| `linalg.hpp` | `scalar_div<Fixed<s16,E>>` es la única especialización; `div_norm(Fixed<s32,E>)` **no compila** | **sí** |
| `arith<>` (`mul_wide`/`div_wide`/`mulu16`) | producto/cociente ensanchado por CPU (`muls.w`/`divs.w` en 16 bits) | **sí** (ya genérico) |
| `mesh3d.hpp` (`Coord = Fixed<s16,0>`, `mul_wide`/`mul32x16`), `collision.hpp`/`grid.hpp`/`broadphase.hpp`/`color.hpp` (`Point2s`, AABB, RGB444) | dominio de **pantalla/tile/píxel** a 16 bits | **no** (son tipos de dominio hardware) |
| `object3d.hpp`/`lib3d.hpp` | layout empaquetado `s16` del `objdat` de lib3d | **no** (ABI/binario del mesh) |
| `eng/retro/*`, `eng/cpu/m68k/*` | vocabulario fijo retro y CPU | **no** (son la capa 16-bit) |

Conclusión: el refactor **no** consiste en volver genérico todo, sino en separar
**tipos de dominio** (bytes/píxeles/registros/ABI, que siguen a 16 bits) de
**escalares de simulación** (que pasan a ser seleccionables).

## 3. Diseño

### 3.1 Tipo general y selección

Nuevo `engine/include/eng/core/scalar.hpp` (o ampliación de `types.hpp`):

```
   eng::intw   → entero de palabra natural (el «int» de C):  s16 (68000) | s32 (68020) | int (host)
   eng::real   → fraccionario por defecto:                   Fixed<s16,12> | Fixed<s32,12> | float
   eng::coord  → coordenada de simulación:                   Fixed<s16,0>  | Fixed<s32,0>  | int
```

Se elige con macros de compilación (defecto por target, forzable):

```
   -DENG_SCALAR_RETRO16   (implícito en __m68k__): s16 / Fixed<s16,E> / MiniFloat16
   -DENG_SCALAR_RETRO32   (68020 nativo):          s32 / Fixed<s32,E>
   -DENG_SCALAR_NATIVE    (host x86-64):           int / float / double
```

`scalar.hpp` es solo **tipos y rasgos**: no cambia algoritmos. Estos siguen recibiendo `S`
por plantilla; los alias son la instanciación por defecto que usan demos/juegos.

### 3.2 Pregunta `s8` vs `bool`

Regla única:

- **`bool`** para booleanos **semánticos** (estado, `valid`, `visible`, `enabled`). Mide
  1 byte igual en m68k y en host.
- **`u8`/`s8`** para **bytes empaquetados, layout/ABI de hardware** (el `objdat` de lib3d,
  campos de registros), **IDs/índices pequeños**, **máscaras de bits** y datos de
  asset/DMA. Ahí el tamaño y el formato importan y `bool` no debe usarse.
- Nunca `bool` dentro de un struct que se envía a DMA, se lee por `incbin` o casa con el
  layout de una herramienta externa.

Auditoría inicial: `object3d.hpp` `Edge::flags`/`Face::flags` (s8) y `route_camera.hpp`
`s8 h_dir/v_dir` se quedan como bytes/dirección (no son booleanos); `chunk_cache` `Slot.valid`
ya es `bool`; conviene barrer el resto y quedarse con `bool` donde sea estado.

### 3.3 `Fixed` de 32 bits

Falta, por `Repr`:

- `scalar_div<Fixed<s32,E,P>>` (`div_norm`): el intermedio `a.v << E` y el cociente piden
  **más de 32 bits**. Implementarlo con `wide<R>` (`long long`) y **excluirlo del 68000**
  (allí `long long` son libcalls): se habilita solo si `sizeof(R) > 2` o si el target no
  es 68000. En host/68020 es aritmética nativa.
- `scalar_sqrt<Fixed<s32,E>>`: `isqrt` de 64 bits (o `sqrt` nativo en host) con las mismas
  guardas por `E`/CPU.
- `scalar_sin/cos/sincos/tan/asin/acos/atan2/exp2/log2/exp/log/pow<Fixed<R,E>>`: **tablas
  dimensionadas por `Repr`** (muestras del propio `R`; acumulador `wide<R>`) y **series
  Taylor/Maclaurin** genéricas para poblar tablas en `constexpr` y como respaldo sin tabla.
- `wide<s32>` es `long long`: válido en host/68020, vetado en 68000 (guard de compilación).

### 3.4 Abstracción de CPU (palabra y multiplicación)

Abstracción por CPU (implementada en `arith.hpp`, sustituye al antiguo `word.hpp`):

```
   mul_wide<S>(a,b) -> wide<S>     … nativa si el target la tiene
   div_wide<S>(a,b) -> S / wide<S>
   arith<S>                        … 16 bits: muls.w/divs.w (m68k); 32/64: nativa
```

`mesh3d`/`collision`/`retro/lib2d` dejan de llamar a `mul16` directo y usan `mul_wide<S>`;
en 68000 la implementación es la de hoy (gate bit a bit), en 68020/host es la nativa.

## 4. Fases

| Fase | Entrega | Verificación |
|---|---|---|
| **F0** | Vocabulario: `i64/u64`, regla `bool` vs byte documentada, `scalar.hpp` con `ENG_SCALAR_*` y alias `intw`/`real`/`coord` (sin tocar algoritmos) | static_asserts host |
| **F1** | `Fixed<s32,E>`: `scalar_div`, `scalar_sqrt`, policies, guardas por CPU | HOST nuevo (`fixed32`) |
| **F2** | `fixed_math.hpp` genérico por `Repr` (tablas dimensionadas + series); `scalar_*<Fixed<R,E>>` | HOST `fixed32` + codegen 68020 |
| **F3** | `mul_wide`/`div_wide`/`arith<S>` por CPU; migrar `mesh3d`/`collision`/`retro` a lo genérico | gate codegen 68000 **sin cambios** |
| **F4** | Higiene `bool`: sustituir `s8`/`u8` semánticos; dejar bytes/ABI | compilación + revisión |
| **F5** | Wiring de selección: demos/juegos usan `eng::real`/`coord`; los hardcodes de simulación (`q12`, `Coord`) pasan a alias | HOST + demo |
| **F6** | Matriz de tests y comparativas (abajo) | HOST + codegen 68000/68020 |
| **F7** | Docs: `SCALAR_LIBRARY`, `MATH_LIBRARY`, `STRUCTURE`, índices | `links`/`encoding` |

Dependencias: F0→F1→F2 (el `fixed_math` de 32 bits necesita la división/raíz); F3 es
independiente; F4/F5 van tras F0; F6 cierra.

**Estado**: **F0–F6 entregados** (detalle y evidencia en
[SCALAR_LIBRARY.md](../../engine/architecture/SCALAR_LIBRARY.md) §8, HOST-135 y HOST-136).
F4 fue un barrido de auditoría: el código ya seguía la regla `bool` vs byte. F5 se materializa en
HOST-136 más `tools/run/run-scalar-modes.sh` (mismo fuente en RETRO16/RETRO32/NATIVE). La tabla §7
incluye la columna `Fixed<s32>` y el antiguo `word.hpp` se eliminó (la aritmética de palabra vive en
`arith.hpp`: `mul_wide`/`div_wide`/`mulu16`). Pendiente menor: adoptar `eng::real`/`coord` en demos
concretas.

## 5. Plan de pruebas

1. **Matriz de escalares** (nuevo test host): el **mismo** algoritmo instanciado con
   `Fixed<s16,12>`, `Fixed<s16,8>`, `Fixed<s32,12>`, `MiniFloat16`, `float`, `double` e
   `int`; se compara contra una **referencia `double`** y se registra el error relativo y
   el número de operaciones. Cubre interp/geometry/spline/scalar_ops/noise/stats/dsp.
   Extiende el patrón de HOST-052/059/064/065/093/102.
2. **`Fixed` de 32 bits** (nuevo): suma/producto/rescale/cast/div/sqrt y
   `fixed_math` (sin/cos/atan2/exp2/log2) contra `double`; saturación y rango.
3. **Tipos nativos**: instanciar el vocabulario completo con `float`/`double`/`long` y
   comparar (los test actuales ya lo hacen en parte; se consolidan en la matriz).
4. **Codegen por target**: `tools/analyze/codegen-report.mjs` compila las mismas sondas a
   `-mcpu=68000` **y** `-mcpu=68020`. El camino 68000 conserva la exigencia de
   `muls.w`/`divs.w` y cero libcalls; el 68020 usa aritmética nativa. La aserción
   «`divs.w` obligatorio» pasa a ser **condicional al target**.
5. **Simulación retro en host**: `-DENG_SCALAR_RETRO16` fuerza los escalares de 16 bits en
   host (lo que ya ocurre con `q12`/`MiniFloat16`); `-DENG_SCALAR_NATIVE` corre lo nativo.
   Un test corre **ambos** e imprime las diferencias de precisión.
6. **Comparativa «cuánto mejor en moderno»**: informe con el error relativo por escalar y
   los conteos de instrucciones 68000 vs 68020 por algoritmo. Documento de resultados en
   `artifacts/` o en el propio README del test.
7. Mantener sincronizados `tools/check/scalar-support.mjs` (tabla función × escalar, ahora
   con `Fixed<s32>` y la columna nativa), `math-diagnostics` y `test-numbering`.

## 6. Criterios de aceptación

- **Cero regresión en 68000**: el codegen del camino retro no cambia (mismas instrucciones
  o mejor) y el gate sigue verde.
- **Host nativo**: compila con `float`/`double`/`int`/`Fixed<s32,E>` y la matriz pasa con
  tolerancias documentadas; se conoce el error de cada escalar.
- **`Fixed<s32,E>`**: división/`sqrt`/trig funcionan en host/68020; en 68000 quedan
  **excluidos por diseño** (el gate lo impide, no un fallo silencioso).
- **Una fuente por algoritmo**: ningún algoritmo duplicado por ancho de palabra.
- **`bool`** en booleanos semánticos; **bytes/ABI** intactos.

## 7. Riesgos y decisiones abiertas

- **Aritmética de 64 bits en 68000**: `wide<s32>` = `long long` son libcalls; por eso el
  `Fixed` de 32 bits no entra en el camino caliente del 68000. Decisión: vetarlo con guard.
- **Memoria de tablas**: una tabla de `s32` ocupa el doble que una de `s16`; el tamaño
  debe seguir siendo elegible por `Repr` y por compilación (como hoy `ENG_FIXED_SIN_SIZE`).
- **Series Taylor/Maclaurin**: elegir orden/número de términos por escalar y comprobar
  convergencia en `constexpr` para poblar tablas; para `float`/`double` basta la serie,
  para `s16` la tabla es más barata.
- **Compatibilidad de API**: conservar los alias retro (`q12`, `q8`, `MiniFloat16`) para no
  romper demos; el cambio es **aditivo**.
- **`bool` en structs**: nunca en ABI/DMA; documentar la regla en `CODING_STYLE.md`.
- **Alcance**: no generalizar tipos de dominio (píxeles/tiles/registros); solo el escalar de
  simulación.
