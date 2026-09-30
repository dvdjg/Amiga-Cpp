# Formato de assets 3D para Amiga

Especificación de autoría y de runtime para modelos 3D destinados al engine Amiga. El formato parte
de una premisa: el asset debe describir la **geometría que el raster planar puede rellenar**, no
forzar un pipeline de triángulos/GPU. La fuente editable es JSON; el runtime consume chunks UAF-R
big-endian, validados y cuantizados para 68000.

## Objetivos

- Representar poliedros low-poly mediante triángulos o caras convexas n-gon.
- Asignar por cara material, índice de paleta y patrón de trama/dither.
- Cocinar winding, normales, convexidad, bounds y agrupación por material en el host.
- Admitir jerarquías de partes rígidas, rigging opcional, keyframes de nodos y animación de vértices.
- Usar posiciones/interpolaciones fixed-point en runtime; floats solo son válidos en el JSON y en
  las herramientas host.
- Mantener buffers compactos, offsets relativos, capacidad fija y sin heap durante gameplay.
- Permitir perfiles simples (malla estática) sin cargar ni ejecutar el código de animación/skinning.

## Relación con formatos existentes

`UAF-R` ya define el contenedor de chunks big-endian y los chunks `Mesh=12` (triángulos) y
`MeshPoly=14` (n-gon). Son válidos como bases geométricas sencillas, pero no describen de forma
completa materiales por cara, tramas, nodos, skeleton, clips ni deltas de vértice. Esta propuesta
añade chunks de modelo 3D; no cambia los chunks existentes ni interpreta un `MeshPoly` antiguo como
un modelo animado.

El modelo de salida respeta el raster Amiga:

```text
cara visible
  → material = índice de paleta + patrón opcional
  → convex n-gon (triángulo permitido como count=3)
  → clipping / painter order
  → spans CPU o fill/line de Blitter
  → bitplanes
```

No se almacena iluminación por píxel, textura RGB ni material PBR. La iluminación artística se
hornea a índices de paleta/tramas por cara o por pose/material variant.

## JSON de autoría

El JSON es una fuente para herramientas, no un formato de carga directa en Amiga. Puede usar números
decimales, nombres y referencias; el cooker valida, normaliza, convierte a fixed y produce UAF-R.

```json
{
  "format": "amiga-model-source",
  "version": 1,
  "coordinateSystem": {
    "handedness": "right",
    "up": "y",
    "forward": "+z",
    "unitsPerMeter": 16
  },
  "palette": { "uafChunk": 0, "colors": 16 },
  "patterns": [
    {
      "id": "shade-50",
      "width": 16,
      "height": 4,
      "bits": ["FFFF", "AAAA", "FFFF", "5555"],
      "anchor": "object"
    }
  ],
  "materials": [
    { "id": "body-light", "color": 6, "pattern": null, "flags": ["opaque"] },
    { "id": "body-shadow", "color": 3, "pattern": "shade-50", "flags": ["opaque"] }
  ],
  "meshes": [
    {
      "id": "guard",
      "vertices": [[-8, 0, 0], [8, 0, 0], [8, 24, 0], [-8, 24, 0]],
      "faces": [
        { "vertices": [0, 1, 2, 3], "material": "body-light", "normal": [0, 0, -1], "convex": true }
      ],
      "bounds": { "min": [-8, 0, -2], "max": [8, 24, 2] }
    }
  ],
  "nodes": [
    { "id": "root", "parent": null, "mesh": null, "pivot": [0, 0, 0] },
    { "id": "torso", "parent": "root", "mesh": "guard", "pivot": [0, 12, 0] }
  ],
  "skeleton": {
    "bones": [{ "id": "root", "parent": null }, { "id": "torso", "parent": "root" }],
    "vertexInfluences": "rigid-bone"
  },
  "clips": [
    {
      "id": "turn",
      "durationTicks": 12,
      "loop": false,
      "tracks": [
        {
          "target": "torso",
          "kind": "trs",
          "interpolation": "linear-shortest-turns",
          "keys": [
            { "tick": 0, "translation": [0, 0, 0], "rotationTurns": [0, 0, 0], "scale": [1, 1, 1] },
            { "tick": 12, "translation": [0, 0, 0], "rotationTurns": [0, 0.25, 0], "scale": [1, 1, 1] }
          ]
        }
      ]
    }
  ],
  "colliders": [
    {
      "id": "guard-body",
      "target": "torso",
      "shape": "sphereSet",
      "layer": 1,
      "mask": 65535,
      "response": "solid",
      "spheres": [
        { "center": [0, 6, 0], "radius": 6, "bone": "torso" },
        { "center": [0, 17, 0], "radius": 5, "bone": "torso" }
      ]
    }
  ]
}
```

