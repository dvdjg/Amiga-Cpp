# Librería de matemáticas 2D/3D: diseño y roadmap

Este documento define el **modelo objetivo** de las matemáticas del engine y el plan
para llegar a él. No describe la evolución: describe cómo deben ser los tipos y las
operaciones, y qué se niega explícitamente a permitir.

## 1. Objetivo

Una librería de álgebra lineal:

- **Genérica**: el escalar es un parámetro de plantilla (fixed-point, float, half,
  complejo, racional…). Nada de `s16` cableado.
- **Matemáticamente correcta**: las operaciones no mezclan magnitudes de naturaleza
  incompatible; si hay que mezclarlas, la conversión es **explícita** y la decide el
  compilador por metaprogramación, con el redondeo más preciso posible.
- **Rápida en 68000**: el mismo código genérico debe producir `muls.w`/`divs.w`
  nativos, sin `__mulsi3`/`__divsi3`, sin temporales y sin tocar memoria que no toque.
- **Rápida en host**: la multiplicación de matrices (N pequeño) debe ser tan rápida
  como la mejor implementación posible para ese tamaño (por desenrollado), y la API
  no debe impedir vectorización cuando el escalar es `float`.
- **Testeable**: las invariantes del sistema de tipos y del redondeo se fijan con
  tests host antes de tocar el engine.

## 2. Por qué el modelo actual mezcla cosas

### 2.1 El exponente no está en el tipo

Hoy todo el fixed-point es `s16`/`s32` y el formato vive **en un comentario**
(`fix` = 4.12, `fix88` = 8.8, `fix8_24` = 8.24). El compilador no puede distinguirlos,
así que `m.x + x` (un 4.12 más un entero) compila sin rechistar y el error es
silencioso. Los alias `fix`/`fix88`/`fix8_24` documentan, pero **no protegen**.

### 2.2 Qué es cada elemento (semántica que hoy no se declara)

Cada hueco de una matriz de transformación guarda una magnitud distinta:

```
        | m00 m01 m02 | tx |      filas 0..2, cols 0..2 : RATIO      (adimensional)
   M =  | m10 m11 m12 | ty |      columna 3              : LONGITUD   (coordenada)
        | m20 m21 m22 | tz |      M * p + t              : LONGITUD
```

- **RATIO**: un factor sin unidades (`cos`, `sen`, escalas). En 4.12.
- **LONGITUD**: una coordenada en el espacio del objeto/mundo. Entera.
- **ÁNGULO**: un índice 0..4095 (no es fixed-point: es un índice de tabla).

Entonces:

- `transform`: `LONGITUD = RATIO * LONGITUD + LONGITUD`. El `normfx(m * x)` del
  original es **exactamente** eso: `m` es RATIO (exp 12), `x` es LONGITUD (exp 0), el
  producto queda en exp 12 y se **normaliza a exp 0** (un solo `>> 12`). Es correcto.
- `translate`: `m.x = m.x + x` suma un **entero** (LONGITUD) a un campo **4.12**
  (RATIO). Esto **es la inconsistencia**: o `m.x` no es un RATIO (la traslación es una
  LONGITUD y debería estar a exp 0), o el entero está mal escalado. El modelo tipado
  lo convierte en un **error de compilación**, y obliga a decidir.

### 2.3 La normalización es fusionada, y eso es lo correcto

Las matrices no normalizan cada producto, sino la **suma** de productos:

```
   m00*x + m01*y + m02*z   (todo en exp 24, suma EXACTA)
   >> 12                   (una sola normalización)
```

Con el exponente en el tipo esto sale **natural**: los productos comparten exponente
(24), sumarlos es exacto y sólo hay un desplazamiento. No es un truco: es el orden
correcto para no perder precisión acumulando redondeos.

## 3. Diseño propuesto

### 3.1 El escalar: exponente en el tipo

