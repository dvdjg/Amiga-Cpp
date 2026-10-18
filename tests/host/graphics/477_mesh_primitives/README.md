# HOST-477: primitivas 3D generadas en compilacion (`eng::retro::mesh_primitives`)

Test host de los generadores **constexpr** de mallas de `engine/include/eng/retro/mesh_primitives.hpp`: caja, prisma regular, tubo abierto, cono/pirámide, esfera UV, cápsula y sólidos platónicos (tetraedro, cubo, octaedro, dodecaedro, icosaedro). Las mallas usan el formato del engine (`math3d::PolyMeshViewT`: vértices compartidos + índices concatenados por cara con `FaceSpan`, caras n-gon convexas) y quedan materializadas como datos en el binario (cero coste de runtime).

## Qué comprueba

1. **Generación en compilación**: las once mallas de prueba son `constexpr` y sus contadores exactos de vértices, índices y caras se fijan con `static_assert` (si la generación no fuese constexpr, no compila).
2. **Topología cerrada**: para cada sólido, `V - E + F = 2` contando aristas únicas a partir de las caras (caja, prisma, cono, esfera, cápsula y los cinco platónicos). El tubo es abierto (sin tapas) y no pasa por Euler.
3. **Winding hacia fuera**: ninguna cara del sólido es visible (`math3d::face_visible`, que decide con los tres primeros puntos de la cara, igual que el culling del render) desde el centro del sólido. La corrección del generador usa el mismo criterio (`outward_ring`), de modo que en quads no planos (esfera/cápsula) el primer triángulo de la cara es el que manda.
4. **Integridad**: índices dentro de rango y caras sin vértices repetidos.
5. **Capacidad**: un prisma de 20 lados cabe en un buffer de su talla y **no** cabe (devuelve `false` sin desbordar) en uno de 8; parámetros inválidos (2 lados, 1 stack, escala 0) se rechazan.
6. **Dos escalares**: la cabecera no impone el escalar — la misma caja y el mismo icosaedro se generan con `coord` (q0 s16) y con `float`, contrastando contadores y coordenadas.

## Salida de referencia

```
OK: primitivas 3D (caja, prisma, tubo, cono, esfera, capsula y platonicos) en compilacion
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/477_mesh_primitives
```

## Notas

- Los generadores escriben en un `MeshData<S, MaxV, MaxI, MaxF>` del llamador (buffers de capacidad fija, sin heap) y `view()` entrega la vista para el render (`mesh_patches_order_lit`, `fill_polygons_by_plane`, pictórico por z…).
- El dodecaedro se construye como dual del icosaedro (vértices = centroides de las caras del icosaedro, caras = las cinco caras incidentes en cada vértice ordenadas por aristas compartidas): no depende de tablas de caras escritas a mano.
- φ se aproxima con cocientes enteros (26/16 y 10/16): icosaedro y dodecaedro quedan ligeramente irregulares, suficiente para geometría de demo.