`rotationTurns` son fracciones de vuelta en fuente (`0.25` = 90°); el cooker las convierte a la
representación entera. Los IDs son nombres de autoría y no se guardan como strings en el runtime:
se convierten a índices compactos. El JSON permite omitir `skeleton`, `clips` y `colliders`; una malla
estática no paga por esos sistemas.

## Contrato geométrico

- Los índices de vértice son locales a cada mesh; las caras referencian un material por índice.
- Las caras de salida deben ser convexas. El cooker triangula o descompone las cóncavas en parches
  convexos y mantiene un `sourceFace` si el juego necesita selección/daño por cara original.
- Un n-gon planar convexo conserva convexidad bajo proyección perspectiva mientras todos sus puntos
  están delante del near plane; el clipping de cámara puede añadir vértices y debe comprobar que el
  polígono resultante sigue siendo válido y cabe en el scratch configurado. Caras no planares,
  auto-intersectantes o degeneradas se triangulan/particionan o se rechazan en el cooker.
- El winding debe ser coherente por mesh. El cooker valida degenerados, índices, tamaño máximo y
  winding; puede normalizar orientación y marcar `doubleSided` cuando la fuente lo exige.
- Se cocinan normal por cara, bounds de mesh/objeto/nodo y límites de cada clip para culling.
- Los vértices pueden compartirse entre caras; los índices de cara se aplanan para lectura secuencial.
- El pivot/origen se declara por nodo, no se corrige moviendo los píxeles del bitmap/sprite asociado.
- Los colliders son proxies de gameplay independientes de las caras visuales. Shapes iniciales:
  AABB, circle/sphere en 2D/3D, `sphereSet`, capsule, segment y convex polygon de pocos vértices.

### Colecciones de esferas

`shape: "sphereSet"` describe un único collider compuesto formado por una lista acotada de esferas
en coordenadas locales. El set tiene un transform propietario (nodo/body); cada esfera contiene
`center`, `radius` y una asociación opcional a bone/nodo rígido. Las esferas no son bodies ni actores
independientes: comparten masa, velocidad, respuesta, capa/máscara y material del collider padre.

Uso recomendado:

- aproximar cabeza/torso/extremidades de un actor o partes de un objeto irregular;
- asociar esferas a bones de un rig rígido sin skinning de vértices;
- definir zonas vulnerables como sub-id opcional de esfera si el gameplay lo necesita;
- dejar cajas como AABB/OBB y superficies largas como caja/segmento cuando eso requiera menos
  primitivas.

El cooker calcula un AABB compuesto envolvente y valida radio positivo, coordenadas, cantidad,
bone indices y rango fixed. El perfil del juego define `max_spheres_per_set`; un valor pequeño
(p. ej. 4–8) es el perfil normal de A500, y los assets que lo excedan se simplifican en host o se
rechazan. La cantidad máxima real se fija por presupuesto de gameplay, no por un límite global
arbitrario del formato.

