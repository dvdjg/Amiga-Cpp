# Arquitectura 3D para juegos sobre el engine

Este documento define las piezas que necesita una capa 3D utilizable por un juego y cómo reutilizar
las matemáticas, mallas, polígonos, superficies y primitivas actuales. El objetivo es una arquitectura
modular para juegos pequeños en Amiga 500, incluida una adaptación *Lite* de *Superhot*. No afirma
que el pipeline completo descrito esté implementado; el estado actual y las carencias están en
[`3D_RENDER_VS_PHYSICS.md`](3D_RENDER_VS_PHYSICS.md) y [`3D_PHYSICS.md`](3D_PHYSICS.md).

## Restricción raster y representación geométrica

El framebuffer Amiga es planar y el hardware no ofrece un rasterizador de triángulos. El engine debe
expresar la salida como operaciones de pantalla: spans por scanline en CPU, líneas por Blitter,
relleno de polígonos convexos por CPU/Blitter y operaciones planares. La triangulación no es una
representación de salida obligatoria ni conviene convertir todos los modelos a triángulos por
costumbre.

```text
asset/modelo
    → caras convexas (triángulos válidos como caso count=3)
    → clipping y visibilidad en espacio de cámara
    → polígonos convexos en espacio de pantalla
    → painter order / grupos de material
    → spans CPU o fill/line jobs del Blitter
    → bitplanes planares + paleta/Copper
```

Las caras cóncavas deben particionarse fuera del runtime en polígonos convexos. El rasterizador no
debe recibir una cara cóncava y asumir que `convex_spans` o el relleno hardware la resolverán. El
asset puede conservar caras n-gon y rangos de índices, pero su receta de cocinado debe declarar
convexidad, normal, material, flags y proxy de colisión.

## Pipeline de render

El renderer por objeto debe tener etapas visibles y buffers de capacidad fija:

1. **Selección de objeto**: frustum/sphere/AABB de objeto contra el volumen visible.
2. **Transformación**: vértice local → mundo → cámara usando el escalar retro y scratch del
   llamador; no recalcular transformaciones que una jerarquía ya haya compuesto.
3. **Culling de cara**: back-face si se conoce winding y la cara es opaca; conservar doble cara para
   superficies abiertas o materiales que lo pidan.
4. **Clipping de cámara**: recortar contra near plane antes de dividir por `z`. Si una cara cruza
   near, interpolar sus aristas y emitir el polígono resultante; descartar si todos los vértices
   quedan detrás. Añadir far/side planes solo si el juego no garantiza límites de mundo/cámara.
5. **Proyección**: dividir los vértices ya válidos. Proteger rango, denominador mínimo, saturación y
   coordenadas fuera del rango `s16`; no convertir `z=0` en `1` como sustituto del clipping.
6. **Clipping de viewport**: clipping 2D contra el rectángulo de pantalla, reutilizando Sutherland–
   Hodgman de `retro::clip_polygon` o una variante de dominio que no fuerce representación `Vec2`
   retro. Los polígonos que cruzan el borde se conservan recortados, no se descartan por bbox.
7. **Orden/composición**: painter order para geometría opaca sin Z-buffer; separar materiales con
   reglas de orden distintas y evitar ordenar objetos/caras cuando la topología garantiza que el
   orden no afecta al resultado.
8. **Rasterización**: convex n-gon → spans CPU como referencia; elegir Blitter para trabajos grandes
   si el perfil confirma beneficio. Wireframe → segmentos únicos por arista visible, no tres líneas
   por cara con aristas compartidas repetidas.

`mesh_renderer` actual transforma y ordena caras, proyecta todos los vértices y después delega el
clipping 2D de caras en `Surface`. La arquitectura objetivo debe incorporar etapas 4–6 en el
renderer 3D para proteger la división perspectiva y evitar overflow antes de llegar a `Surface`.

## Escena 3D y cámara