```
   Fixed<Repr, Exp>            valor = v * 2^-Exp
   Repr: s16 | s32 | float | half | complejo...
   Exp : bits de fracción (12 = 4.12, 0 = entero, 24 = 8.24; admite negativos)
```

Aritmética (todo en compilación):

```
   Fixed<Ra,Ea> * Fixed<Rb,Eb> -> Fixed<Wide<R>, Ea+Eb>   exponentes SUMAN
   Fixed<R,E>   + Fixed<R,E>   -> Fixed<R,E>              sólo mismo exponente
   Fixed<R,Ea>  + Fixed<R,Eb>  -> NO EXISTE (error)       (Ea != Eb)
   rescale<Edst>(Fixed<R,E>)      -> Fixed<R,Edst>           un solo desplazamiento
   from_int<R>(i)              -> Fixed<R,0>              conversión explícita
```

`Wide<R>` es la promoción de la representación (`s16->s32`, `s32->s64`, `float->float`):
el producto debe ensancharse o desborda. Es una pieza obligatoria del diseño, no un
detalle: `s16(4.12) * s16(4.12)` da 8.24 y **no cabe** en `s16`.

Flujo de exponentes en el camino caliente (ejemplo del `transform`):

```
   RATIO(12) * LONGITUD(0)  ->  exp 12            (exacto, sin normalizar)
   (exacto) + (exacto)      ->  exp 12
   rescale<0>(...)             ->  LONGITUD          (un >>12)
```

Y en el producto escalar fusionado de lib3d:

```
   RATIO(12) * RATIO(12)    ->  exp 24            (muls.w, exacto)
   exp24 + exp24            ->  exp 24            (add.l exacto)
   rescale<12>(...)            ->  RATIO(12)         (un >>12)
```

### 3.1.b La política va en el tipo (mismo layout, distinta matemática)

El escalar lleva un tercer parámetro, `Policy`, que **no cambia el layout** (`sizeof` y
`Repr` idénticos) pero sí el resultado de las operaciones. Es el punto donde se elige
por algoritmo el compromiso precisión/rendimiento sin duplicar tipos ni reordenar
memoria:

- **Redondeo** al normalizar: truncar hacia −inf (`>>`, gratis y fiel al original),
  al más cercano (`+2^(n-1)` antes del `>>`, un `add` más) o al par (banquero, sin
  sesgo acumulado, bastante más caro). Medido en 68000 al bajar 8.24 → 4.12:
  **4 / 5 / 17** instrucciones respectivamente.
- **Desbordamiento** al estrechar: envolver (complemento a 2, gratis) o saturar
  (clamp, dos comparaciones).
- **`retag<P>()`**: mismo dato, otra política, coste cero. Permite aplicar el cálculo
  exacto en el transform y el truncado en una previsualización, sobre el MISMO buffer.

El abanico es extensible a otras decisiones del mismo tipo (estrategia de división,
anchura del acumulador) sin tocar a los usuarios de la librería.

### 3.2 El concepto `Scalar`

Un tipo es utilizable como escalar si satisface un concepto (`Scalar`) con: `+`, `-`,
`*`, `zero`, `one`, una promoción `Wide` y un reescalado `rescale<Exp>`. Para `float`
y `double` la multiplicación no cambia de exponente y `rescale` es la identidad; para
`half` se resuelve con las mismas reglas que el fixed. Para complejos, igual (el
producto usa la aritmética del cuerpo). El álgebra lineal es **el mismo código** para
todos.

Dos operaciones **normalizadas** cierran el contrato y hacen posible el mismo algoritmo
para todos (`fixed.hpp`/`linalg.hpp`, ver `SCALAR_LIBRARY.md` §3.b):

- `mul_norm(a, b)`: producto normalizado al propio escalar (identidad en `float`/`half`;
  `rescale` en fixed, donde `a*b` pasa a exponente doble). Es lo que permite escribir
  `lerp`, `cross2`, `rotate2`, `vscale`… sin saber si el escalar cambia de exponente.