Para test booleano entre dos sets, broadphase compara primero sus AABB envolventes y narrow phase
recorre pares de esferas candidatos con `distance_sq <= (ra + rb)^2`, sin raíz cuadrada. Si se
requiere resolver impulso, la normal/contacto debe calcularse solo para los pares que colisionan;
para triggers basta registrar overlap y sub-id. El runtime puede detener el recorrido al primer hit
si la consulta solo pregunta si hay colisión; para contactos múltiples utiliza un buffer fijo y
reporta overflow. No se debe hacer producto cartesiano entre todos los colliders del mundo: primero
filtrar entidades con broadphase y collision layer/mask.

En UAF-R, un `Collider3D` puede guardar cada set con `{target_node, layer, mask, response,
sphere_first, sphere_count, local_bounds}` y un array contiguo de esferas `{center_x_q8_8,
center_y_q8_8, center_z_q8_8, radius_q8_8, bone_index, flags}`. `bone_index = 0xffff` significa que
el centro está en el espacio local del collider; no se guarda un puntero ni se crea un body por
esfera.

## Materiales de paleta y entramado

Un material de cara contiene un índice lógico de paleta y un estilo de cobertura:

```text
FaceMaterial = paletteIndex + patternIndex + flags
flags = opaque | doubleSided | noCull | emissive-flat | collisionSurface
```

- `paletteIndex` indexa una paleta UAF-R asociada al asset/escena; no almacena RGB por cara.
- `patternIndex = none` representa relleno sólido.
- Un `RasterPattern` es un tile pequeño de bits (preferiblemente ancho 16 y altura pequeña), con
  foreground/background de índices de paleta y anclaje `object`, `face` o `screen`.
- Las tramas estables por objeto son recomendadas. El anclaje a pantalla evita que el patrón
  parpadee al moverse la cámara, pero puede requerir más trabajo de clipping/fill.
- Los patrones lineales para wireframe pueden usar un `u16` cíclico, fase y estilo OR/EOR; no deben
  confundirse con el patrón 2D de relleno de cara.
- El asset conserva el patrón semántico. El renderer elige CPU spans, `PatternFill`, máscara planar o
  precomposición a un bitmap pequeño según capacidad y tamaño. No se presupone que `Surface` ya
  admita polígonos texturados: el relleno poligonal con patrón sigue siendo una extensión del seam.

No almacenar directamente una máscara expandida a 4/5/6 bitplanes por cada material si el patrón
compacto basta. El cooker puede opcionalmente generar variantes preintercaladas o máscaras cacheadas
cuando el perfil de juego pruebe que compensan la memoria Chip ocupada.

## Rigging y animación

El formato admite tres niveles, sin obligar a los tres en cada asset:

1. **Transform de objeto/nodo**: keyframes de translation/rotation/scale por nodo para partes
   rígidas. Es el modo recomendado en A500.
2. **Rig rígido**: cada vértice pertenece a un único bone (índice `u8`), o cada mesh se enlaza a un
   nodo/bone. Evita skinning por vértice y permite animar robots/personajes de piezas.
3. **Deformación de vértice opcional**: morph targets/deltas sparse por vértice para expresiones,
   impactos o animación muy limitada. El cooker limita el número de targets/keyframes y el runtime
   procesa solo vértices visibles/afectados.

La fuente JSON usa TRS. El formato cocinado almacena:

| Canal | Representación de runtime recomendada | Motivo |
|---|---|---|
| Posición/key translation local | `s16 Q8.8` por eje, con origen de mesh/pivot y escala de asset | subunidad para movimiento suave; ±128 unidades por componente antes de rebasar |
| Escala | `s16 Q4.12` o escala entera uniforme cuando basta | rango y ratios acordes al álgebra existente |
| Rotación primaria | 3 × `u16 Turns` (vueltas modulo 1) | interpolación shortest-arc con enteros; evita floats y normalización quaternion |
| Normal de cara | 3 × `s16 Q4.12`, normalizada por cooker | shading flat y culling rápido |
| Delta de vértice | `s16 Q8.8` sparse, solo si se usa morph/vertex animation | almacena desplazamiento local con resolución subpixel |

