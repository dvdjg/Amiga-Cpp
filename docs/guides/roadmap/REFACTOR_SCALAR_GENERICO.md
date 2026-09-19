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

## 8. Replanteo: cabeceras genéricas, el tipo solo en config y `retro/`

**Regla nueva (corrige §2).** Ninguna cabecera define su propio alias concreto de escalar
(`using Coord = Fixed<s16,0>`), ni impone un ancho. El tipo se decide en **dos sitios**:

1. `eng/core/scalar.hpp` — los alias **configurables** `eng::intw`/`real`/`coord` (y, si
   hace falta, un `eng::real32` aparte). Es el único lugar que nombra el exponente de un
   modo dado.
2. `eng/retro/`, `eng/cpu/m68k/` — el vocabulario **retro 16 bits** (`q0`/`q12`, `s16`) y
   las especializaciones para ciertas funciones (trig por tabla, `muls.w`). Aquí SÍ se
   nombra `Fixed<s16,E>` a propósito (es el target y el ABI de los assets).

Todo lo demás (`core/`, `graphics`, `field`, `scene`, `ai`, `sim`, `platform/amiga`)
**usa `eng::coord`/`real` o es plantilla sobre `S`**; nunca crea un alias concreto. Los
tipos de **dominio** (píxeles/tiles/registros/`bob`/`bitmap`) siguen crudos: no son
escalares.

**Decisión ya aplicada:** en 68020 (`retro32`) `real`/`coord` siguen siendo
`Fixed<s16,12>`/`Fixed<s16,0>` (mismos assets que 68000); lo único que cambia es `intw`
(`s32`). Un fixed de 32 bits sería una opción **explícita**, no el defecto del target.

### 8.1 Cambios por cabecera (backlog)

| Cabecera | Hoy | Cambio |
|---|---|---|
| `core/mesh3d.hpp` | `using Coord = Fixed<s16,0>` local | usar `eng::coord` (o plantilla `S`); borrar el alias local |
| `platform/amiga/gfx3d.hpp` (`math3d`) | `Mat3 = Mat<3,q12>`, `Affine3 = Affine<3,q12,q0>`, trig `q12` | alias a `eng::real`/`eng::coord`; trig sobre `eng::real` (ver 8.2) |
| `platform/amiga/object3d.hpp` | piloto: `Point3S<retro::q0/q12>` | cambiar a `eng::coord`/`eng::real` (tras 8.2) |
| `core/fixed_math.hpp` | tablas/series **solo** `Fixed<s16,E>` | plantilla `Fixed<Repr,E>`; tablas por `Repr` |
| `core/linalg.hpp` | `scalar_div` solo `Fixed<s16,E>` | generalizar a `Fixed<Repr,E>` |
| `core/light.hpp`, `retro/lib2d.hpp`, `retro/minifloat_fixed.hpp` | tipos concretos | usar `eng::real`/`coord` o plantilla |
| `scene/actor.hpp`, `core/util/grid.hpp`, `collision.hpp`, `broadphase.hpp`, `color.hpp` | `Point2s`/AABB/RGB a 16 bits | son **dominio** (píxeles): se quedan; auditar sólo call sites con coma fija |

### 8.2 Problemas que sigo viendo (respuesta a «¿sigues viendo problemas?»)

1. **La trig es el cuello de botella real.** `math3d::load_rotate` depende de
   `retro::sin_q12/cos_q12`, que devuelven `Fixed<s16,12>`. Hasta que `fixed_math` esté
   parametrizado por `Repr`/`E`, `eng::real` **no puede** usarse en `math3d`/`object3d` si
   en algún modo `eng::real` ≠ `Fixed<s16,12>`. Es el primer trabajo, no el último.
2. **El `objdat` es un formato externo de 16 bits y de escalas mezcladas.** `point` es
   `q0`, `normal` es `q12`, y `rotate/scale/translate` usan `q0`/`q4`/`q12`. Eso choca con
   el modelo «un tipo = un exponente» y con `Fixed` (prohíbe mezclar): no hay un único `S`
   que describa un `Point3D` reutilizado. Opciones: (a) tipos 16-bit separados por campo
   (`PointQ0`/`NormalQ12`) sobre el mismo blob; (b) un lector que convierta a `eng::coord`/
   `real`. La (a) conserva el `reinterpret_cast` y el layout; la (b) es más limpia pero
   añade copia. **Decisión abierta.**
