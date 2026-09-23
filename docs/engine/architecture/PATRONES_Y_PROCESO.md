# Patrones y proceso del engine: cómo se hacen las cosas

Este documento **sintetiza la forma de trabajar** del engine (modelo de escalares, mallas,
raster Amiga y metodología) para que una persona o una IA puedan repetirla sin redescubrirla.
No duplica el contenido de las referencias canónicas: las enlaza. Cuando un detalle cambie,
se actualiza **su** documento y aquí solo se ajusta el resumen.

Índice de referencias canónicas:

- Regla de cabeceras y API: `PUBLIC_API.md`, `CODING_STYLE.md`.
- Escalares y álgebra: `SCALAR_LIBRARY.md`, `MATH_LIBRARY.md`.
- Mallas y render 3D: `3D_RENDER_VS_PHYSICS.md`.
- Rendimiento y codegen: `../guides/optimization/OPTIMIZACION_GPP_68000.md`.
- Tests y verificación: `../../testing/README.md`.
- Numeración de demos/tests: `../../ai-dev-environment/NUMBERING.md`.

---

## 1. La regla de oro de las cabeceras

**Una cabecera es lo más genérica posible.** El algoritmo se escribe una vez contra un
**punto de extensión** (un trait) y cada escalar/tipo concreto aporta su implementación en **su
propia cabecera**. Tipos concretos (`Fixed<s16,…>`, `q12`, `MiniFloat16`) solo pueden
aparecer en:

```
   retro/  platform/  cpu/  field/        (capa concreta)
   fixed*.hpp  minifloat*.hpp  scalar.hpp (el propio escalar)
```

Los **puntos de extensión** del engine, todos declarados en `eng/core/math/scalar_fwd.hpp`:

| Punto de extensión | Para qué | Ejemplo de especialización |
|---|---|---|
| `scalar_traits<S>` | cero/uno, `from_int`/`to_int`, `inner`, `norm_from` | `Fixed`, `float`, `MiniFloat16` |
| `numeric_traits<S>` | límites, promoción | `fixed.hpp`, `minifloat.hpp` |
| `scalar_div<S>` | división explícita (el núcleo prohíbe `/`) | saturante en fixed retro |
| `scalar_const<S>` | constantes decimales → escalar | `from(0.5)` |
| `noise_traits<S>` | ruido | `minifloat_math.hpp` |
| `scalar_sin/cos/sqrt/…<S>` | funciones matemáticas | `fixed_math.hpp`, `minifloat_math.hpp` |
| `mesh_traits<S>` | culling y clave de profundidad de una malla | `retro/fixed_mesh.hpp` |
| `mesh_order_traits<Kind>` | qué necesita el orden de caras de una cualidad | `ConvexSolid` |

**Enforcement automático**: `node tools/check/generic-headers.mjs` falla si una cabecera
genérica nombra un tipo concreto (`Fixed<…>`, `q0/q8/q12/q24`, `MiniFloat16`) o incluye el
soporte matemático (`fixed_math.hpp`/`minifloat_math.hpp`) o un backend (`retro/…`). Corre en
`tools/run-host-tests.sh` y `tools/test-regression.sh`.

**Cómo añadir un escalar nuevo** (patrón probado con `Fixed` y `MiniFloat16`):

1. Declara el tipo (idealmente un envoltorio de un `Repr`).
2. Especializa, **en su cabecera**, los puntos de extensión que necesite (`scalar_traits` como
   mínimo). El resto tiene primarios válidos.
3. Si su producto cambia de formato (fixed), define `inner`/`norm_from` para que el álgebra
   genérica normalice una sola vez.
4. Añade una **matriz de escalares** de test (ver §6) que compare contra `double`.

---

## 2. Operadores como tipos primitivos

Un escalar y los tipos de álgebra deben comportarse como un primitivo (sin `static_cast`
manuales ruidosos). Juego completo:

- **Escalar**: `+ - *` (y `/` si procede), `+= -= *=`, `== != < > <= >=`, unario `+`,
  `++`/`--` (pre/post). `Fixed` **no** define `/` ni `%` (el núcleo lo prohíbe): se divide con
  `div_norm`/`scalar_div`.
- **`Vec`**: `operator[]`, unario `+`, `+= -=`, `Vec*k`/`k*Vec` (`mul_norm`), `Vec/k`
  (`scalar_div`), `*= /=`, `== !=`.
- **`Mat`**: unario `+`, `+= -= *=`, `Mat*k`/`k*Mat`, `== !=`.

Detalle en `MATH_LIBRARY.md` §3.3.b y los tests HOST-048/135.

---

## 3. Álgebra lineal genérica

