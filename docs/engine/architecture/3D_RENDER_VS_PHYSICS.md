# 3D en el engine: modelo y render frente a física

Este documento fija qué soporte 3D existe, para qué sirve y qué **no** hay, de modo que no
se confunda la canalización de **render** con una capa de **colisión/física** (que no existe).
El objetivo es un A500 (68000, sin FPU, poca RAM), así que todo es entero/fixed y sin heap.

## 1. Lo que sí hay (modelo y render)

```
   modelo / transformación            proyección / visibilidad           relleno
   ────────────────────────           ──────────────────────────         ─────────
   linalg: Vec/Mat/Affine    ──►      gfx3d (math3d): load_rotate ──►   polygon_fill
   (+ compose, inverse_rigid)         light: face_signed_area /        (CPU o Blitter)
   mesh3d: MeshView (V+F)             face_visible (back-face cull)
   object3d/lib3d (mesh obj)          mesh3d: MeshFaceOrder<Kind>
```

| Pieza | Dónde | Qué aporta |
|---|---|---|
| Aritmética linear genérica | `eng/core/linalg.hpp` | `Vec<N,S>`, `Mat<N,S>`, `Affine<N,SR,SL>`; `transform`, `compose`, `dot`, `cross`, `inverse_rigid` |
| 3D fijo retro (`math3d`) | `eng/platform/amiga/gfx3d.hpp` | `Mat3 = Mat<3,q12>`, `Affine3 = Affine<3,q12,q0>`, `P3 = Vec<3,q0>`, `load_rotate`/`load_reverse_rotate`, `scale` |
| Malla (render) | `eng/core/mesh3d.hpp` | `MeshView` (vértices + caras triangulares), `Vec3`, `mesh_transform`, culling y orden de caras (`MeshFaceOrder<Kind>`: convexo o cóncavo) |
| Objeto empaquetado (lib3d) | `eng/platform/amiga/object3d.hpp`, `lib3d.hpp` | `Object3D`, `objectToWorld`/`worldToObject`, transform+proyección+visibilidad de un mesh tipo `obj2c` |
| Sombreado / relleno | `eng/core/light.hpp`, `eng/platform/amiga/polygon_fill.hpp` | Sombreado por cara; relleno de polígonos por CPU o Blitter |
| Escalares | `eng/core/fixed.hpp`, `fixed_math.hpp` | `Fixed`, `q12` (4.12), `q24`; sin `float`, con `muls.w`/`divs.w` |

Evidencia: demos `077_math3d_cube` (alambre), `078_math3d_solid`, `079`, `116_flatshade_convex`
y tests HOST-011/013/014/047/050/051/053/055.

### 1.1 Orden de caras según la topología (elegido en compilación)

El orden de caras se elige con una **cualidad de compilación** (`MeshFaceOrder<Kind>`), de modo
que cada caso genera su versión optimizada y ambos comparten el bucle de culling:

| `Kind` | Cuándo | Qué hace | Qué se ahorra |
|---|---|---|---|
| `ConvexSolid` | Sólido convexo (p. ej. la `pilka`) | Sólo back-face culling | Clave de profundidad, shell sort; la lista es `ConvexFace` (2 B/cara) |
| `ConcaveMesh` | Malla general (triángulos) | Culling + orden de pintor (lejos→cerca) | — |
| `ConvexPatches` | Malla cóncava descompuesta en parches convexos (n-gon) | Culling + pintor por parche | — (relleno convexo por parche) |

Fundamento: en un poliedro **convexo**, tras descartar las caras traseras las visibles
**particionan la silueta y no se solapan** en proyección, así que el orden de dibujo es
indiferente y el sort (O(n log n)) es gasto puro. `mesh_painter_order` (pintor, el caso
general) se mantiene como envoltorio de `MeshFaceOrder<ConcaveMesh>`; `mesh_convex_order` es
el del sólido convexo. Añadir una cualidad nueva = especializar `mesh_order_traits<Kind>` y
`mesh_order_item<Kind, S>`.

Medición (sondas `c_math3d_order_convex`/`c_math3d_order_concave` de
`tools/analyze/codegen-report.mjs`, malla de 12 triángulos): convexo **256 instrucciones / 3
saltos** frente a cóncavo **323 / 6**, mismo `muls.w` y sin libcalls. El ahorro es la clave de
profundidad y el shell sort que el caso convexo no genera.

### 1.2 Caras n-gon y relleno convexo (amigable con el raster)

El raster Amiga rellena **polígonos convexos**, no triángulos, así que triangular una cara de
5-6 lados (la `pilka`: 20 hexágonos + 12 pentágonos) multiplica los *fills* y añade diagonales
internas. Para evitarlo:

