# Drivers graficos

Un driver grafico es una estrategia de composicion para un hardware concreto. No es
solo un modo de pantalla: tambien define presupuestos, memoria, reglas de paleta,
uso de sprites, copper, blitter y restricciones artisticas.

La logica de juego debe expresar intenciones:

- mostrar actor;
- mover camara;
- activar hotspot;
- cambiar room;
- pedir efecto de luz/agua;
- mostrar dialogo.

El driver decide como materializar esas intenciones en el hardware disponible.

## Drivers iniciales

### `EhbScene`

Primer objetivo serio. Pensado para aventura grafica moderna:

- 6 bitplanes EHB;
- fondos ricos de 32 colores base + 32 half-brite;
- pocos BOBs grandes;
- cursor o detalles con sprites hardware;
- copper por zonas y efectos baratos;
- cambios completos de paleta solo en zonas seguras o transiciones.

La primera prueba ejecutable es `demos/030_ehb_palette_zones`: genera una reticula
planar de indices 0..63 y usa el Copper para cambiar la paleta completa en tres
zonas verticales.

El primer bloque reutilizable ya existe en
`engine/include/amg/graphics/drivers/ehb_scene.hpp`:

- `EhbPalette`: 32 colores fisicos RGB444.
- `EhbPaletteZone`: cambio de paleta asociado a una linea raster.
- `StaticEhbSceneConfig`: descripcion de una escena EHB estatica.
- `StaticEhbScene`: reserva bitplanes/copperlist en Chip RAM, programa display
  320x256 EHB, activa bitplane DMA y construye la copperlist final.

La copperlist ya pasa por `engine/include/amg/graphics/copper/scheduler.hpp`.
Este `CopperScheduler` minimo no resuelve todavia conflictos complejos, pero ya
centraliza el setup EHB, las paletas y las zonas raster, y devuelve un informe de
coste para que las pruebas y el futuro exportador UAF-R puedan detectar escenas
caras antes de que se conviertan en corrupcion visual.

El coste por linea empieza en `engine/include/amg/graphics/copper/timeline.hpp`.
`CopperTimeline` cuenta waits y moves por linea, marca las lineas visibles que
superan un presupuesto conservador de H-BLANK y alimenta `ScheduleReport`.

El primer efecto reusable esta en
`engine/include/amg/graphics/effects/palette_cycle.hpp`. `PaletteCycleEffect`
rota un tramo de paleta fisica sin tocar bitplanes; la demo
`demos/040_palette_cycle_effect` lo valida con captura y `runStatus.detail`.
La demo ya usa `engine/include/amg/graphics/frame_plan.hpp`: el efecto genera un
parche de paleta en `FramePlan` y `StaticEhbScene` actualiza solo los valores de
los MOVEs `COLORxx` existentes, sin recompilar la copperlist completa.

La primera prueba de Blitter esta en `demos/050_blitter_bobs`. La demo crea
trabajos `BlitJob` dentro de `FramePlan`; el backend Amiga los ejecuta con el
Blitter hardware sobre los 6 bitplanes EHB. Ya existen tipos para copia,
restore, BOB cookie-cut y `MaskedBlobNoSave`. Este ultimo modela la tecnica tipo
Mega Typhoon: blobs no solapados que se escriben directamente sobre un playfield
sin guardar el fondo previo. Por ahora exige X alineada a 16 pixels y no hace
clipping. El BOB principal ya se mueve con save/restore real: restore anterior,
save de la nueva zona y draw cookie-cut. `FramePlan` tambien fusiona dirty rects
para que las areas anterior/nueva del BOB se puedan tratar como una region logica
de redraw aunque el backend siga emitiendo jobs concretos de Blitter.

`demos/051_blitter_shifted_bobs` valida el siguiente contrato: un `BlitJob`
enmascarado puede pedir `source_shift` y el backend lo traduce a los shifts A/B
de `BLTCON0`/`BLTCON1`. Esto permite X no alineada a 16 pixels con una word extra
por fila de fuente.

`demos/052_tile_staging_blits` separa los blits de tiles de los blits de sprites.
`TileBlockCopy` usa la misma copia rectangular que `CopyRect`, pero representa una
intencion distinta: preparar columnas, filas o bloques de tilemap en zonas no
visibles del playfield. La demo compone un bloque 4x4 de tiles en un buffer Chip
RAM no visible y despues lo publica al playfield EHB con otro blit.

El primer modelo retenido para scroll vive en
`engine/include/amg/graphics/tilemap/tile_scroll.hpp`. `TileMap16` no sabe nada de
`BPLCON1` ni `BPLxPT`: solo empaqueta indices de tile con dirty flags por buffer y
descompone una posicion de scroll en tile/coarse/fine. El driver Amiga futuro sera
quien traduzca esa intencion a Copper y Blitter.

La primera fachada de escena vive en
`engine/include/amg/scene/virtual_scene.hpp`. `VirtualScene`, `Camera2D` y
`TileLayer` describen un escenario virtual en terminos portables: mundo, viewport,
capas, margenes ocultos y estrategia de scroll. La demo 052 ya pasa por esa capa
antes de emitir blits, de modo que las proximas demos podran crecer hacia un
`TileScrollDriver` sin reescribir la logica.

`demos/100_virtual_tile_scene_scroll` es el primer MVP visual sobre esa fachada.
Muestra un mapa 64x16 con camara X=57, `fine_x=9`, paletas EHB por zonas Copper y
tiles generados como words planares. Todavia no es el driver definitivo: recompone
el viewport de forma didactica. Su valor es fijar el contrato estetico y de API
antes de optimizarlo con Blitter, margenes ocultos y `BPLCON1`.