El runtime debe usar fixed, sin float. El JSON puede contener decimales porque solo lo procesa el
host. Guardar directamente los `s16` finales en el JSON acoplaría la autoría a un exponente y haría
difícil editar unidades, pivots o escalas; el cooker debe cuantizar y validar rango, error máximo y
overflow, y el asset cocinado debe declarar el codec/escala usados.

### Rotación: decisión y extensibilidad

La opción base es Euler en unidades `Turns` con orden declarado por modelo (por defecto XYZ) y
interpolación shortest-arc por eje. Es compacta, barata y compatible con tablas trigonométricas del
68000; sus limitaciones son gimbal lock y posible diferencia de trayectoria para rotaciones grandes.
Para el perfil inicial se recomienda animación por piezas rígidas y evitar giros que crucen
configuraciones singulares.

El formato reserva `rotationCodec` para añadir quaternion Q1.14. Si se habilita, usar nlerp con
corrección de signo y normalización aproximada en keyframes/setup o mediante tabla; no ejecutar una
normalización con raíz/división por vértice. Matrices 3×3 no son el codec recomendado para tracks:
ocupan más, interpolarlas elemento a elemento pierde ortogonalidad y exige reortogonalizar. Matrices
afines sí pueden ser una representación **derivada/cacheada** por nodo en el frame.

Los keyframes son sparsos por canal y se almacenan ordenados por tick. Interpolaciones:

- `step`: cambios de estado/material/visibilidad.
- `linear`: posición, escala y morph deltas.
- `linear-shortest-turns`: Euler modular.
- `nlerp-quat`: codec opcional para rotaciones que no convienen en Euler.

Un clip contiene duración en ticks fijos, loop/one-shot, tracks, eventos de gameplay opcionales,
bounds y root motion explícito. Los eventos no deben ejecutarse desde una IRQ.

## Formato cocinado UAF-R

El JSON de autoría se cocina a chunks UAF-R big-endian. Propuesta de tipos nuevos, reservando IDs
libres del enum actual:

| Chunk | Contenido |
|---|---|
| `Model3D = 15` | header/version/codec, meshes, vertices fixed, índices, caras convexas, submeshes, nodos, pivots y bounds |
| `Material3D = 16` | índice de paleta, patrón, flags y propiedades flat por material |
| `RasterPattern = 17` | patrón 1-bit pequeño, dimensiones, filas y colores foreground/background |
| `Animation3D = 18` | skeleton, clips, keyframes TRS y eventos |
| `Collider3D = 19` | proxies por nodo: AABB/OBB, esfera, colección de esferas, cápsula, segmento y flags |

La elección entre un chunk compuesto o chunks separados se fija en el cooker: para el primer
runtime se recomienda `Model3D` con geometría/nodos/bounds y chunks separados para `Material3D`,
`RasterPattern` y `Animation3D`, de forma que una malla estática no tenga que cargar animación ni
tramas no usadas. Las referencias internas son índices de chunk e índices de tabla, nunca punteros.

Subcabecera de `Model3D` propuesta:

```text
u16 schema_version
u16 flags
u8  position_codec       // S16_Q8_8, S16_INTEGER, reserved
u8  rotation_codec       // TURN_EULER, QUAT_Q1_14, reserved
u8  coordinate_flags     // handedness/up/forward convention ID
u8  reserved
u16 mesh_count
u16 node_count
u16 vertex_count
u16 face_count
u16 index_count
u32 mesh_table_offset
u32 node_table_offset
u32 vertex_data_offset
u32 index_data_offset
u32 face_data_offset
```

Cada offset es relativo al comienzo del payload y se valida con arithmetic de overflow antes de
formar vistas. Tablas alineadas a 2/4 bytes; campos big-endian; sin padding implícito de structs C++.
El cooker fija máximos por perfil (vértices/caras/índices, huesos, keyframes y patrón) y rechaza una
asset fuera de límites en vez de truncarla en el target.

El patrón puede almacenarse comprimido como `width/height + rows u16`. El runtime no necesita un
descriptor general de textura ni UVs RGB. Si después se añade textura indexada, debe ser otro codec
optativo con presupuesto explícito de Chip RAM, no una propiedad implícita de todo material.