`Vec<N,S>`, `Mat<N,S>` y `Affine<N,SR,SL>` (ratio + longitud) son genéricos sobre el escalar.
Regla de normalización **fusionada**: los productos se acumulan exactos en el exponente doble y
se normalizan **una vez** (`dot`, `mul_norm`, `transform`). Nunca se multiplica y normaliza por
término. Restar/sumar escalares de distinto exponente es un error de compilación **con
mensaje** (no un «no matching function»); lo fija `tools/check/math-diagnostics.sh`.

---

## 4. Mallas 3D y el raster Amiga

El raster Amiga rellena **polígonos convexos**, no triángulos. Triangular una cara de 5-6 lados
(el balón `pilka`: 20 hexágonos + 12 pentágonos) multiplica los *fills* (32 caras → **116
triángulos**) y añade diagonales internas. Por eso hay dos representaciones:

| Representación | Cabecera | Caras |
|---|---|---|
| `MeshViewT<S>` | `mesh3d.hpp` | triángulos fijos (`Face{a,b,c}`) |
| `PolyMeshViewT<S>` | `mesh3d.hpp` | **n-gon** (`FaceSpan{first,count}` + índices concatenados) |

**Orden de caras elegido en compilación** (`MeshFaceOrder<Kind>`), sobre todo culling compartido:

| `Kind` | Cuándo | Qué hace |
|---|---|---|
| `ConvexSolid` | sólido convexo | solo culling (sin clave ni sort; `ConvexFace`, 2 B/cara) |
| `ConcaveMesh` | malla general (tri) | culling + pintor (lejos→cerca) |
| `ConvexPatches` | cóncava = unión de parches convexos (n-gon) | culling + pintor por parche |

Fundamento: en un **convexo**, tras descartar las traseras, las visibles **particionan la
silueta y no se solapan** → el sort es gasto puro. Medición (`codegen-report.mjs`): convexo
256 instr / 3 saltos vs cóncavo 323 / 6.

**Puentes entre representaciones** (adaptadores, sin heap):

- `eng/platform/amiga/object3d_poly.hpp`: `build_poly_mesh(obj, verts, offsets, indices,
  faces, out)` convierte un `Object3D` `obj2c` (los `FaceIndex` son **offsets de byte**) a un
  `PolyMeshView`. Validado en HOST-014 sobre la `pilka` real (60/32/180; el culling coincide
  con `lib3d`).

**Asset UAF-R** (`eng/assets/uaf.hpp`): chunk `Mesh` (triángulos) y `MeshPoly` (n-gon:
`{vertex_count,face_count,index_count}` + vértices + índices + `{first,count}`). El cocedor
`tools/assets/uaf-pack.ts` (`meshChunkData`/`polyMeshChunkData`) los escribe; `MeshAssetView`/
`PolyMeshAssetView` los leen (big-endian, host y Amiga). Conversión de un `obj2c` real a
`.uafr`: `node dist/tools/assets/pilka-to-uafr.js` (60 vértices, 32 caras, 116 triángulos).

---

## 5. El relleno (forma amiga)

- **`convex_spans`** (`eng/core/data/polygon.hpp`): spans `(y, xl, xr)` de un convexo por **dos
  cadenas** desde el vértice superior al inferior, **O(altura)** frente a O(lados·altura) del
  barrido por min/max. Validado contra la referencia en HOST-013.
- **`Playfield::draw_span`**: un tramo de 16 píxeles = **una palabra por plano** (extremos con
  máscara), ~16× menos RMW del bitmap que píxel a píxel. Lo usan `fill_polygon`, `fill_rect`,
  las líneas horizontales y las filas de glifo.
- Ruta hardware: `mask → contorno ONEDOT → area-fill (FILL_OR) → cookie-cut` (backend Amiga).

Detalle y coste en `OPTIMIZACION_GPP_68000.md` §13 y `3D_RENDER_VS_PHYSICS.md` §1.

### 5.b Copperlist: estructura fija + parcheo (`copper::Template`)

Coste real de una copperlist: en el A500 escribir en chip RAM durante el display de varios
planos cuesta **decenas de ciclos por palabra** (la CPU compite con el DMA de bitplanes).
Medido en la demo `125_layers_dualpf` (6 planos): **reconstruir** ~900 palabras por frame
cuesta ~76k ciclos y baja el frame de 1 a 1.5 campos (**49.9 → 32.5 fps**); **sin reconstruir**
vuelve a 1 campo. Es coste de bus, no de CPU (release ≈ debug) — por eso la compilación
estática / expression templates **no** lo resuelven por sí solos: reducen instrucciones, no
escrituras.

La palanca es **escribir menos palabras**: separar **estructura** (fija) de **datos** (por
frame) con `eng::copper::Template` (`copper/template.hpp`):

```cpp
copper::Template t {block};
u16 c = t.move_slot(Register::COLOR01, 0);   // slot de DATO
u16 w = t.wait_slot(0);                       // slot de la INSTRUCCION WAIT
t.end();
... por frame ...
t.set(c, color);        // 1 palabra
t.set_wait(w, line);    // 1 palabra (patch_wait: la linea vive en word0)
```

