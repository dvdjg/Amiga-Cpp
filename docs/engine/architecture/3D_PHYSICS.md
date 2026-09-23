# Física 3D en el engine: opciones realistas y opción preferida

Este documento estudia cómo abordar **simulación física 3D** en un Amiga A500 (68000 a ~7 MHz,
sin FPU, sin heap durante gameplay) y fija la **opción preferida** dadas las restricciones. No
describe el soporte de render 3D, que vive en [3D_RENDER_VS_PHYSICS.md](3D_RENDER_VS_PHYSICS.md);
aquí se asume que el render ya existe (`PolyMeshView`, n-gon, flat-shade, culling) y se decide
qué hacer con la **simulación** que lo mueve.

El principio que gobierna todo el documento es que, en este hardware, el coste de la física no
está en la detección sino en el **solver** (resolver cada contacto iterando). Por eso la vía
realista no es un solver más listo, sino **reducir el número de contactos resueltos por frame** y
sustituir fuerzas por **transiciones de estado**.

## 1. Restricciones y presupuesto

```text
   CPU: 68000 a 7.09 MHz PAL -> ~140 000 ciclos por frame a 50 Hz
   sin FPU -> todo fixed-point o MiniFloat16
   sin heap -> capacidad fija por perfil (N20...N512), pools y arenas
   sin libgcc de 64 bits -> prohibido __mulsi3/__divsi3/__muldi3/__divdi3 en el bucle
```

Presupuesto razonable de física dentro del frame, dejando margen para render, juego y sistema:
**~30 000–60 000 ciclos/frame**. A ~20–40 ciclos por instrucción media en memoria, eso son del
orden de **1 500–3 000 instrucciones dinámicas de física por frame**: muy poco. Cualquier diseño
que itere mucho por contacto no cabe.

## 2. Qué ya existe y se reutiliza

| Pieza | Dónde | Uso en física |
|---|---|---|
| `Vec<N,S>`, `Mat<N,S>`, `Affine<N,SR,SL>` | `eng/core/math/linalg.hpp` | posiciones, velocidades, orientación, transform |
| `dot`, `cross2`, `length_sq`, `normalize`, `inverse_rigid` | `eng/core/math/geometry.hpp`, `gfx3d.hpp` | distancias, ejes, transform rígida inversa |
| `Fixed<Repr,Exp,Policy>` (q12, q8) | `eng/core/math/fixed.hpp` | escalar del bucle caliente |
| `MiniFloat16` + puente a fixed | `eng/core/math/minifloat.hpp`, `eng/retro/minifloat_fixed.hpp` | magnitudes de rango amplio (masa/inercia/orientación) |
| `convex_overlap` (SAT 2D), `circle_overlap`, `Aabb` | `eng/core/util/collision.hpp` | base del SAT 3D y de las pruebas baratas |
| `MeshView`/`PolyMeshView`, adaptador `obj2c` | `eng/core/data/mesh3d.hpp`, `object3d_poly.hpp` | geometría para el render y para masa/inercia offline |
| `convex_spans` | `eng/core/data/polygon.hpp` | relleno de las caras de contacto visual |
| `SpatialHash` | `eng/core/util/broadphase.hpp` | broadphase 2D a extender a 3D |

No hay que inventar el álgebra ni la malla: el trabajo nuevo es **colisión 3D, resolución y
propiedades de masa**.

## 3. Opciones realistas

Se ordenan de más barata a más ambiciosa. La columna «cabe» es por frame a 50 Hz.

### Opción A — Cinemática de estados (falsa pero creíble)

El mundo no tiene fuerzas: cada actor sigue un **estado** (`idle`, `falling`, `bouncing`,
`rolling`, `resting`) y las transiciones están guionizadas. El terreno es un **heightfield**
(`y = height(x,z)`, bilineal); el actor se pega al suelo. Un proyectil sigue una **parábola
precalculada** (tabla o `y = y0 + vy·t − g·t²` en fixed). El rebote es `v' = −e·v` con uno o dos
rebotes y un `settle` que clava en reposo.

- **Colisión**: AABB/esfera contra heightfield o contra «paredes» 2D.
- **Coste**: decenas de ciclos por actor.
- **Cabe**: cientos de actores.
- **Aplica a**: plataformas, carreras, cañones, caídas, cualquier juego donde el jugador solo ve
  el resultado y no la dinámica interna.
- **Límite**: no hay interacción entre cuerpos; no sirve para apilar ni para empujar.

### Opción B — Cuerpos rígidos «arcade» (un impulso por contacto)

Gravedad + integración semi-implícita de posición y velocidad en fixed. **Colisión solo de
formas baratas**: esfera-esfera, esfera-plano, caja-plano y **OBB-OBB por SAT** (cajas
orientadas). Broadphase con spatial hash 3D. Resolución con **un impulso por contacto** (sin
iterar, sin warm start), corrección posicional de Baumgarte ligera y fricción de Coulomb
saturada.

- **Coste**: ~1–3 k ciclos/frame con 4–8 cuerpos.
- **Cabe**: sí, con holgura.
- **Aplica a**: barriles, cajas, balones, escombros, un jugador-cápsula que empuja cosas.
- **Límite**: sin iterar, las **pilas vibran**; hay que evitar apilar y usar `sleep` para dormir
  cuerpos en reposo.

