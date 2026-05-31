# Benchmark de APIs retro para tiles y escenas virtuales

Este documento deja rastro de la lectura comparativa usada para orientar el
engine C++23. No pretende copiar ACE ni Scorpion: extrae responsabilidades que
un engine retro suele necesitar y las traduce a una arquitectura propia, con UAF
como formato fuente.

## Fuentes revisadas

- ACE, especialmente `docs/programming/tilebuffer.md`,
  `include/ace/managers/viewport/tilebuffer.h`,
  `include/ace/managers/viewport/scrollbuffer.h` y
  `include/ace/managers/viewport/camera.h`.
- Scorpion Engine, con informacion publica de:
  - `https://www.patreon.com/scorpionengine`
  - `https://github.com/earok/scorpion-editor-demos`
  - `https://www.scorpion.wiki/`
  Lo publico confirma el enfoque multi-plataforma Amiga / Mega Drive / NeoGeo,
  demos abiertas, editor cerrado y areas funcionales como actors, maps, blocks,
  code, audio, panels, anims y project settings.
- Universal Asset Format, especialmente `uaf-core/src/formats/scene.rs`,
  `uaf-core/src/formats/tileset.rs` y `demo-scenes/FORMAT.md`.

## Lo que ACE resuelve bien

ACE es de bajo nivel, pero su API de viewport contiene ideas muy solidas:

- `camera`: posicion actual/anterior, limites de mundo, delta y doble buffer.
- `scrollbuffer`: superficie mayor que la pantalla, margenes ocultos, modulo,
  bloques de Copper y sincronizacion al final del frame.
- `tilebuffer`: tilemap grande, tileset, callback de dibujo, cola de redraw,
  invalidacion por tile o rectangulo y funciones rapidas para saber si una zona
  esta ya dentro del buffer.

La leccion importante es que un scroll suave en Amiga no debe redibujar toda la
pantalla. Debe mover la ventana con punteros/fine scroll y recomponer solo las
columnas o filas que entraran por el margen oculto.

## Lo que Scorpion sugiere funcionalmente

Scorpion no expone todo el runtime como codigo abierto, asi que aqui solo se
usan datos publicos y demos abiertas. Aun asi, su forma de producto es valiosa:

- el usuario piensa en proyectos, mapas, actores, animaciones, audio y bloques
  de comportamiento, no en registros;
- hay demos para plataformas, RPG, bullet hell, carreras, parallax, render
  modes, paletas, escalado y translucencia;
- el editor y el motor apuntan a varias maquinas 68K, por lo que separan lo
  funcional de lo que cada backend puede renderizar;
- la wiki comunitaria organiza el aprendizaje alrededor de pestañas de editor y
  conceptos de juego: actors, codeblocks, panels, maps, audio, blocks.

La leccion para nuestro engine es que las capas altas deben poder describir un
juego en terminos de escena, actores, tiles, colisiones, triggers, audio y
scripts. El Amiga debe ser un backend poderoso, no el lenguaje mental de todo el
proyecto.

## Lo que UAF ya trae

UAF ya tiene mucho vocabulario compatible con esta direccion:

- `SceneAsset` con modo OCS, paletas, zonas horizontales, capas, sprites y
  efectos Copper.
- `TilesetAsset` con tiles, paleta, atlas y metadatos por tile.
- Tilemaps indexados con bits de flip horizontal/vertical.
- Colisiones con cajas, circulos, triangulos y poligonos.
- Contratos de scroll/runtime para indicar estrategia, viewport y costes.

La siguiente convergencia natural es exportar un formato cocinado UAF-R que
alimente directamente `amg::scene::VirtualScene`, `TileLayer` y los futuros
drivers de scroll.

## API objetivo del engine

La API C++ debe quedar en capas:

1. `amg::scene::VirtualScene`: camara, capas, actores, triggers y contratos de
   runtime. No conoce registros Amiga.
2. `amg::graphics::tilemap::TileMap16`: celdas, dirty flags por buffer y
   descomposicion de scroll en tile/coarse/fine.
3. `TileScrollDriver` Amiga: convierte una capa de tiles en trabajos de blitter,
   punteros de bitplane, `BPLCON1`, invalidaciones y margenes ocultos.
4. `CopperScheduler`: recibe contribuciones de driver, paleta, agua, HUD,
   blobs y efectos de escena, y compone una copperlist unica por frame.
5. `FramePlan`: ordena blits y presupuestos de bus para que las demos midan si
   el frame cabe en el ciclo PAL esperado.

Este enfoque evita hacer "C con clases": el juego podra pedir cosas como
`camera.center_on(player)`, `scene.load("forest_01")`,
`tile_layer.invalidate(world_rect)` o `actor.play("jump")`. El backend Amiga
decidira si eso termina siendo scroll por Copper, blitter, sprites hardware,
BOBs, redraw parcial o CPU.

## Ajustes recomendados para UAF

- Separar claramente `authoring scene` y `runtime scene`. El primero puede tener
  nombres, capas comodas y datos editoriales; el segundo debe estar ya cocinado
  para DMA, blitter y alignment.
- Cada tile layer deberia exportar:
  - tile size;
  - dimensiones de mundo;
  - estrategia de framebuffer;
  - margenes ocultos;
  - si permite scroll fino hardware;
  - profundidad en bitplanes;
  - prioridad y playfield destino;
  - tiles animados y metadatos de colision.
- Cada escena deberia exportar contratos de Copper por zona, no copperlists
  finales obligatorias. El engine debe poder fusionar efectos de varios recursos.
- Las colisiones deben tener una capa logica independiente del render, aunque
  puedan derivarse de metadatos de tile.
- Los actores deberian referenciar animaciones/slices y no imagenes crudas.

## Estado implementado

- `engine/include/amg/graphics/tilemap/tile_scroll.hpp` contiene el modelo de
  tilemap 16x16 con dirty flags por buffer y scroll descompuesto.
- `engine/include/amg/scene/virtual_scene.hpp` introduce camara 2D, capas de
  tiles, estrategia de scroll y plan retenido de escena.
- `demos/052_tile_staging_blits` ya atraviesa `VirtualScene` antes de compilar
  los blits manuales de la demo. Es una comprobacion minima, pero importante:
  la demo de bajo nivel empieza a depender de la abstraccion que usara el engine.
- `demos/100_virtual_tile_scene_scroll` valida el primer resultado humano: mapa
  virtual mayor que pantalla, camara retenida con fine scroll, paletas EHB por
  zonas y analisis automatico de captura. Aun usa un raster didactico; el driver
  optimizado queda como siguiente paso.

## Siguiente MVP recomendado

Crear `100_virtual_tile_scene_scroll`:

- mapa virtual mayor que la pantalla;
- camara que avanza sincronizada a VBlank;
- recomposicion solo de columnas/filas ocultas;
- prueba automatica que verifique por trazas el dirty count y por captura que el
  tile esperado aparece tras varios desplazamientos;
- export opcional desde un tilemap UAF indexado sencillo.