Límite: si la **estructura depende del frame** (p. ej. las líneas de banda de `layers` se
desplazan con el scroll), no se puede parchear sin reordenar slots; ahí o se reservan slots
fijos (y se parchea también el registro) o se reconstruye. `patch_wait` solo es seguro para
`vpos` 0..255 (por encima, el par de overflow debe recolocarse).

#### Modelo por clases de elemento

Una copperlist real es una mezcla; clasificar cada elemento decide **qué se escribe por frame**:

| Clase | Ejemplo | Mantenimiento por frame |
|---|---|---|
| **Estático** | waits en punto fijo, escrituras constantes (modo de display) | 0 (se construye una vez) |
| **Grupo en bloque** | grid de bandas que se desplaza entero | 1 palabra por WAIT (las líneas) |
| **Dato variable** | paleta por banda (mismo registro, valor distinto) | 1 palabra por MOVE (solo el dato) |
| **Registro variable** | slot que cambia de destino (bg↔fg según fase) | 1 palabra (solo el registro; raro) |
| **Dinámico total** | estructura que cambia de forma | reconstruir |

Coste óptimo = (#waits que se mueven) + (#datos que varían) + (#registros que cambian). Lo
estático es gratis. Un `copper::Template` recorre estos grupos con bucles contiguos.

**¿Expression templates?** Ayudan a generar la **estructura estática en compilación** (orden,
offsets de registro, init sin coste), pero **no** reducen el nº de palabras que varían por
frame: eso lo fija el dato. En este perfil (bus-bound) la ganancia es **no significativa**; el
win está en **clasificar** y **parchear lo mínimo**. Se implementa como runtime (`Template` +
slots), no como expresión estática.

---

## 6. Metodología: cómo se verifica (orden obligatorio)

**Toda API con test host; ninguna funcionalidad se afirma sin evidencia reproducible.** Orden:

```
   build  ->  run  ->  analyze        (por demo; lo impone tools/test-regression.sh)
```

1. **Test host por API** (C++ `g++` del entorno, sin emulador): `tests/host/<NNN>_<tema>/`.
   - La exactitud numérica se compara contra `double`.
   - La **matriz de escalares** (HOST-135) instancia los MISMOS algoritmos con varios
     anchos (host y `-DENG_SCALAR_RETRO16`) y mide error relativo.
   - Los tests de malla validan equivalencia de píxeles (`Surface`/`Playfield`) y de caras
     visibles (`math3d` vs `lib3d`).
   - Un asset `obj2c` de demo se puede incluir en un test para validar el adaptador sobre la
     malla real (HOST-014).
2. **Codegen 68000** (`node tools/analyze/codegen-report.mjs`): compila una sonda y reporta
   instrucciones, `muls.w`, saltos, `jsr` y libcalls por construcción. Es la medición
   reproducible de «qué genera g++». Vetado cualquier libcall de 32/64 bits o instrucción de
   68020. Comparar variantes (p. ej. orden convexo vs cóncavo) se hace añadiendo una sonda.
3. **Checks estáticos** (`tools/check/`): `encoding`, `links`, `test-numbering`,
   `demo-numbering`, `generic-headers`. Corren en la regresión.
4. **Demo Amiga + validación visual**: build m68k (`build-demo.sh`), run (`run-demo.sh`,
   captura por secuencia) y análisis visual con Ollama (`docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).
5. **Activos**: los generados van a `out/` (p. ej. `out/assets/mesh/pilka.uafr`); nada binario
   en git.

**Numeración y ramas**: demos y tests usan **bloques reservados por rama** para no solapar
(`docs/ai-dev-environment/NUMBERING.md`). Antes de crear un test/demo, reservar el número.

**Commits**: por defecto, al **abrir** un turno se commitea lo que quedó del anterior; el
trabajo del turno en curso se deja sin commitear salvo petición (`AGENTS.md` §1.8). Mensaje
conciso, sin metainformación de proceso.

---

## 7. Checklist para añadir una funcionalidad

1. ¿Existe ya algo equivalente? (buscar en `engine/`, `demos/`, `tools/`, `docs/`). Reutilizar
   o subir al engine, nunca duplicar.
2. ¿La cabecera es genérica? El algoritmo contra un **punto de extensión**; lo concreto en su
   cabecera de escalar/backend. Pasa el gate `generic-headers`.
3. Escribir el **test host primero** (criterio de aceptación) y, si toca números, la
   **matriz de escalares**.
4. Si hay ruta caliente: **sonda de codegen** y comparar instrucciones vs la alternativa.
5. Actualizar el **documento canónico** (no narrar la evolución) y, si aplica, el índice
   (`DOC-MAP-PRINCIPAL.md`) y la referencias cruzadas.
6. `bash tools/run-host-tests.sh` (incluye codegen + checks) en verde; build m68k de una demo
   que use el cambio.