La escena 3D debe ser una capa de dominio separada de `scene::World`/`Camera2D`, aunque reutilice su
propiedad de recursos, actores, tiempos y planner donde encaje. El mínimo modelo es:

```text
Scene3D
  ├── Camera3D (view, projection, near/far, viewport)
  ├── Node3D (local transform, parent, children, visibility, layer mask)
  ├── Renderable (MeshAsset, Material, bounds, render flags)
  ├── Collider (proxy, collision layer/mask, trigger)
  ├── Body3D (static / kinematic / arcade-dynamic)
  └── Light policy (flat face/material index; sin iluminación por píxel)
```

El grafo debe tener capacidad fija por perfil, handles generacionales y recorrido iterativo o una
pila scratch acotada. Las transformaciones globales se recalculan solo para nodos sucios. El juego
debe poder crear una escena estática precocinada que no necesite `Node3D` por vértice ni un objeto
polimórfico por cara.

`Camera3D` debe definir un convenio único de ejes, handedness, dirección de vista, rango positivo de
`z`, plano cercano, focal/projection scale y viewport. El plano cercano forma parte del contrato de
render, no es un detalle opcional de `project_perspective`.

## Assets y modelos

La carga de geometría debe separar formato fuente de representación de runtime:

- **Fuente**: formato host elegible (OBJ u otro) con unidades, pivote, materiales, caras, smoothing,
  jerarquía, animación y proxies etiquetados.
- **Cocinado host**: validar índices, winding, convexidad, normales, límites, bounds, submeshes,
  materiales, poses y proxies; particionar caras cóncavas; generar mallas LOD; comprimir si compensa.
- **Runtime**: blob endian-safe con header/version, arrays contiguos y offsets relativos validados;
  descomprimir/copiar una vez a memoria propietaria antes de publicar la vista.
- **Memoria**: malla y tablas CPU-only prefieren Fast cuando exista; datos leídos por DMA viven en
  Chip. La vista del asset nunca es propietaria.

`MeshBlob`, `MeshViewT`, `PolyMeshViewT`, `obj2c` y el chunk `MeshPoly` ya aportan puntos de partida,
pero falta un contrato único de `MeshAsset` que conecte loader, material, jerarquía, bounds, receta
de clipping, LOD y proxy físico. El formato de fuente y runtime previsto se especifica en
[`MODEL3D_ASSET_FORMAT.md`](MODEL3D_ASSET_FORMAT.md).

## Rigging y animación

El rigging no debe ser requisito para el primer juego 3D. En un 68000, la ruta recomendada por orden
de coste es:

1. **Malla rígida por nodo**: partes del personaje con transforms locales y animación por keyframes
   de nodos; reutiliza jerarquía y no modifica vértices.
2. **Poses precocinadas**: el host genera un conjunto discreto de poses; el runtime interpola o
   selecciona una pose con buffers fijos.
3. **Skinning CPU**: solo para un caso visual que no pueda resolverse con partes rígidas; limitar
   huesos por vértice, precisión fixed y vértices visibles. Cocinar palettes de matrices y medir.

La representación de colisión no sigue el skinning por vértice: usa proxies unidos a nodos/huesos
(cápsulas, esferas u OBB) y actualiza solo sus transforms. Los clips deben incluir duración, keyframe
rate, looping, root motion explícito y bounds por clip para culling.

## Colisión, intersecciones y física

La simulación se organiza independiente del renderer:

```text
Body state → broadphase → narrow phase → contact/event → response → transform sync
```

- **Consultas**: raycast/segment cast para disparos e interacción; sweep de cápsula o esfera para
  jugador y proyectiles; point/overlap tests para selección.
- **Broadphase**: grid/hash 3D o lista espacial precocinada para cuartos pequeños; AABB de mundo por
  proxy; capacidad fija y reutilizable. El `SpatialHash` actual es 2D, no se reutiliza fingiendo una
  coordenada.