### Opción C — SAT 3D para convexos (el techo realista)

La extensión natural del `convex_overlap` 2D: en 3D los ejes son las **normales de cara** de cada
convexo más los **productos cruzados de aristas** (`E_a × E_b`). Da **eje y profundidad de
penetración** sin GJK, con `muls.w` y sin `sqrt`. La parte de contacto se resuelve como en B.

- **Caja-caja (OBB)**: 3 + 3 + 9 = **15 ejes**; factible y bien acotado.
- **Convexo-convexo arbitrario**: el número de ejes explota con los lados y deja de caber.
- **Coste medido** (6 ejes, un solo escalar, sin profundidad): ver §5.
- **Aplica a**: cajas orientadas, prismas, el jugador como OBB.
- **Límite**: para mallas convexas generales habría que ir a GJK+EPA, que **queda fuera**.

### Opción D — Poliedro visual con proxy de colisión

Se renderiza el poliedro (n-gon, flat-shade, ya implementado) pero se **colisiona con una
cápsula/esfera/OBB** que lo envuelve. La colisión y el render **no comparten malla**.

- **Coste**: el de B.
- **Aplica a**: cualquier objeto con forma vistosa cuya colisión no necesita ser exacta.
- **Valor**: es el truco que permite «poliedros» sin pagar SAT general.

### Opción E — Física en `eng::sim`

`eng::sim` es un mundo de **agentes 2.5D** (necesidades, biomas, LOD, terreno). No es un
integrador de cuerpos rígidos y no debe convertirse en uno: la física de cuerpos es una **capa
aparte** que el juego compone con `SimWorld`, no una parte de él.

## 4. Opción preferida

```text
   render      PolyMeshView (n-gon, flat-shade, culling)          <- ya existe
   colisión    heightfield + esferas + OBB (SAT-3D caja)          <- nuevo, pequeño
   resolución  1 impulso/contacto + gravedad, sin solver iterativo
   escalar     fixed (8.8 longitud / 4.12 ratio) en el bucle
   magnitudes  MF16 para masa/inercia/orientación (rango amplio)
   masa/inercia precalculadas en host, empaquetadas               <- fuera del runtime
   reposo      sleep de cuerpos quietos (clave para el presupuesto)
```

La elección es **B + C + D**: cuerpos rígidos arcade con colisión de esferas y OBB por SAT 3D,
malla visual independiente del proxy, y reposo agresivo. Es el mayor realismo que cabe, y evita
los dos sumideros conocidos (solver iterativo y GJK/EPA).

```text
   frame:
     broadphase (spatial hash 3D)            ~pocas decenas de ciclos
        │
        ▼
     narrow phase por par candidato
        esfera-esfera  ->  distancia^2 (sin sqrt)     <- el caso más común
        OBB-OBB        ->  SAT 6+9 ejes               <- solo cajas
        │
        ▼
     un impulso por contacto + corrección de posición
        │
        ▼
     integrar posiciones (semi-implícito)  ->  dormir si |v| y |ω| bajo umbral
```

### Por qué no GJK+EPA

GJK resuelve distancia/intersección de convexos arbitrarios y EPA la penetración, pero su coste y
su complejidad no se justifican sin un consumidor que necesite convexos generales. El SAT de OBB
cubre la forma que de verdad aparece (cajas), y el proxy cubre el resto. GJK+EPA queda como
trabajo futuro **condicionado a un caso de uso real**.

### Por qué no cuaterniones integrados

Integrar un cuaternión cada frame y normalizarlo cuesta `sqrt` y una división. Para B+C basta con
**matriz de rotación reortogonalizada** periódicamente o ángulos de Euler con reortogonalización;
el cuaternión solo si se necesita composición limpia de muchas rotaciones.

## 5. Estudio de coste medido

Kernels representativos compilados a `.s` para 68000, `-O2`, `-fomit-frame-pointer`, con
`Fixed<s16,8>` (8.8, longitud de mundo) salvo donde se indica. Instrucciones **estáticas** de la
función (el bucle desenrollado no aparece: son secuencias rectas).

| Kernel | Instr. | `mul` | `div` | Saltos | Llamadas | Nota |
|---|---:|---:|---:|---:|---:|---|
| `sphere_hit` (`dist² < (r₀+r₁)²`) | 35 | 4 | 0 | 0 | 0 | sin `sqrt` |
| `sphere_impulse` (proyección + 2 sumas) | 56 | 6 | 0 | 0 | 0 | |
| `integrate` (gravedad + posición) | 44 | 5 | 0 | 0 | 0 | semi-implícito |
| `resolve_ground` (clavar + rebote) | 23 | 0 | 1 | 4 | 0 | 1 división por rebote |
| `obb_sat` (6 ejes, un escalar) | 76 | 5 | 0 | 12 | 0 | 15 ejes ≈ ×2.5 |
| `sphere_hit` en **MiniFloat16** | 983 | — | — | 224 | 0 | ~28× el fixed |
| `mul_norm` en **Fixed<s32,16>** | — | — | — | — | **`__muldi3`** | inviable en m68k |