- `div_norm(a, b)`: división **explícita** vía `scalar_div<S>`. El núcleo de `Fixed` no
  define `operator/` a propósito; el fixed retro aporta una política explícita (división
  saturante con `divs.w` en 68000), de modo que `remap`/`inv_lerp` funcionan sobre fixed
  sin abrir la puerta a divisiones implícitas.

### 3.3 Álgebra lineal genérica

```
   Vec<N,S>       vector de N escalares S
   Mat<N,S>       matriz NxN (parte LINEAL: RATIO)
   Affine<N,S>    Mat<N,S> + Vec<N, LONGITUD>   (transformación afín completa)
```

Operaciones, sin temporales y desenrolladas (N constante):

```
   Mat<N,S> * Mat<N,S>   -> Mat<N,S>          (ratio * ratio = ratio)
   Mat<N,S> * Vec<N,L>   -> Vec<N,L>          (ratio * longitud = longitud)
   Affine * Vec<N,L>     -> Vec<N,L>          (M*v + t)
   transpose, identity, inverse (2x2/3x3 analíticas)
```

### 3.4 Lo que NO se permite (la regla)

- **Ninguna conversión implícita** entre exponentes ni entre representaciones.
- **Ningún `operator` entre un `Fixed` y un entero** genérico: multiplicar por una
  coordenada o sumar una traslación son operaciones **con nombre** (`mul_i`,
  `translate`) o constructores explícitos (`from_int`, `norm`).
- Ninguna matriz de `float` operando con una de `Fixed` sin una conversión declarada.

## 3.5 Qué es «genérico» y qué es «backend» (regla de las cabeceras)

La librería se separa en tres capas y **cada truco vive en la suya**:

```
   ┌──────────────────────────────────────────────────────────────────┐
   │ A. núcleo genérico (eng::math)                                    │
   │    Fixed<Repr,Exp,Policy>, Vec, Mat, Affine + operadores          │
   │    SÓLO matemática portable. Sin asm, sin empaquetados, sin 4.12  │
   │    concreto. Sirve igual para float, half, complejo o un fractal. │
   ├──────────────────────────────────────────────────────────────────┤
   │ B. especializaciones de plataforma (cada una en SU archivo)       │
   │    p. ej. backend Amiga: `muls.w`/`divs.w`, empaquetado de dos    │
   │    productos en un registro, tablas de seno 4.12, formatos fijos  │
   │    (`q12`, `q0`, `fix88`). Atari ST, Mega Drive, etc. tendrían    │
   │    los suyos. NUNCA se incluyen desde A.                          │
   ├──────────────────────────────────────────────────────────────────┤
   │ C. lógica del efecto (lib3d, demos, juegos)                       │
   │    escribe MATEMÁTICA: `M * v + t`, `dot(a,b)`. No llama a         │
   │    `vertex_mul` ni elige trucos: el compilador instancia la mejor │
   │    versión a partir de los TIPOS de los argumentos.               │
   └──────────────────────────────────────────────────────────────────┘
```

Consecuencias (criterio para aceptar código):

- **Prohibido en A**: `asm`, `#if defined(__mc68000__)`, `mul16`/`mulu16`/`div16` con
  implementación específica, constantes como `1 << 12` con significado de formato,
  y cualquier función tipo `vertex_mul` que sólo tenga sentido con un reparto concreto
  de registros.
- **Prohibido exponer una función de bajo nivel**: la operación se expresa con los
  **operadores** de las plantillas (`*`, `+`, `dot`) y, si la plataforma tiene una
  versión mejor, se selecciona por **especialización/sobrecarga** según los tipos. El
  nombre que ve el programador describe *matemática* (`transform`, `dot`), no *mecánica*
  (`mul_packed_16`).
- **Punto de extensión, no `#ifdef`**: una plataforma aporta sus versiones
  especializando los rasgos del núcleo (`scalar_traits`, y los puntos de personalización
  de las operaciones) desde su propio fichero de backend.

