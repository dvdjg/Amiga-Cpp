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
   norm<Edst>(Fixed<R,E>)      -> Fixed<R,Edst>           un solo desplazamiento
   from_int<R>(i)              -> Fixed<R,0>              conversión explícita
```

`Wide<R>` es la promoción de la representación (`s16->s32`, `s32->s64`, `float->float`):
el producto debe ensancharse o desborda. Es una pieza obligatoria del diseño, no un
detalle: `s16(4.12) * s16(4.12)` da 8.24 y **no cabe** en `s16`.

Flujo de exponentes en el camino caliente (ejemplo del `transform`):

```
   RATIO(12) * LONGITUD(0)  ->  exp 12            (exacto, sin normalizar)
   (exacto) + (exacto)      ->  exp 12
   norm<0>(...)             ->  LONGITUD          (un >>12)
```

Y en el producto escalar fusionado de lib3d:

```
   RATIO(12) * RATIO(12)    ->  exp 24            (muls.w, exacto)
   exp24 + exp24            ->  exp 24            (add.l exacto)
   norm<12>(...)            ->  RATIO(12)         (un >>12)
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
`*`, `zero`, `one`, una promoción `Wide` y una normalización `norm<Exp>`. Para `float`
y `double` la multiplicación no cambia de exponente y `norm` es la identidad; para
`half` se resuelve con las mismas reglas que el fixed. Para complejos, igual (el
producto usa la aritmética del cuerpo). El álgebra lineal es **el mismo código** para
todos.

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
| `a * b` con resultado que no cabe en `R` | **aviso**: la promoción automática lo evita; si se fuerza el estrechado, el usuario debe pedirlo (`narrow`, con su política) |
| `norm<Edst>` que pierde muchos bits | documentado en la política de redondeo elegida |

Los `static_assert` con mensaje (`"...: mezcla 4.12 con entero"`) y conceptos
(`Scalar`, `SameExp`) son la herramienta: el error debe decir *qué* se mezcló.

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
- **F3 — Migración.** `math2d`, `math3d`, `object3d` y `lib3d` sobre la librería
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