## Pipeline host→Amiga

```text
JSON/modelo fuente
  → validar IDs, índices, winding, convexidad y jerarquía
  → convertir caras cóncavas a convex patches
  → calcular normals, bounds, pivots, proxies y tablas de material
  → cuantizar vértices y keyframes a codecs fixed declarados
  → optimizar tracks: eliminar keys redundantes y separar clips/canales
  → generar chunks UAF-R + CRC/round-trip
  → cargar chunks requeridos en memoria según perfil de asset
  → montar vistas tipadas sin copiar en cada frame
```

El host valida el round-trip: decodificar el asset cocinado, comparar bounds/topología/materials y
simular cuantización de poses. Debe reportar error máximo de posición/rotación, rangos saturados,
caras partidas, patrones/índices fuera de paleta y bytes por chunk. El Amiga valida offsets, versión,
conteos y CRC antes de publicar vistas.

## Optimización para el raster Amiga

- Agrupar caras por material/patrón si esto no viola painter order; separar el orden de render del
  orden de almacenamiento en el asset.
- Precalcular normals, convex patches, bounds, proxies, índices de aristas y variantes LOD en host.
- Usar backface culling solo cuando winding, material opaque y sidedness lo permitan.
- Omitir culling de viewport solo para assets/perfiles con bounds comprobados dentro de cámara fija;
  near clipping no se omite si una cara puede cruzar el plano cercano.
- Para `ConvexSolid`, omitir sort de caras visibles porque no se solapan tras culling; para mesh
  cóncava mantener painter order o dividir en parches cuyo orden esté precocinado.
- Dibujar wireframe desde una lista de aristas únicas para no emitir tres líneas por cara y duplicar
  aristas compartidas.
- Elegir CPU spans para polígonos pequeños; evaluar Blitter para fills grandes y agrupados. La
  geometría del asset no presupone que cada cara produzca un job individual.
- Materiales patrón se anclan al objeto/cara/screen con elección explícita; el host puede generar
  índices por intensidad de luz flat, pero el runtime no calcula focos ni iluminación por píxel.
- Skinning permanece desactivado en el perfil base; rig rígido por nodo no transforma vértices.
- Cambiar LOD por mesh/patch completo en distancia/histeresis, no decimar caras durante el frame.

## Criterios de aceptación del formato

1. Un JSON puede describir malla estática n-gon, material de paleta y patrón, y cocinarse a un UAF-R
   que el host decodifica sin pérdida de topología/material.
2. El cooker rechaza caras cóncavas no particionadas, índices inválidos, polígonos degenerados y
   cuantización fuera de rango.
3. Un clip de partes rígidas interpola posición/escala y ángulos Turns sin float en Amiga y coincide
   con la referencia host dentro del error declarado.
4. Un asset sin skeleton/clip/pattern no contiene tablas ficticias ni requiere reservar esos
   subsistemas.
5. Un caso n-gon rasteriza por convex spans/Blitter sin triangulación forzada; la equivalencia de
   patrones se compara contra CPU.
6. El parser runtime valida límites, alineación, versión, índices de chunk y conteos antes de
   exponer vistas.
7. El pipeline incluye presupuesto de bytes Chip/Fast y variante/profile LOD calculados por host.

## Referencias

- [`3D_GAME_ARCHITECTURE.md`](3D_GAME_ARCHITECTURE.md)
- [`3D_RENDER_VS_PHYSICS.md`](3D_RENDER_VS_PHYSICS.md)
- [`OBJECT3D_MESH_VIEW.md`](OBJECT3D_MESH_VIEW.md)
- [`WORLD_FORMAT.md`](WORLD_FORMAT.md)
- [`UAF_PACK.md`](../../tools/UAF_PACK.md)
- [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md)
- [`MEMORY_OWNERSHIP.md`](MEMORY_OWNERSHIP.md)