### 3.5.1 Los errores de compilación son parte del API

El tipado existe para que el compilador **rechace lo que no tiene sentido** con un
mensaje comprensible, no con un error de plantilla ilegible. Objetivo de calidad:

| Se intenta | Debe pasar |
|---|---|
| `4.12 + entero` | **no compila** (exponentes distintos) |
| `4.12 + 8.8` | **no compila** |
| `Mat<3,float> * Vec<3,q12>` | **no compila** (escalares distintos) |
| `a * b` con resultado que no cabe en `R` | **aviso**: la promoción automática lo evita; si se fuerza el estrechado, el usuario debe pedirlo (`cast`, con su política) |
| `rescale<Edst>` que pierde muchos bits | documentado en la política de redondeo elegida |

Los `static_assert` con mensaje (`"...: mezcla 4.12 con entero"`) y conceptos
(`Scalar`, `SameExp`) son la herramienta: el error debe decir *qué* se mezcló. En `Fixed`
está implementado con sobrecargas que sólo existen para disparar el `static_assert` (en vez
del «no matching function» de la sobrecarga ausente); `tools/check/math-diagnostics.sh`
exige el **mensaje**, no sólo que falle.

### 3.5.2 API pública: nombres estabilizados

Un efecto o juego escribe **sólo** el vocabulario genérico; la especialización (4.12,
`muls.w`, empaquetados) se elige **dentro**, según los tipos que entren en la operación.

```
   PÚBLICO                                      INTERNO (puede cambiar sin aviso)
   ───────────────────────────────────────      ─────────────────────────────────
   Fixed<Repr, Exp, Policy = DefaultPolicy>      arith<R>            (backend 68000)
   Vec<N,S>  Mat<N,S>  Affine<N,SR,SL>           wide / common_repr / mul_repr
   + - * == != < (y - unario)                    limits, detail::rshift/sat_add_repr
   dot(...)   transform(...)                      pack3_ops, projector
   mul_norm(a,b)   div_norm(a,b)                  mapper
   from_int<R>(i)   to_int(a)
   rescale<Exp>()  cast<Repr>()  retag<Policy>()
   DefaultPolicy / RoundPolicy / SaturatePolicy
   scalar_traits<S> / numeric_traits<S>  ( puntos de extensión )
   scalar_div<S> / scalar_sqrt<S>        (                      )
```

Las tres conversiones tienen nombres que no se pisan: `rescale` cambia el exponente,
`cast` la representación y `retag` sólo la política (coste cero). Los **puntos de
extensión** que toca quien añade un escalar son `scalar_traits<S>` (álgebra),
`numeric_traits<S>` (límites), y —si los algoritmos usados dividen o hacen `sqrt`—
`scalar_div<S>` / `scalar_sqrt<S>`. `mul_norm`/`div_norm` son el vocabulario estable que
usan los algoritmos; el resto de la metaprogramación (`wide`, `common_repr`, `mul_repr`)
queda fuera del contrato.

Las convenciones **Q** (`q12`, `q0`, `q24`, `q12_round`, `q12_sat`) **no** son núcleo:
son vocabulario de la especialización retro y viven en `engine/include/eng/retro/fixed_q.hpp`
(namespace `eng::retro`). `q12 = Fixed<s16,12>` es una decisión retro (palabra de 16 bits,
4 bits de fracción, atada a la tabla de seno y al layout del original); tenerla en `eng::math`
haría que el núcleo (que también sirve a `float` o a un complejo de usuario) hablara en Q12.

### 3.6 Escalares concretos registrados