3. **El `real` de host.** Para que `mesh3d`/`object3d` usen `eng::coord` central sin romper
   el ABI de 16 bits, `eng::coord`/`real` deben ser de **16 bits también en host** (el
   proyecto es Amiga y reutiliza assets; la versión `float` es para tests de *algoritmos
   genéricos*, que se instancian explícitamente con `float`, no vía `eng::real`). Si se
   prefiere `float` en host para `eng::real`, entonces el `objdat` necesita un alias de
   ABI aparte (16 bits). **Decisión abierta** (afecta a los tests 135/136 y a `mesh3d`).
4. **Tests «golden» de 16 bits.** 050/053 fijan valores s16 exactos; el modo por defecto de
   los tests host debe ser el de 16 bits (retro16) o forzarse por test, si no, los valores
   cambian. Hoy se fuerza por test; conviene una política única.
5. **Rendimiento.** Generalizar no debe perder los `always_inline` ni introducir
   `__mulsi3`/`__divsi3`: el `codegen-report` (68000) sigue siendo el gate obligatorio en
   cada paso. La generalización debe ser de **tipos**, no de despacho dinámico.
6. **Tipos de dominio.** `bitmap`/`bob`/registros/`incbin` no son escalares: se quedan
   crudos; confundirlos con «deuda de escalar» sería un error.

**Orden recomendado:** (1) parametrizar `fixed_math`/`linalg` por `Fixed<Repr,E>` y la trig
por escalar; (2) `mesh3d`/`math3d` a `eng::coord`/`eng::real`; (3) `object3d` runtime a
`eng::coord`/`eng::real` y decidir 8.2-2; (4) auditar los call sites de coma fija; (5)
consolidar la política de tests (retro16 por defecto en host).

### 8.3 El patrón concreto: `scalar_sin<S>` en lugar de `sin_q12`

El vocabulario genérico **ya existe** en `core/scalar_math.hpp` (`scalar_sin<S>`,
`scalar_cos<S>`, `scalar_sqrt<S>`, …), con especializaciones para `float`/`double`. El
refactor no inventa nada: usa esos puntos de extensión y **borra** el vocabulario concreto
de `retro/angles.hpp` (`sin_q12`/`cos_q12`/`fix`). El algoritmo nunca nombra `q12`:

```cpp
// math3d/gfx3d: SOLO el algoritmo; el escalar es plantilla (instancia por defecto = eng::real)
template <class S = eng::real>
void load_rotate(eng::math::Mat<3, S>& m, angle_t<S> ax, angle_t<S> ay, angle_t<S> az) {
    const S sX = eng::math::scalar_sin<S>::op(ax), cX = eng::math::scalar_cos<S>::op(ax);
    // ... producto matricial con eng::math::dot / mul_norm sobre S
}
using Mat3 = eng::math::Mat<3, eng::real>;   // la instancia concreta, en un solo sitio
```

Pasos:

1. **`scalar_sin`/`scalar_cos` para `Fixed<Repr,E>`**: la especialización de
   `Fixed<s16,12>` usa la tabla `kSinTab` (idéntica, para no mover los golden); para
   `Fixed<s32,E>`/`Fixed<s16,8>`… serie o tabla propia. **Misma firma** para todos.
   Contrato fijado: ángulo en **radianes** (como `float`/`double`); la conversión a índice
   de tabla queda dentro de la especialización. **Hecho** en `eng/retro/fixed_trig.hpp`
   (`scalar_sin`/`scalar_cos`/`scalar_sincos<Fixed<s16,12,P>>` con `kSinTab`) y cubierto
   por HOST-177, que instancia un mismo algoritmo con `float` y con `q12`.
2. **`gfx3d`/`math3d` a plantillas sobre `S`** (`load_rotate`, `load_reverse_rotate`,
   `scale`, `transform`, `inverse_rigid`). Los alias `Mat3`/`Affine3`/`P3` son la
   instanciación por defecto y viven en **un** sitio.