- **Narrow phase**: esfera/esfera, esfera/plano, cápsula/segmento, AABB/OBB por SAT; mallas visuales
  contra proxies simples. GJK/EPA solo con consumidor y perfil que lo justifiquen.
- **Colección de esferas**: un collider compuesto puede aproximar cabeza/torso/extremidades con pocos
  pares esfera-esfera; todas comparten transform/body, y cada esfera puede colgar de un nodo rígido.
  Broadphase usa el AABB envolvente; narrow phase compara distancia al cuadrado y radios combinados,
  sin raíz cuadrada. Limitar cantidad por perfil (típicamente 4–8 en A500), collision masks y parar
  al primer hit cuando no se soliciten contactos completos. El formato y el contrato están en
  [`MODEL3D_ASSET_FORMAT.md`](MODEL3D_ASSET_FORMAT.md).
- **Jugador**: body cinemático con sweep y resolución contra planos/OBB; permite movimiento FPS sin
  solver general.
- **Dinámicos**: para *Superhot Lite*, usar cuerpos arcade y pocos objetos empujables; integrar con
  `dt` fijo, un impulso/contacto y sleep. Evitar pilas y CCD general al principio.
- **Triggers**: separar overlap de colisión sólida; emitir eventos de entrada/salida para zonas,
  pickups, puertas y el estado de tiempo lento.
- **Tiempo de Superhot**: modelar escala temporal por actor o grupo y decidir explícitamente qué
  permanece a tiempo real (jugador/cámara/entrada) y qué se ralentiza. No modificar el `dt` global
  de física de modo que rompa estabilidad o timers del engine.

Las propiedades de masa/inercia se cocinan en host desde una malla cerrada validada o se asignan a
proxies; no se calculan por frame. El juego puede elegir cinemática pura si no necesita interacción
física entre objetos.

## LOD y selección de trabajo

Render LOD, animación LOD y simulación LOD son políticas separadas:

- **Render LOD**: mesh precocinado por distancia/histeresis, o descarte del objeto fuera del
  volumen de cámara; cambio estable, sin cocinar geometría en runtime.
- **Animation LOD**: pose completa cerca, pose reducida o keyframe discreto lejos, pausa fuera de
  escena cuando sea seguro.
- **Simulation LOD**: física completa para jugador/amenazas próximas; proxies simplificados para
  objetos lejanos; congelar o actualizar por ticks reducidos solo si no rompe triggers.
- **Budget**: capacidad declarada de objetos, caras, proxies y contactos por frame; degradar con
  reglas deterministas (LOD, culling, desactivar partículas) y reportar overflow.

El LOD de agentes de `eng::sim` es de simulación de criaturas y no reemplaza un selector de mallas 3D.

## Optimización por invariantes verificadas

El pipeline no debe ejecutar siempre todas las fases. Cada fast path debe seleccionarse con flags o
recetas de asset validados, más un fallback de referencia:

| Invariante demostrada | Fase que puede reducirse/omitirse | Salvaguarda |
|---|---|---|
| Objeto estático y bounds dentro del frustum durante toda la escena | culling de objeto por frame | scene bake/volumen de cámara estáticos; invalidar al mover cámara |
| Convex solid con winding coherente y cámara exterior | ordenar caras visibles | `ConvexSolid`: back-face cull solamente |
| Todo vértice transformado tiene `z >= near` por diseño | clipping near por cara | validación host de bounds/rango; debug assert por frame |
| Geometría del cuarto enteramente dentro del viewport/proyección | clipping 2D de sus caras | viewport y cámara bloqueados; reservar guard/clip exterior |
| Malla opaca con material plano y sin solapamiento ambiguo | blending/orden general | material opaco y tipo topológico explícitos |
| Modelo estático precocinado en camera/world space | transformación por vértice | conservar variante local si cambia nodo o cámara |
| Backface culling ya precalculado para una pose fija | culling dinámico | la pose/cámara no puede cambiar; invalidar al cambiar |
| Proxy físico independiente y estático | construir collider desde la malla | asset incluye proxy validado y transform ligado |

