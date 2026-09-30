# Arquitectura para juegos pixel art 2D e isométricos

Este documento define las utilidades de alto nivel necesarias para juegos 2D basados en bitmaps,
tilemaps y sprites/BOBs, incluidos juegos isométricos. El sistema debe permitir movimiento y
colisión simples, mecanismos, actores y escenarios modificables sin imponer un motor de física en
juegos que no lo necesitan. Cada capacidad opcional debe poder elegirse por perfil/plantilla o
configuración de mundo para que el camino sin física no pague costes de integración, broadphase ni
solver.

## 1. Punto de partida

El engine ya aporta piezas que se deben componer:

- `TileSource`, `TileLayerMap`, `ChunkCache`, `StreamingWorldMap`, `TileEditor` y `AttributeTable`
  para contenido estático, disperso, streaming y edición de celdas.
- `SpriteSheet`, `Animation`, `Visual`, `Actor`, `ActorStore`, `World` y `Layer` para contenido
  visual y estado retenido.
- `Surface`, `Rasterizer`, `FramePlan`, BOB/sprite y el compositor para materializar el dibujo.
- `eng::Box`, AABB, intersección de segmentos, punto-en-triángulo, punto-en-convexo, SAT 2D y
  `SpatialHash` como primitivas geométricas/utilitarias.
- LOD de simulación en `eng::sim`, que no es un motor de movimiento o colisión de actores 2D.

No hay todavía una capa general que conecte el metadata de colisión del mapa, las hitboxes móviles,
el movimiento con resolución, las fuerzas/inercia y las modificaciones de escenario. La detección
de colisión hardware `CLXDAT` solo informa ciertos cruces entre sprites/bitplanes; no da posición de
contacto ni reemplaza las consultas geométricas de gameplay.

## 2. Separación de contenido, simulación y representación

```text
Asset / mapa cocinado
  ├── visual: tilesets, atlas, animaciones, capas
  ├── metadata: flags, material, trigger, collider/proxy
  └── geometría: grid, polígonos convexos y bounds
          │
          ▼
World2D / IsoWorld
  ├── contenido estático y chunks modificables
  ├── actores con transform lógico, movimiento opcional y collider opcional
  ├── mecanismos/triggers/eventos
  └── cámara/proyección
          │
          ├── queries y simulación (solo si el perfil lo activa)
          └── planner visual → Surface / FramePlan / Copper
```

Un actor tiene identidad y estado de gameplay independientemente de si se dibuja como sprite DMA,
BOB, bitmap de playfield o entidad no visible. El componente de colisión no debe inferirse del
rectángulo del sprite ni de su animación, salvo que el asset elija explícitamente esa política.

Contrato recomendado por entidad:

- `Transform2D`: posición de mundo, orientación discreta o ángulo opcional, escala permitida por el
  perfil, y anclaje visual separado del origen de colisión.
- `Motion2D` opcional: velocidad, aceleración, velocidad máxima y flags de movimiento.
- `Collider2D` opcional: forma, offset respecto al transform, capa/máscara y flags sólido/trigger.
- `Body2D` opcional: tipo Static, Kinematic o Dynamic, masa inversa y material de respuesta.
- `Renderable2D` opcional: `Visual`/animación, ancla, capa y clave de orden visual.
- `Mechanism` opcional: estado compacto (puerta, plataforma, interruptor, obstáculo, destructible)
  y eventos de activación.

Los juegos configuran capacidades al compilar o al crear el mundo: `WorldProfile<StaticMap>` no
contiene arrays de cuerpos ni ejecuta física; `WorldProfile<Kinematic>` añade movimiento/resolución;
`WorldProfile<ArcadePhysics>` añade acumulación de fuerzas, respuestas y cuerpos dinámicos. La
especialización puede eliminar sistemas enteros del binario y del hot path. Si se elige runtime,
usar bit flags/configuración de mundo una sola vez en setup, no ramas por entidad para capacidades
que el juego nunca usa.

## 3. Coordenadas 2D e isométricas

La colisión se calcula en el espacio donde se define el gameplay:

- **Top-down/lateral 2D**: coordenadas de mundo `(x,y)`; el eje vertical es normalmente también el
  eje de movimiento.
- **Isométrico de tablero**: coordenadas lógicas `(u,v)` de celda. La proyección a pantalla es una
  transformación de dibujo, por ejemplo `screen_x = origin_x + (u-v)*tile_w/2` y
  `screen_y = origin_y + (u+v)*tile_h/2 - elevation*elevation_step`. La colisión de caminar por
  celdas se resuelve en `(u,v)`, no en la AABB del sprite proyectado.