Lectura de los datos:

- **Esferas y plano son casi gratis**: 23–56 instrucciones, cero libcalls. Un mundo de esferas y
  un suelo cabe de sobra.
- **El SAT de caja es barato** (76 instr. con 6 ejes); los 9 ejes de producto cruzado lo suben a
  ~200 instrucciones por par, que sigue siendo asumible con **pocos pares** tras el broadphase.
- **`MiniFloat16` no es escalar de solver**: 983 instrucciones y 224 saltos por la misma prueba
  que en fixed cuesta 35. Sirve para magnitudes (masa inversa, inercia, orientación), no para el
  bucle de contacto.
- **El fixed de 32 bits no vale**: `Fixed<s32,16>` promueve el producto a 64 bits y emite
  `__muldi3`, prohibido. El mundo grande se maneja con **escala + rebasado** alrededor de la
  cámara, no ensanchando el escalar.

Herramientas para reproducir estas cifras: `tools/analyze/codegen-report.mjs` (sonda por
construcción, falla ante libcalls y 68020) y `tools/analyze/expr-asm-compare.mjs` (comparativa
entre formulaciones).

## 6. Escalar y rango

- **Longitud de mundo**: `Fixed<s16,8>` (8.8) da ±128 con paso 1/256, suficiente para un mundo de
  juego pequeño; `Fixed<s16,12>` (±8) se reserva a ratios (senos, escalas).
- **El producto de fixed sube de exponente**: `8.8 × 8.8 → 16.16` en `s32`; hay que **normalizar
  con `mul_norm`/`rescale`** y no encadenar productos sin normalizar (el acumulador `s32` se
  agota). Es el mismo modelo que `dot`/`transform` en `linalg.hpp`.
- **Rebasado**: para mundos mayores que ±128, mover el origen de coordenadas alrededor de la
  cámara y trabajar en local. Es la alternativa barata al fixed de 32 bits.
- **Masa e inercia**: `MF16` (rango amplio, valores ~1) o fixed con escala fija. La **masa
  inversa** (0 = estático) es un buen candidato a MF16 por su rango.
- **Tiempo**: `dt` fijo por frame (1/50 s) evita dividir; los integradores usan `dt` constante.

## 7. Hitos

| Hito | Entrega | Criterio de cierre |
|---|---|---|
| F1 | Propiedades de masa en **host** (`double`): volumen, centro, tensor de inercia de un `PolyMeshView` + test | volumen/inercia de la `pilka` y un cubo coinciden con la fórmula analítica (< 1 %) |
| F2 | Broadphase **3D** (spatial hash sobre `Vec<3,s16>`) + esfera-esfera + esfera-plano | N esferas caen, rebotan y se **duermen**; coste medido con `codegen-report` |
| F3 | OBB: SAT 3D caja-caja (15 ejes) + un impulso por contacto + corrección de posición | cajas apiladas de 2 en 2 estables; pilas de 3+ pueden vibrar (documentado) |
| F4 | Heightfield + actor controlado (jugador-cápsula) sobre el terreno | el jugador sube/baja pendientes y no atraviesa el suelo |
| F5 | Integración con render (`PolyMeshView`) y con el juego/demo | demo 3D con física visible, `build -> run -> analyze` OK |

Cada hito entra con su test host y, cuando toque hardware, con sonda de codegen sin libcalls ni
68020.

## 8. Qué queda fuera

- **Convexos generales por GJK+EPA**: condicionado a un caso de uso real.
- **Cuerpos blandos, telas, fluidos**: fuera del presupuesto.
- **CCD (detección continua)**: a 50 Hz y velocidades de juego no es necesaria; se acota la
  velocidad máxima para evitar el túnel.
- **Restricciones articuladas** (motores, bisagras, cadenas): fuera del primer alcance.
- **Física en `eng::sim`**: la capa de agentes no integra cuerpos rígidos.

## 9. Verificación

- **Coste**: cada kernel nuevo entra en `tools/analyze/codegen-report.mjs`; el gate falla ante
  `__mulsi3`/`__divsi3`/`__muldi3`/`__divdi3` o instrucciones 68020.
- **Corrección**: test host por pieza (propiedades de masa contra fórmula, SAT contra casos
  conocidos, conservación de energía en el rebote dentro de tolerancia).
- **Reposo**: test que comprueba que un cuerpo quieto se duerme y no consume simulación.
- **Evidencia en hardware**: demo con física visible validada por `build -> run -> analyze`.

Referencias: [3D_RENDER_VS_PHYSICS.md](3D_RENDER_VS_PHYSICS.md) (render y límites 16 bits),
[MATH_LIBRARY.md](MATH_LIBRARY.md) (escalar y álgebra), [SCALAR_LIBRARY.md](SCALAR_LIBRARY.md)
(selección de escalar), [MINIFLOAT16.md](MINIFLOAT16.md) (rango y precisión de MF16),
[OPTIMIZACION_GPP_68000.md](../../guides/optimization/OPTIMIZACION_GPP_68000.md) (codegen).