Además de `float` y `Fixed<Repr,Exp,Policy>`, el núcleo tiene un escalar de **coma
flotante de 16 bits**, `eng::math::MiniFloat16` (`eng/core/minifloat.hpp`), registrado
con `scalar_traits` en `linalg.hpp`: `Vec`/`Mat`/`Affine` funcionan con él sin código
propio. Su formato, rango, precisión y reglas de uso están en
[MINIFLOAT16.md](MINIFLOAT16.md); sus funciones matemáticas (`sqrt`, `exp`, `log`,
`log2`/`log10`, `pow`, `hypot`, trigonometría e inversas) viven en
`eng/core/minifloat_math.hpp` y **no** son todavía parte del concepto `Scalar` que el
álgebra lineal exige (el álgebra solo necesita `+ - *` y los rasgos); se documentan en
ese mismo fichero de referencia. El puente con el fixed retro (`fix`/`fix88`), incluida
la transformación de coordenadas fijas con una matriz MF, está en
`eng/retro/minifloat_fixed.hpp` (ver [MINIFLOAT16.md](MINIFLOAT16.md) §8): usa el
exponente de `Fixed<s16,Frac>` como tag de escala en vez de duplicar un tipo de
coordenada.

### 3.6.1 Tablas de trigonometría `Fixed`: coste y precisión

Las funciones trigonométricas de `Fixed<s16,E>` (`eng/core/fixed_math.hpp`) se apoyan en tablas `static constexpr` compartidas por `(E, Size, Iter)`. El tamaño de cada tabla es un parámetro de plantilla (`fixed_sin<E,Size,Iter>`, `fixed_exp2<E,Size>`, `fixed_log2<E,Size>`, `fixed_atan2<E,Size>`) y su valor por defecto se fija por compilación con `ENG_FIXED_SIN_SIZE`, `ENG_FIXED_SIN_ITER`, `ENG_FIXED_EXP2_SIZE`, `ENG_FIXED_LOG2_SIZE` y `ENG_FIXED_ATAN_SIZE`.

| Tabla | Muestras (defecto) | Tipo | `.rodata` (defecto) | Error típico |
|---|---|---|---|---|
| seno/coseno | 1024 (paso 2π/1024) | s16 | 2 KiB | ~5e-3 |
| `exp2` | 256 | s32 | 1 KiB | — |
| `log2` | 256 | s32 | 1 KiB | — |
| `atan` (`atan2`/`asin`/`acos`) | 257 sobre `[0,1]` | s32 | ~1 KiB | — |

El seno admite 4096 (~1e-3, 8 KiB) y 256 (~2e-2, 512 B) como extremos de precisión/coste. Medido con `size`: ~5 KiB de tablas con los valores por defecto y ~1.8 KiB con `-DENG_FIXED_SIN_SIZE=512 -DENG_FIXED_EXP2_SIZE=64 -DENG_FIXED_LOG2_SIZE=64 -DENG_FIXED_ATAN_SIZE=64`. Una tabla cuyos usos tienen argumento constante se pliega en compilación y no llega a enlazarse. `fixed_sincos` calcula seno y coseno del mismo ángulo con un solo índice (dos lecturas de la tabla de seno); el factor `2π` de `wrap_angle`/`angle_diff` es una constante, no una tabla. Verificado por HOST-104 y por la demo `110_ylimited_shooter`.

### 3.7 Estado de tipado de las cabeceras de geometría/3D

Criterio: una cabecera usa los escalares tipados (`Fixed`/`MiniFloat16` y
`Vec`/`Mat`/`Affine`) salvo que su crudeza sea **contrato** (layout empaquetado, DMA) o
**rendimiento medido** (una multiplicación que se ensancharía a 64 bits y no enlazaría en
68000). El gate de codegen (`tools/analyze/codegen-report.mjs`) fija que estos algoritmos
no llamen a libgcc (`__mulsi3`/`__divsi3`) ni usen instrucciones de 68020.