- **Isométrico con altura física**: usar `(x,y,z)` o `(u,v,elevation)` como mundo 2.5D cuando hay
  saltos, plataformas superpuestas o proyectiles con altura. `elevation` representa altura del
  suelo/cuerpo, no la coordenada vertical proyectada de pantalla. Reutilizar transforms/pruebas 3D
  baratas solo para esos colliders; mantener mapas de suelo y paredes en grids 2D.

La proyección isométrica no convierte por sí sola el juego en 3D. Un personaje que camina sobre el
suelo puede usar una caja en el plano lógico y una altura visual independiente. Para resolver
oclusiones visuales, ordenar por foot-point/profundidad lógica (p. ej. `u+v`, más un desempate estable),
no por la esquina superior de la imagen. El orden de dibujo nunca debe utilizarse como resultado de
colisión.

Las consultas de puntero/selección convierten el punto de pantalla a celda isométrica y luego
validan contra el rombo real del tile cuando el tile no llena su celda lógica. Las hitboxes de
actores permanecen en mundo lógico; su forma proyectada sirve solo para hit-test visual si el juego
lo solicita.

## 4. Datos de colisión del escenario

El contenido del mapa debe separar al menos:

```text
Tile visual      índice de imagen/animación
Tile attributes  material, navegación, flags de colisión, trigger id
Collider shape   none / full cell / half cell / slope / one-way / convex polygon
Mechanism state  estado mutable (abierta, rota, activada, bloqueada)
```

No codificar todas las reglas en el `TileId`: el tile visual puede variar mientras su atributo de
colisión permanece igual, o una instancia del tile puede estar abierta/rota. El pipeline host puede
convertir propiedades de Tiled/object layers o un formato propio a grids compactos:

- bitset `solid` para colisión binaria por celda;
- tabla de material/flags por celda cuando se necesitan fricción, daño o superficie;
- triggers con ids compactos y lista fija de rectángulos/celdas;
- proxies estáticos por chunk para geometría irregular;
- overrides dinámicos para puertas, bloques empujables y tiles destruidos.

La ruta común de tile collision debe hacer lookup O(1) por celda y no consultar/pintar el tilemap
visual para cada subpaso. Las pendientes o formas especiales se procesan solo si el metadata del
tile las declara; el resto usa la máscara sólida simple.

## 5. AABBs y queries

La API de alto nivel debe ofrecer:

- `overlap(a,b)` y `point_query(point)` para selección y triggers;
- `move_and_slide(body, delta)` para movimiento cinemático contra escenario y cuerpos sólidos;
- `sweep_aabb`/`segment_cast` para evitar atravesar paredes a velocidades altas;
- consultas de zona/celda, line-of-sight y overlaps de triggers;
- colisión pixel-perfect opcional como refinamiento para juegos que de verdad la necesiten.

Las AABB se expresan en coordenadas de mundo con bordes y semántica documentados. La caja de dibujo
puede ser distinta de la caja física. El primer narrow phase debe usar AABB/círculo y grid de tiles;
SAT 2D de polígonos convexos existente queda como opción para colliders poligonales raros, no como
prueba universal.

El `SpatialHash` actual almacena puntos y consulta AABB; no es aún un broadphase completo de colliders
que ocupan varias celdas ni gestiona pares persistentes. Para juegos pequeños se puede omitir y
probar actores contra las celdas cubiertas por su AABB. Para muchos actores dinámicos se extiende a
inserción multi-celda o buckets por chunk, con deduplicación de pares en buffers fijos. Los objetos
estáticos del mapa no se insertan de nuevo cada frame.

## 6. Movimiento y física opcional

Debe existir un gradiente de capacidades, de coste bajo a mayor:

### Perfil sin física

Solo transform y comandos de juego; colisión desactivada. Coste de runtime cero más allá del actor.

### Movimiento por tiles

Para RPGs/puzzles: objetivo de celda, velocidad visual opcional, validación de celda destino,
reservas de celda para evitar que dos actores entren a la vez y triggers al cruzar. No necesita
velocidad, gravedad ni solver.

### Kinematic/AABB

Para plataformas, top-down y juegos de acción: el juego propone delta; el motor subpaso o hace sweep,
resuelve eje por eje y devuelve contactos normalizados. Permite paredes, one-way platforms, slopes
discretas, suelo y puertas sin cuerpos dinámicos.

### Física arcade de cuerpos dinámicos

Opcional por plantilla/presets. Estado fixed-point con `position`, `velocity`, `acceleration`,
`inverse_mass`, damping y material. Orden fijo:

```text
forces/environment → integrate velocity → sweep/move → contacts → response → triggers/sleep
```

Permite:

- gravedad global por mundo/zona o por body;
- viento global, por volumen o por tile/material;
- inercia con `v += a*dt`, límite de velocidad y damping por eje;
- rebote por coeficiente de restitución y fricción simplificada;
- impulsos explícitos para explosiones/armas;
- kinematic/static/dynamic bodies y triggers;
- sleep para detener cuerpos quietos y reducir coste.

El timestep debe ser fijo y derivado de ticks de juego, no del tiempo de render. En A500 conviene
integrar velocidades/aceleraciones con escalas conocidas, productos estrechos auditados, saturación
definida y constantes de `dt` precalculadas. No usar MiniFloat por defecto en el solver ni recurrir
a productos de 64 bits en el hot path.

### Física con fuerzas de ambiente

Viento y gravedad no deben ser callbacks genéricos ejecutados para cada cuerpo si el juego no los
usa. El `PhysicsProfile` selecciona campos activos: gravedad constante se suma sin lookup; viento
por zonas se precocina a grid/celdas o se consulta solo para cuerpos dentro de volúmenes marcados.
Una bandera de material permite fricción, hielo, rebote o daño sin añadir ramas a todas las celdas.

## 7. Mecanismos y escenario modificable

El mundo diferencia geometría estática cocinada de overrides dinámicos:

- Puertas/plataformas móviles son colliders cinemáticos que actualizan transform y barrido.
- Bloques empujables usan body dinámico solo si el juego necesita inercia/rebote; si no, transición
  por estados y movimiento cinemático.
- Destructibles/escenario editable cambian un override de celda/material o un proxy de chunk; no
  reconstruyen la malla ni el broadphase completo.
- Interruptores, pickups y zonas son triggers sin respuesta física.
- Para tilemaps, `TileEditor` y `AttributeTable` pueden marcar dirty visual; debe existir un canal
  paralelo para dirty de colisión/navegación con su propia versión/generación.
- Al modificar un chunk, actualizar solo sus celdas/proxies y las fronteras vecinas afectadas.

La API de juego debería expresar operaciones de dominio (`open_door`, `break_tile`, `set_material`,
`activate_trigger`); la capa World aplica overrides y avisa a visual, navegación y colisión.

## 8. Isométrico: compartir sin mezclar espacios

Lo compartible con 3D es la matemática vectorial, transforms, bounds, SAT y eventualmente queries
con coordenada de elevación. Lo específico del isométrico es la proyección 2D, picking de rombos,
orden de profundidad y metadata de celdas. Recomendación:

- `IsoProjection` convierte entre coordenadas lógicas y pantalla; no participa en narrow phase del
  suelo.
- `IsoWorld2D` usa grid `(u,v)` y AABB/rombos lógicos para movimiento y escenario.
- `IsoWorld3D`/2.5D agrega altura y colliders volumétricos donde haya niveles superpuestos.
- `DepthKey` controla draw order (`u+v`, elevación, sublayer y tie-break estable), no contacto.
- Los sprites pueden tener footprint físico más pequeño que el bitmap transparente y foot anchor
  para alinear base visual/posición lógica.

No usar el SAT de 3D para todas las celdas isométricas: en la mayoría de juegos el grid lógico es más
barato y semánticamente correcto. Reusar el narrow phase 3D para rampas volumétricas, puentes bajo
los que se puede pasar o proyectiles con altura.

### Núcleo compartido con 3D

Lo común debe ser el vocabulario geométrico y las políticas de consulta, no forzar un único espacio
de coordenadas o contenedor:

| Primitiva/política | Compartir entre 2D, iso y 3D | Adaptación requerida |
|---|---|---|
| Vectores, transforms y escalares fixed | Sí | Cada mundo fija unidades, ejes y rango en su receta |
| Bounds AABB y consultas | Conceptualmente sí | El `SpatialHash` actual solo indexa puntos 2D; no es broadphase de colliders con extensión |
| Overlap de cajas/círculos y segmentos | Sí si coincide la dimensión | No proyectar una caja 3D a pantalla para colisión volumétrica |
| SAT de polígonos convexos | Útil en 2D e iso del suelo | SAT 3D de OBB es otro kernel; devolver resultado/contacto de dominio común |
| Raycast y sweep | Misma interfaz de consulta | Implementaciones 2D, 2.5D y 3D seleccionadas por perfil |
| Triggers y capas/máscaras | Sí | Comparar en el mismo espacio físico; `DepthKey` visual no filtra contactos |
| Respuesta física | Conceptos configurables comunes | Integrador/solver especializados por dimensión y capacidad |

La API puede parametrizar dimensiones o políticas en compilación, sin fijar `s16` ni arrastrar
contenedores 3D a un juego 2D. Un collider debe declarar su espacio. La proyección iso se usa para
render, picking y orden visual; la entrada convierte pantalla→mundo en una sola frontera. Las
colisiones se resuelven en coordenadas lógicas y solo se proyectan para debug.