- **`PolyMeshViewT<S>`** (`mesh3d.hpp`): malla con caras de longitud variable (`FaceSpan` +
  array plano de índices); el triángulo es `count == 3`. El culling de una cara n-gon usa la
  normal de **Newell** (`poly_face_visible`), válida también para caras no convexas, y la clave
  de orden es el mínimo z de la cara. `mesh_patches_order` ordena los parches convexos con el
  mismo `MeshFaceOrder<ConvexPatches>` (pintor por parche).

  **Límite de rango y ruta retro:** Newell acumula en el exponente ancho del escalar (para
  `Fixed<s16,0>` el último producto pasa a 64 bits → `__muldi3`, inviable en 68000). Por eso,
  para el camino retro, la vista lleva **normal por cara** (`PolyMeshViewT::normals`, la del
  `obj2c`) y se ordena con `mesh_patches_order_lit` (`ConvexPatchesLit`): el cull es
  `signo(n·(cam-p0))` con `n` y `cam-p0` del mismo tipo → producto `s32`, **sin 64 bits** y
  coincidente con `lib3d`. La sonda `c_math3d_poly_cull` lo fija (162 instr, 3 `muls.w`, 0
  libcalls). La demo 116, ruta B (`-DFLATSHADE_FAITHFUL=0`), rellena los parches en ese orden
  con `blitter_fill_polygon` (validado visualmente: la `pilka` sale con caras n-gon sólidas).
- **`convex_spans`** (`eng/core/polygon.hpp`): genera los spans `(y, xl, xr)` de un polígono
  convexo por **dos cadenas** (izquierda/derecha desde el vértice superior al inferior),
  O(altura) frente a O(lados·altura) del barrido por mínimo/máximo. Es el generador de spans
  que usa el relleno CPU (`Playfield::fill_polygon`), con un `emit` que escribe el span
  (píxel a píxel, un `write_span` o un blit).
- **`Playfield::draw_span`**: escribe un tramo horizontal con **una palabra por plano** (16
  píxeles) en vez de píxel a píxel; `fill_polygon` lo usa con `convex_spans`.
- **Adaptador `obj2c` -> `PolyMeshView`** (`eng/platform/amiga/object3d_poly.hpp`): extrae
  vértices y caras **n-gon** de un `Object3D` (los `FaceIndex` son offsets de byte; se mapean
  a índice de vértice) a buffers del llamador, listo para `mesh_patches_order`.
- **Chunk UAF-R `MeshPoly`** (`eng/assets/uaf.hpp` + cocedor `polyMeshChunkData`): malla con
  caras de longitud variable (`{vertex_count, face_count, index_count}` + vértices + índices
  concatenados + `{first,count}`), junto al `Mesh` triangular existente. `PolyMeshAssetView`
  lo decodifica big-endian (host y Amiga) y lo copia a un `PolyMeshView`.

## 2. Lo que NO hay (colisión/física 3D)

- **Colisión 3D**: no hay broadphase 3D (el `SpatialHash` de `eng/core/util/broadphase.hpp` es
  2D), ni narrow phase de sólidos convexos, ni detección de contacto/penetración.
- **Volumen/propiedades de masa**: no hay integración de volumen ni centro de masas.
- **Mallas convexas / hull / BVH**: no existen utilidades de convex hull ni jerarquías de
  volúmenes envolventes.
- **Jerarquía de escena**: `Affine` compone (`compose`), pero no hay grafo de nodos
  padre/hijo con transformaciones en cascada; cada `Object3D` lleva su propia `objectToWorld`.

La colisión que sí existe es **2D**: `eng/core/util/collision.hpp` (AABB, segmentos,
triángulo, círculo y, desde HOST-125, **SAT 2D** de polígonos convexos).

## 3. GJK/EPA y por qué no están

`GJK` calcula distancia/intersección entre conjuntos **convexos en cualquier dimensión** (2D y
3D). En 2D se prefiere **SAT** (más simple, más barato y da el eje de penetración directo), que
es lo implementado. La **penetración** en 3D requiere `EPA` sobre GJK, y el **volumen** es otro
algoritmo (integración por tetraedros, p. ej. Mirtich), no GJK. Ninguno de los dos está en el
engine porque **no hay consumidor 3D físico**.

## 4. Límites de los 16 bits

- `Affine3` es ratio 4.12 (±8) + traslación `s16`; cadenas largas de composición acumulan
  redondeo y el rango se agota rápido. Un mundo 3D grande exigiría escalas/rebasado.
- Las diferencias de coordenadas que entran en productos cruzados deben caber en `s16`
  (~±16000); los productos usan `mul_wide` (`muls.w`) para no caer en `__mulsi3`.

## 5. Si algún día se aborda física 3D (fuera de alcance hoy)

Camino mínimo, reutilizando lo existente: **spatial hash 3D** sobre `Vec<3,s16>` + **SAT 3D**
(para cajas/convexos) o **GJK+EPA** (convexos arbitrarios) + integración de volumen si hiciera
falta; y una **jerarquía de transforms** (`compose` en cascada) para la composición de objetos.
No es prioritario en A500 por presupuesto de RAM/ciclos.