| Cabecera | Estado | Motivo |
|---|---|---|
| `core/mesh3d.hpp` | tipada | `Vec3 = Vec<3, Fixed<s16,0>>` (LONGITUD); el producto mixto usa `arith<s16>` (`muls.w`) y `mul32x16`, sin `__mulsi3` |
| `platform/amiga/gfx3d.hpp` | tipada | `Mat3`/`Affine3`/`Vec3` tipados; `scale` recibe `q12` |
| `graphics/mesh_renderer.hpp` | tipada | opera sobre `Vec3` tipado; `focal` sigue en 4.12 crudo (convención) |
| `assets/uaf.hpp` | tipada | las sub-vistas exponen vistas de dominio (`AudioSample`, `PlaneViewBytes`, `IndexedTiles`, `PaletteWords`, `SpriteWords`, `CopperWords`, `MeshAssetView`→`Vec3`); el contenedor/lectura sigue en big-endian portable |
| `retro/lib2d.hpp` | tipada | `Vec2`/`Mat2x2`/`Rect` tipados y `translate`/`scale` toman `q0`/`q12`; el recorte (`clip_*`, Liang-Barsky/Sutherland-Hodgman) es aritmética de enteros de píxel por diseño |
| `scene/route_camera.hpp` | tipada | posición `Vec<2, Coord>` y offsets de la circunferencia generados con `eng::SineTable<4096,64>` (sin tabla a mano) |
| `graphics/effects/rotozoom.hpp` | cruda (motivo) | 16.16 en `s32`: `mul_repr<s32>` ensancha a 64 bits → `__muldi3` (no enlaza en 68000); el asm comparte el layout `RotozoomSteps` |
| `platform/amiga/object3d.hpp`, `lib3d.hpp` | cruda (motivo) | layout empaquetado `obj2c` 1:1; encima se tipa (`Affine3`, `P3`) |
| `core/light.hpp`, `isqrt.hpp`, `fast_div.hpp`, `arith.hpp`, `word.hpp` | cruda (diseño) | aritmética de enteros/manipulación de bits; no son escalares |
| `retro/fixed_q.hpp`, `retro/minifloat_fixed.hpp` | puente | vocabulario crudo (`fix`/`fix88`) para ports + conversiones tipadas |

### 3.8 Estado de tipado de `eng/field` y `eng/graphics`

Son capas de **composición y hardware**: su crudeza es casi siempre **contrato** (registros,
Blitter, Copper, DMA, layout planar, anchos/strides/planos/índices/máscaras). Clasificación de
las 43 cabeceras:

| Área | (A) tipada | (B) cruda adaptable | (C) cruda por diseño |
|---|---|---|---|
| `eng/field/` | — | `surface.hpp` (`SurfaceRect`), `tile_demo.hpp` (cámaras Q16, sin consumidor), campos de cámara de `scroll_engine.hpp` (`ScrollState`) y `playfield.hpp` (`PlayfieldHardwareView`) | el resto (composición, layout, índices) |
| `eng/graphics/` | `mesh_renderer.hpp` (`math3d::Vec3`/`Affine`; `focal`/`sx`/`sy` en 4.12/píxel crudo por convención) | `drivers/tile_scroll.hpp` (`ScrollPosition2`/`TileScrollInput`) | el resto (registros, Copper, C2P, fuentes, drivers) |

Reglas:

- Un **ancho/alto/stride/plano/índice/máscara** es entero por contrato: no se tipa como escalar.
- Una **coordenada/ratio/ángulo** debería ser `Coord`/`q12`/`Vec`; los campos (B) son los
  candidatos (hoy crudos por herencia, no por necesidad).
- El punto fijo que **no se puede tipar** sin romper el enlazado (p. ej. 16.16 en `s32`) queda
  crudo y documentado en su cabecera.
- El **rendimiento** de esta aritmética (divisiones/módulos en caminos calientes) se audita en
  `docs/guides/optimization/OPTIMIZACION_GPP_68000.md` §11, y el gate
  `tools/analyze/codegen-report.mjs` cubre las sondas 2D/3D (`project_perspective`, `clip_line`,
  `face_signed_area`, `update_object_transformation`).

