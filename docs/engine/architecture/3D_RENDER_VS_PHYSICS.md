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
| `ConcaveMesh` | Malla general | Culling + orden de pintor (lejos→cerca) | — |

Fundamento: en un poliedro **convexo**, tras descartar las caras traseras las visibles
**particionan la silueta y no se solapan** en proyección, así que el orden de dibujo es
indiferente y el sort (O(n log n)) es gasto puro. `mesh_painter_order` (pintor, el caso
general) se mantiene como envoltorio de `MeshFaceOrder<ConcaveMesh>`; `mesh_convex_order` es
el del sólido convexo. Añadir una cualidad nueva = especializar `mesh_order_traits<Kind>` y
`mesh_order_item<Kind, S>`.

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