Ejemplos de perfiles: `StaticRoomProfile` puede eliminar jerarquía dinámica y culling de objetos;
`ConvexPropProfile` elimina painter sorting de caras visibles; `NearClippedWorldProfile` mantiene
clipping near pero usa clipping 2D; `InteriorBoundedProfile` puede demostrar que no hay caras que
salgan de pantalla y omitir clipping 2D. La opción “geometría no se sale de pantalla” debe ser una
precondición verificable del nivel/cámara, no una suposición global del renderer.

Evitar recalcular en hot path: bounds, normales, convex decomposition, proxies, tablas de keyframes,
rangos de índices y agrupaciones por material se cocinan en host/setup. Para el A500, limitar
producto/división a 16×16 cuando sea posible, usar fixed con rango probado, no usar 64-bit en bucle,
presupuestar buffers scratch y medir divisiones de perspectiva y overdraw.

## Primer perfil de juego: Superhot Lite

Arquitectura inicial sugerida, sin convertirla en requisito de todo juego 3D:

- habitaciones pequeñas, estáticas y con límites visibles conocidos;
- cámara perspectiva fija o con movimiento acotado;
- mallas opacas low-poly, caras convexas y flat shading por paleta;
- near clipping obligatorio si objetos pueden cruzar la cámara; clipping 2D conservador salvo
  habitaciones precalculadas dentro de viewport;
- jugador cinemático con cápsula/proxy y raycasts de arma;
- pocos proyectiles/cajas dinámicas con proxies esfera/OBB y respuesta arcade;
- tiempo lento por escalas de ticks de actor, con jugador/entrada/cámara en tiempo normal;
- animación de personajes por partes rígidas o poses precocinadas; sin skinning de propósito general;
- LOD inicial por descarte y versiones de malla precocinadas, no simplificación dinámica.

Este perfil valida las abstracciones comunes y permite introducir fast paths de cuarto estático sin
comprometer la corrección de otros consumidores.

## Paquetes de trabajo que podrán convertirse en roadmap

1. Renderer 3D: cámara, near clipping, viewport clipping, buffers acotados y equivalencia con el
   relleno CPU.
2. Asset 3D cocinado: materiales, convexidad, bounds, proxies, nodos, versiones LOD y validación.
3. Scene3D: jerarquía, transforms sucios, cámara, extracción de renderables y presupuestos.
4. Queries/collision: raycast, sweep del jugador, broadphase 3D y proxies.
5. Física arcade: fixed timestep, triggers, cuerpos simples, sleep y time scale por actor.
6. Animación: nodos rígidos y clips; evaluar después si hace falta skinning CPU limitado.
7. LOD y fast paths: perfiles estáticos/confinados/convexos con aserciones y fallback.
8. Juego de prueba: una habitación Superhot Lite, validada primero en host y luego A500 con perfil.

## Criterio de diseño

El juego describe cámara, mallas, materiales, proxies y comportamiento. El renderer adapta caras
convexas a spans/polígonos del raster Amiga; la plataforma elige CPU/Blitter y organiza la paleta por
Copper. Las optimizaciones pueden quitar etapas solo si el perfil del asset/escena demuestra una
invariante y existe validación que detecte cuándo deja de cumplirse.

## Referencias

- [`3D_RENDER_VS_PHYSICS.md`](3D_RENDER_VS_PHYSICS.md)
- [`3D_PHYSICS.md`](3D_PHYSICS.md)
- [`OBJECT3D_MESH_VIEW.md`](OBJECT3D_MESH_VIEW.md)
- [`RASTER.md`](RASTER.md)
- [`MEMORY_OWNERSHIP.md`](MEMORY_OWNERSHIP.md)
- [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md)