**Campos (B) que se dejan enteros a propósito** (decisión, no deuda):

| Campo | Por qué entero | Cómo tiparlo si algún día se decide |
|---|---|---|
| `field::SurfaceRect{x,y: s32; w,h: u16}` | espacio **lógico** sin tag (mundo o pantalla) y rango de mundo de 16 bits (`world_w: u16` ≤ 65535); `w`/`h` son extensión, no escalar | con etiquetas de espacio (decisión abierta §6.3): `Coordinate<Space, Fixed<s32,0>>` |
| `field::ScrollState{mapposx/videoposx/mapposy/videoposy: s32}` | posición de cámara acumulada sobre el rango del mundo, que **no cabe en `s16`** (`world_w/h: u16`) | `Fixed<s32,0>` (LONGITUD de 32 bits), nunca `Coord`/`Fixed<s16,0>` |
| `field::BigBufferScroll{position,min_pos,max_pos: s32}` | offset de cámara del "big buffer" con clamp/anillo sobre el mundo (mismo rango) | `Fixed<s32,0>` |
| `graphics::ScrollPosition2{x,y: u16}` / `TileScrollInput` | posición **sin signo** en espacio bitmap/tile (offsets de fetch) | `Fixed<u16,0>` o dejarlo crudo: es offset de hardware |
| `field::tile_demo.hpp` (`CameraQ16`, `sin_smooth`, …) | **helper de demo sin consumidor** (solo `pf_plane_row`/`cell_hash`/`kPalette` se usan) → marcar NO VERIFICADA, no tipar | retirar o promover a API con demo/test |

La razón de fondo común: `Coord` es `Fixed<s16,0>` (LONGITUD de 16 bits) y estas cámaras
manejan el rango de un mundo de 16 bits, que lo desborda. Tiparlas bien exige `Fixed<s32,0>`
**y** etiquetas de espacio; mientras no existan, el entero con nombre claro es la opción
correcta (y evita el `__muldi3` que saldría de multiplicar `s32`).

## 4. Rendimiento y metaprogramación
- **N constante** y **sin matrices temporales**: la acumulación va directa a los
  registros del resultado. Qué hacer con el bucle (desenrollar o dejarlo plegado) se
  decide **midiendo**: el 68000 tiene un prefetch de 2 palabras y un cuerpo compacto
  puede batir a uno desenrollado que no quepa. El compilador hoy lo deja plegado.
- **Especialización por plataforma**: el mismo `Mat<N,S>::operator*` genera el bucle
  óptimo en 68000 (acumulador de 32 bits, `muls.w`, `dbra`) y vectoriza en host
  (`float`, sin `-ffast-math`). Se elige por traits de plataforma, no por `#ifdef`
  desperdigados.
- **Especialización por escalar**: fixed-point usa multiplicación entera y
  desplazamiento; `float` usa la unidad de coma flotante; `half` usa conversiones
  explícitas.
- **Medición**: 
  - Host: micro-benchmark de `Mat3<float>::operator*` contra (a) bucle naive, (b) una
    llamada genérica tipo BLAS/Fortran (`dgemm`) y (c) generadores especializados por
    el compilador. Para N=2/3/4 el desenrollado debe ganar al *overhead* de la llamada
    genérica; para N grande (blocking/SIMD) queda fuera del alcance del efecto, pero
    el diseño no lo impide.
  - Amiga: ciclos por frame con el periférico de depuración, y comprobación en el
    `.s` de que no hay `__mulsi3`/`__divsi3` ni escrituras a memoria sobrantes.

Prueba de concepto ya medida (prototipo, 68000 `-O2`), para `dot` fusionado:

```asm
        move.w  6(%sp),%d0
        muls.w  10(%sp),%d0     ; producto 1 (muls.w nativo, 16x16)
        move.w  14(%sp),%d1
        muls.w  18(%sp),%d1     ; producto 2
        add.l   %d1,%d0         ; suma exacta (mismo exponente)
        moveq   #12,%d1
        asr.l   %d1,%d0         ; una sola normalización
        ext.l   %d0
        rts
```