Para iso multinivel, cada collider declara `floor_id` o intervalo de elevación. Dos entidades con
el mismo `(u,v)` en plantas distintas no se bloquean por defecto. Si hay salto se incluye elevación
del cuerpo en la prueba vertical; si solo existe un puente visual, se puede representar con capas y
triggers sin activar una colisión 3D general.

## 9. Opcionalidad y eliminación de coste

Cada mundo elige un perfil al compilar o en setup:

```text
WorldProfile<StaticTiles>      no actors physics, solo lookup de celdas
WorldProfile<Kinematic2D>     query AABB/sweep, sin arrays dinámicos/solver
WorldProfile<Arcade2D>        cuerpos, fuerzas, contactos y sleep
WorldProfile<IsoGrid>         proyección + colisión en coordenada lógica
WorldProfile<IsoHeight>       iso + elevación/queries 2.5D
```

En templates/NTTP, código de módulos no seleccionados debe desaparecer del binario. En un único
binario para varios mapas, la selección se hace en setup y desactiva sistemas completos; el bucle no
debe ejecutar broadphase, timer de física o búsquedas de material si los contadores de cuerpos,
campos y triggers activos son cero.

Capacidades máximas (actores dinámicos, colliders, contactos, overrides, zonas de viento) son datos
de configuración y producen errores de setup si exceden presupuesto. Las tablas estáticas de colisión
y materiales se cocinan en host; grids mutables usan bitsets/overrides compactos en Fast si existen y
Slow como fallback cuando solo los consume CPU.

## 10. Optimización por invariantes y perfiles

Las fases se omiten solo mediante una propiedad de perfil validada en el asset/test:

| Caso garantizado | Optimización | Riesgo a cubrir |
|---|---|---|
| Mapa estático, movimiento por grid | no broadphase de actores; consulta solo celdas barridas | actores grandes y diagonales deben consultar todas las celdas cruzadas |
| Solo colliders AABB alineados | saltar SAT/polígono | puertas/pendientes llevan otro shape y declaran capacidad explícita |
| Mundo sin cuerpos dinámicos | compilar fuera integración y solver | triggers cinemáticos aún pueden activarse |
| Gravedad constante | no consultar campo por actor/celda | zona local de gravedad fuerza activar el módulo de campos |
| Sin viento | eliminar consulta/acumulación de viento | el bit de perfil debe impedir que el código/estado se instancie |
| Escenario estático precocinado | broadphase estático construido una vez | overrides invalidan chunk/version solo localmente |
| Iso top-down sin altura solapada | colisión en `(u,v)` 2D | no resolver objetos que pasan por encima/debajo |
| Sprites con bitmap muy transparente | collider manual precocinado, no bbox visual | evitar que arte transparente distorsione gameplay |
| Colisión solo en celdas y proxies | no pixel-perfect ni máscara en el update | activar máscara solo para casos locales excepcionales |
| Número fijo y pequeño de actores | bucle compacto O(N²) opcional | medir el umbral antes de activar SpatialHash |
| Escenario sin modificaciones | no actualizar índice/bitset en runtime | el modo editable necesita dirty de colisión |

El profiling debe separar movimiento, lookup de tiles, broadphase, narrow phase, respuesta,
triggers y dirty visual. El modo host sirve para equivalencia determinista; A500 valida coste de
codegen/ciclos y presupuesto de memoria.

## 11. Juego inicial y evolución

Para pixel art 2D/isométrico, empezar por mapa cocinado + metadata de colisión + AABB/círculo + actor
kinematic + triggers; añadir fuerzas solo donde el gameplay las use. Un juego tipo *Superhot Lite*
pertenece al perfil 3D/FPS y reutiliza parcialmente queries/proxies, no convierte el modelo 2D/iso en
una abstracción 3D universal.

El futuro roadmap puede separar: (1) metadata cook/format, (2) queries y kinematic 2D, (3) escenarios
editables y mecanismos, (4) arcade physics opcional, (5) proyección/picking isométrico, (6) altura
2.5D y reutilización de proxies/queries 3D, (7) profiling/presets compile-time.

## Referencias

- [`CONTENT_AND_TILEMAP.md`](CONTENT_AND_TILEMAP.md)
- [`OBJECT_SYSTEM.md`](OBJECT_SYSTEM.md)
- [`3D_GAME_ARCHITECTURE.md`](3D_GAME_ARCHITECTURE.md)
- [`3D_PHYSICS.md`](3D_PHYSICS.md)
- [`SCENE_AND_RESOURCES.md`](SCENE_AND_RESOURCES.md)
- [`MATH_LIBRARY.md`](MATH_LIBRARY.md)