La siguiente pieza ya vive en
`engine/include/amg/graphics/tilemap/tile_scroll.hpp`: `ProgressiveTileScheduler`.
Su objetivo es evitar picos de Blitter al entrar en una zona nueva. En vez de
dibujar toda la columna/fila offscreen de golpe, cada tile puede entrar en cola con
`frames_until_visible`; el driver consume solo un presupuesto pequeno por frame.
Esto permite un patron estilo Lionheart: preparar lo que falta antes de que el
scroll lo haga visible, sin parar la escena cuando aparece una franja nueva.

El API distingue tres rutas de driver:

- horizontal: columnas ocultas, punteros de bitplane/coarse X y `BPLCON1`;
- vertical: filas ocultas y avance de buffer/modulo;
- bidireccional: columnas, filas y esquina, con un presupuesto compartido.

La ruta bidireccional debe ser la opcion ergonomica para el juego. El usuario mueve
una camara 2D; el driver decide si ese frame puede usar la ruta barata horizontal o
vertical, o si necesita la ruta completa.

`engine/include/amg/graphics/drivers/ehb_tile_scroll.hpp` implementa el primer
driver real de esta familia. `EhbTileScrollScene` reserva una superficie EHB de
480x416, muestra una ventana de 320x256 y reconstruye su copperlist con punteros
de bitplane desplazados y `BPLCON1` para fine X. Los margenes de 160 pixels
equivalen a 10 columnas y 10 filas ocultas: suficiente para predibujar tiles
sueltos en orden de urgencia durante varios frames antes de que crucen el borde
visible, incluso con una ruta circular de cuatro tiles de radio.

`demos/101_ehb_tile_scroll_driver` demuestra la ruta inicial con movimiento real:
la camara se mueve dos tiles a derecha/izquierda, dos tiles arriba/abajo y despues
recorre una orbita de cuatro tiles de radio. Cada frame reconstruye la copperlist,
y los tiles offscreen aceptados por presupuesto se convierten en `TileBlockCopy`
que el backend ejecuta por Blitter antes de instalar la copperlist final. La
prueba de secuencia debe detectar animacion, no solo una captura estatica correcta.
Para evitar un diente de sierra horizontal, el driver redondea el puntero coarse X
hacia el siguiente bloque de 16 pixels cuando hay fine scroll y programa
`BPLCON1 = 16 - fine`. Asi la camara de juego puede crecer hacia la derecha
mientras el contenido visible se desplaza de forma continua hacia la izquierda.
La prueba fuerte usa FrameScope con `-Profile amiga-scroll -RequireProfileMatch`
para comparar esa telemetria de camara contra el movimiento observado.

La misma cabecera incluye ahora `EhbBidirectionalRingPrefetch`, un planificador de
slots de columnas y filas. Todavia no intenta hacer wrap fisico en mitad de la
ventana visible, porque OCS no puede saltar de final de fila a principio de fila
durante un unico fetch de bitplanes. Su contrato si es el que necesitaremos para el
driver completo: mapear columna/fila de mundo a slot fisico, recordar que franja
vive en cada slot y pedir solo lo que falta. La demo 101 recicla columnas y filas;
el nibble bajo de `runStatus.detail` publica flags (`0x1` columnas, `0x2` filas).

Los blobs futuros tambien podran tener una contribucion Copper asociada. Por
ejemplo: cambiar colores justo en sus franjas, ondular filas mediante scroll/splits
de bitplanes o crear regiones no rectangulares. Esa informacion no debe escribir
la copperlist directamente desde el blob; debe entrar como intencion en el
`CopperScheduler` para resolver conflictos con paletas, playfields, sprites y
otros efectos raster.

Esta clase todavia no es el driver completo de aventura. Es el nucleo de display
sobre el que construiremos `EhbRoomDriver`: BOBs, cursor hardware, profundidad por
Y, hotspots, color cycling y scheduler central de Copper.

### `Standard5`

Modo de 5 bitplanes para 32 colores reales. Menos costoso que EHB y util para
escenas con mas movimiento.

### `Standard4`

Modo de 4 bitplanes para 16 colores. Base recomendable para accion rapida,
muchos BOBs o scroll exigente.

### `FakeDualPlayfield`

Composicion estilo "DPF falso": por ejemplo 4 planos para foreground y 1 plano
para fondo, sombra, mascara o decoracion. Inspirado en trucos como los usados en
juegos que aparentan mas capas sin activar Dual Playfield real.

### `DualPlayfield`

DPF real OCS con PF1/PF2, scroll independiente y parallax. Es el driver para
escenas tipo Mega Typhoon o Jim Power, con el coste artistico de repartir colores
por playfield.

### `SpriteBackdrop`

Uso de sprites hardware como parte del fondo: tiras, multiplexado vertical,
repeticion horizontal y fondos asistidos por copper.

### `CopperHeavy`

Escenas especiales con fuerte carga copper: intros, menus, fondos raster,
transiciones, cielos, agua o efectos demoscene.

## Regla de oro

Si una escena necesita romper demasiadas reglas de un driver, probablemente no
necesita mas excepciones: necesita otro driver.

## Abstraccion central

Los drivers no deben competir entre si por los coprocesadores. El orden previsto es:

1. La logica de juego actualiza un `RenderScene` retenido.
2. El driver grafico compila esa escena a un `FramePlan`.
3. `CopperScheduler`, `BlitterQueue` y `SpriteAllocator` arbitran recursos.
4. El backend Amiga emite registros, DMA y listas ya validadas.

Esto es lo que permitira incorporar efectos de demoscene sin convertir cada juego
en una coleccion de hacks irrepetibles.