Es el código ideal: dos `muls.w`, una suma larga y un desplazamiento. Sin libcalls,
sin temporales.

## 5. Plan por fases (tests primero)

- **F0 — Especificación ejecutable.** Tests host que fijan: suma de exponentes en el
  producto, promoción de `Wide`, la no-mezcla (`4.12 + entero` no compila), el
  redondeo de `norm` (hacia −inf, como `normfx`) y la equivalencia
  `norm(a*b + c*d) == norm_fusionado(...)` frente a hojas separadas.
- **F1 — `Scalar` + `Fixed<Repr,Exp>`.** Tipos, aritmética, `norm`, `from_int`,
  promoción; verificar el `.s` en 68000 (sin libcalls).
- **F2 — `Vec`/`Mat`/`Affine`.** Operaciones desenrolladas, sin temporales;
  test de **bit-exactitud** contra la implementación actual para todos los ángulos del
  demo (mismo resultado binario) antes de sustituir nada.
- **F3 — Migración.** `lib2d` (retro), `math3d`, `object3d` y `lib3d` sobre la librería
  nueva. La única fórmula del original que cambia de forma es `translate` (queda
  tipada). Gate: 49 tests host + regresión + `verify-116`.
- **F4 — Rendimiento.** Benchmarks host + Amiga; documentar el codegen resultante y
  las desviaciones (si las hay) respecto al original.

## 6. Decisiones abiertas

1. **`Exp` con signo o sin signo.** Con signo admite escalas negativas (valor 2^k),
   útil para tablas de punto fijo; sin signo es más simple y cubre todo el uso actual.
2. **Bit-exactitud.** ¿Se mantiene el redondeo del original (hacia −inf, `>>`) o se
   adopta redondeo al más cercano donde mejore la precisión? Cambiar el redondeo
   cambia píxeles: exige re-validar el gate visual.
3. **Unidades/espacios.** ¿Basta con el exponente, o se añaden etiquetas de espacio
   (objeto/mundo/pantalla) para que `Mat` de espacios distintos no se pueda multiplicar?
4. **La traslación.** ¿Pasa a `LONGITUD` (exp 0, matemáticamente correcto) aunque
   cambie el layout empaquetado del `objdat`, o se mantiene el layout y se tipa sólo
   la capa de cálculo con una conversión explícita?
5. **Frontera con el `objdat`.** ¿Sustituir el formato empaquetado de lib3d por la
   representación tipada, o conservarlo (compatibilidad con las mallas generadas) y
   tipar sólo el cálculo?
6. **Alcance del benchmark.** ¿"Rivalizar con Fortran" se limita a N pequeño (2/3/4,
   donde el desenrollado gana) o se quiere también el camino grande (blocking/SIMD)?

## 7. Verificación

- El prototipo del núcleo (`Fixed<Repr,Exp>` + `dot` fusionado) ya está validado:
  invariantes en host y `.s` óptimo en 68000.
- Cada fase se cierra con su test host antes de tocar la siguiente capa.
- La migración (F3) se valida por **bit-exactitud** contra la implementación actual, no
  sólo por cobertura: cualquier diferencia de píxel es un fallo.
- **Hardware sin `float`**: `tests/l0_bare_metal/020_math_scalars` reejecuta el vocabulario
  (MF/q12/q8 + operaciones entre tipos + `mesh3d`/`light`/`isqrt`/ángulos) en el 68000, con
  el veredicto por canal lateral (`verify-math.sh`).
- **Codegen**: `tools/analyze/codegen-report.mjs` compila una sonda por función clave
  (incluido `face_signed_area` de `mesh3d`) y falla si aparecen libcalls de libgcc,
  instrucciones de 68020 o helpers de gameplay sin inlinear.