3. **El ángulo deja de ser `u16`** en la API: es `angle_t<S>` (`float` → radianes; fixed →
   índice 0..N). A diseñar: `scalar_angle<S>` (tipo + ops) o que `scalar_sin<S>` documente
   su unidad. Es el punto que evita «argumentos con tipo prefijado».
4. **`angles.hpp` desaparece**; su contenido va a las especializaciones retro de
   `scalar_sin`/`scalar_cos` (donde SÍ procede nombrar `Fixed<s16,12>`), junto a
   `fixed_math.hpp`.
5. **Límite innegociable**: el `objdat` empaquetado (`obj2c`, 16 bits, escalas mezcladas),
   los registros de hardware y la geometría de píxeles **no** son escalares; se quedan como
   están (formato/dominio). Todo lo demás (matemática, escena, sim) va por `S` o por
   `eng::real`/`coord`.

### 8.4 Estado: pasos 1–3 hechos

- **`gfx3d` genérico** (`platform/amiga/gfx3d.hpp`): `Mat3<S>`, `Affine3<SR,SL>` y `P3<S>`
  son alias de plantilla; `load_rotate`/`load_reverse_rotate`/`scale`/`transform`/
  `inverse_rigid` reciben el escalar por plantilla y el **ángulo en radianes** del propio
  `S`; la única operación concreta es `scalar_sin<S>`/`scalar_cos<S>`. Ninguna firma nombra
  `q12`, `q0`, `fix` ni `u16`.
- **`retro/angles.hpp` retirado**: su contenido vive en `retro/fixed_trig.hpp` (tabla
  exacta + `scalar_sin`/`scalar_cos`/`scalar_sincos<Fixed<s16,12,P>>` en radianes), más
  `angle_to_radians` para quien piensa en el índice `0..4095`. El `rotate(Mat2x2&)` 2D pasó
  a `retro/lib2d.hpp` (ángulo en radianes).
- **Radianes de punta a punta**: `object3d::Angle3` es un `q12` en radianes; las demos
  `077/078/079/116/117` convierten su índice de frame con `angle_to_radians`; la conversión
  índice→radianes→tabla es **exacta** (redondeo al más cercano), así que los gates
  HOST-050/051/053 (bit-exactitud y tabla dorada) siguen verdes y la demo 117 no cambia de
  resultado (25.04 fps, 2.0 líneas/frame).
- **Instancia por defecto**: los alias usan `eng::real`/`eng::coord`; los tests host de la
  capa Amiga fijan `-DENG_SCALAR_RETRO16` para validar la instancia de producción
  (`eng::real=q12`, `coord=q0`).
- **Blob `obj2c` tipado** (`object3d`): `Mesh3D::bytes`/`Object3D::objdat` son `Span<u8>`
  (ya no `void*`), los campos llevan su escala (`Point3D` q0, `Face::normal` q12) y
  `new_object3d` valida el descriptor (`mesh_validate`). Los **grupos y offsets siguen
  `s16`**: son offsets de byte del `obj2c` (y del asm `flatshade_asm.s`), no escalares.
- **Por qué `Object3D`/`Mesh3D` no se plantillan sobre el escalar**: su layout es ABI
  (el asm lee `objdat`@0, grupos@4/8/12, `objectToWorld`@38…); meter un `Span`/tipo más
  ancho en esos campos movería los offsets. La generalización aplica a la **aritmética**
  (`Affine3<>`/`P3<>`/`load_rotate`), no al layout empaquetado.
- **`scalar.hpp` consolidado**: se queda en `core/` como **única** selección por target.
  `intw` lo usa el núcleo (`eng::board`); `real`/`coord` son la instancia por defecto de la
  capa de plataforma/3D (hoy sólo `gfx3d` los consume). El resto del engine es plantilla y
  no los nombra. No se mueven a `platform/` para no duplicar las macros de selección.
- **Ángulo con unidad en el tipo**: `eng::math::Angle<S, Unit>` (`radians`/`turns`/
  `degrees`/`unit`) + `sin`/`cos` (punto de entrada único). El ángulo-vueltas del original
  es `Turns = Angle<q12, turns>` con `sin`/`cos` por tabla exacta; se eliminan
  `sin_q12`/`cos_q12` y las funciones con el formato en el nombre.
