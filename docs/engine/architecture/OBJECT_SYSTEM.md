# Sistema de objetos: representación, transparencia, fondos y necesidades de Copper

Este documento especifica el sistema de objetos del engine: cómo un actor descrito por la aplicación se materializa como **sprite hardware**, **BOB por Blitter** u **objeto CPU**, qué políticas de **transparencia** y de **gestión del fondo** admite, y cómo declara sus **necesidades de Copper** para que una instancia superior (el compositor) monte la copperlist del frame.

Es la pieza de diseño que cierra la infraestructura de objetos del roadmap (`docs/guides/roadmap/NORMALIZACION_REPO.md`, F6). No repite el vocabulario de intenciones ni las plantillas de sprite, que ya están especificados en `VISUAL_EFFECT_SPRITE_DESIGN.md` (`Visual`, `CopperIntent`, `HwSpriteTemplate`, concept `Effect`), ni el modelo de escena retenida de `SCENE_AND_RESOURCES.md`.

Estado: **diseño objetivo**. Las piezas marcadas como EXISTE están implementadas; las marcadas como PROPUESTO son el contrato a implementar.

## 1. Principio rector

Un actor no se define por «ser» un sprite, un BOB o un objeto CPU: se define por **contenido portable, posición y prioridad**, y el engine elige la materialización según recursos disponibles y la puede **reasignar sin que la aplicación lo sepa**. La identidad visual (`Visual`) no cambia al degradar de representación; solo cambia el mecanismo que la dibuja.

```text
aplicación (sin hardware)
  World (retenido, fuente única de verdad)
    ├─ actors[]   (id estable, Visual, posición de mundo, z, preferencia, estado)
    ├─ layers[] / cameras[]
    └─ effects[]  (productores de intención con estado temporal)
         │
         ▼ planner: elige/confirma representación y traduce a intenciones
    representación:  Sprite  |  Bob  |  Cpu  |  Layer
         │
         ├── Sprite → SpriteIntent(s)  +  CopperIntent (COLOR16..31, rearm, prioridad)
         ├── Bob    → BlitJob(s) en FramePlan  +  CopperIntent (paleta/shift anclados)
         ├── Cpu    → escritura en Surface  +  DirtyRect
         └── Layer  → playfield/scroll propio (capa con scroll)
         │
         ▼ composición del frame
    [ ejecutar BlitJobs del FramePlan ] + [ copper::Plan ordena y materializa intenciones ] + [ escrituras CPU ]
         │
         ▼ backend Amiga (Agnus/Denise)
```

La separación de capas es la misma que en `VISUAL_EFFECT_SPRITE_DESIGN.md` §2: la lógica de juego emite intenciones, el engine arbitra, el backend escribe registros.

## 2. Estado de las piezas

| Pieza | Estado | Dónde |
|---|---|---|
| Elección de representación (`Representation`, `ActorTemplate`, `RepresentationBudget`, `RepresentationAllocator`, `choose_representation`) | EXISTE | `engine/include/eng/scene/representation.hpp` |
| Contenido portable (`Visual`, `VisualKind`) y vocabulario de intención (`CopperIntent`, `SpriteIntent`, concept `Effect`) | EXISTE | `engine/include/eng/graphics/raster_intent.hpp` |
| Contenido animado (`Animation`, `Frame`, `SpriteSheet`) | EXISTE | `engine/include/eng/graphics/animation.hpp` |
| Plan de Blits (`FramePlan`, `BlitJob`, `BlitJobKind`, `DirtyRect`, `BlitBudget`) | EXISTE | `engine/include/eng/graphics/frame_plan.hpp` |
| BOB de bitmap (`Bob`, `BobTarget`, `bob_draw`, `bob_erase_box`) | EXISTE | `engine/include/eng/graphics/bob.hpp` |
| Lote de BOBs OR intercalado (mismo tamaño, 1 blit/objeto, sin `jsr` por objeto) (`OrBlobBatch`, `begin/one/end`) | EXISTE | `engine/include/eng/platform/amiga/blob.hpp` (test HOST-176) |
| Construcción del BOB desde un `Visual` (`bob_from_visual`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Plantilla de sprite (`HwSpriteTemplate`, `HwSpriteSegment`, `HwSpritePaletteSwitch`) | EXISTE | `engine/include/eng/graphics/sprite.hpp` |
| Asignación de canales (`SpriteAllocator`, `SpriteSlot` con `as_bob`) | EXISTE | `engine/include/eng/graphics/sprite_allocator.hpp` |
| Emisión de sprites (`SpriteManager`) | EXISTE | `engine/include/eng/graphics/sprite_manager.hpp` |
| Orquestación de Copper (`copper::Plan`, `Scheduler`, `DoubleBuffer`) | EXISTE | `engine/include/eng/graphics/copper/` |
| Buffers de display (`scene::compose`, `SceneResources.buffers`) | EXISTE | `engine/include/eng/graphics/composition/compose.hpp` |
| Superficie de dibujo (`Surface`, `SurfaceRect`) | EXISTE | `engine/include/eng/field/surface.hpp` |
| Telemetría (`FrameTelemetry`, `RunStatus`) | EXISTE | `engine/include/eng/debug/run_status.hpp` |
| Estado de actor retenido (`Actor`, `ActorStore<Max>` con handles generacionales) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Descriptor de actor con políticas (`ActorDesc`: transparencia, fondo, Copper, anclaje) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Transparencia y fondo traducidos a Blitter (`transparency_plan`, `resolve_background`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Emisión del actor (borrado/restauración + dibujo + Copper anclado) | EXISTE | `actor_emit`, `actor_emit_copper` (`scene/actor.hpp`) |
| Anclaje y offset por actor, animación con velocidad entera | EXISTE | `actor_screen_rect`, `actor_tick` (`scene/actor.hpp`) |
| Traslación al bitmap-anillo | EXISTE | `ring_physical` (`scene/actor.hpp`) |
| Recorte parcial del objeto a la ventana | PROPUESTO | el emisor rechaza el objeto que no cabe entero (hace falta máscara de fin de línea) |
| Resolución de conflictos de Copper por `(superficie, z)` | EXISTE | `copper::Plan::add_prioritized` (orden ascendente por prioridad dentro de cada línea: la última escritura manda) |
| Política de save-under por buffer | EXISTE | `emit_save`/`emit_restore` por buffer (`scene/actor.hpp`), solo con destino planar |
| Anclaje por frame distinto (hot-spot variable entre frames) | PROPUESTO | hoy el anclaje es por actor |
| Superficie destino y orden `z` **por superficie** (`ActorDesc::surface`, `ActorEmitContext::targets`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Prioridad de sprite frente a los playfields (`ActorDesc::sprite_priority`, `SpriteIntent::priority`) | EXISTE | `scene/actor.hpp`, `graphics/raster_intent.hpp` |
| Proyección de un actor al camino de sprite (`actor_to_sprite_intent`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Playfield como capa: fondo de DPF o bitmap suelto (Soft DPF) | PARCIAL | `Representation::Layer`; la composición de superficies se especifica en `PLAYFIELD_SCROLL_ARCHITECTURE.md` |
| Orden de emisión por superficie y `z` (`plan_actor_order`, `emit_actors_in_order`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Tiras horizontales de sprite (canales contiguos, `strip_id`/`strip_index`/`strip_span`) | EXISTE | `engine/include/eng/graphics/sprite_allocator.hpp` |
| Proyección de plantilla a intenciones (`sprite_template_to_intents`, `SpriteIntentSet`) | EXISTE | `engine/include/eng/graphics/sprite.hpp` |
| Intenciones de sprite de los actores y reparto (`build_sprite_intents`, `actor_to_sprite_intent`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Degradación sprite → BOB (`emit_bob_fallbacks` sobre `SpriteSlot::as_bob`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Composición de sprites del frame (`compose_sprites`, `SpriteComposeScratch`, `SpriteComposeResult`) | EXISTE | `engine/include/eng/scene/actor.hpp` |
| Contrato del sprite resuelto (`HwSpritePlacement`) y volcado al emisor (`SpriteManager::apply`) | EXISTE | `graphics/sprite.hpp`, `graphics/sprite_manager.hpp` |
| Franjas de sprite y rearme intra-scanline (Risky Woods / Jim Power) | PARCIAL | proyección de franjas/rearme/paleta hecha; falta conectarla a la emisión real del compositor |
| Tiles como BOB (blit desde banco común + posición de mapa) | EXISTE | `BlitJobKind::TileBlockCopy` (`frame_plan.hpp`), `field/xlimited.hpp` |
| Objeto CPU sobre `Surface` con política de fondo | PROPUESTO | §14.7 |

## 3. Modelo de dominio

El estado de un actor se separa en tres bloques con ciclos de vida distintos: **contenido** (inmutable, cocinado), **estado por objeto** (lógica) y **estado por buffer de display** (presentación).

```text
Actor (id estable con generación)
  ├─ contenido (referencia inmutable, cocinada en Chip/Fast)
  │    ├─ Visual            (pixels + mask + w/h/planos)             [EXISTE]
  │    ├─ Animation + State (frames con ticks, avance por tick)      [EXISTE]
  │    └─ CopperIntent[]    (necesidades ancladas al actor)          [EXISTE]
  ├─ estado por objeto (lógica, independiente del buffer)
  │    ├─ posición de mundo, preferencia de representación           [EXISTE]
  │    ├─ superficie destino (`surface`) y orden `z` DENTRO de ella  [EXISTE]
  │    ├─ prioridad de sprite frente a los playfields                [EXISTE]
  │    ├─ anclaje (hot-spot) y offset (shake/recoil)                 [EXISTE]
  │    ├─ modo de transparencia y política de fondo                  [EXISTE]
  │    └─ representación actual (la que eligió el planner)          [parcial]
  └─ estado por buffer (N = 1..3 de `scene::compose`)
       └─ rectángulo anterior (save-under) por buffer trasero        [EXISTE]
```

Invariantes:

- El identificador de un actor es **estable** hasta su destrucción; el slot se recicla con un contador de generación para invalidar handles viejos.
- El estado **por objeto** no depende del buffer; el estado **por buffer** (el rectángulo previo del save-under) sí, porque cada buffer contiene un frame distinto y hay que restaurar el fondo de *ese* buffer.
- **`z` no es global**: ordena la superposición **dentro de una misma superficie** (`surface`). Dos objetos dibujados en playfields distintos no compiten por `z`, porque los superpone la prioridad de playfields del chipset.
- Los **sprites** no compiten por `z`: su orden es la prioridad de hardware entre canales y frente a los playfields (`sprite_priority`).
- Ninguna estructura de actor guarda punteros a registros, copperlist ni direcciones DMA.
- Todo el estado tiene **capacidad fija** y se consulta antes de saturar (modelo de ocupación de `SCENE_AND_RESOURCES.md`).

## 4. Elección y degradación de representación

La representación actual se expresa con `eng::scene::Representation`: `Cpu`, `Sprite`, `Bob` y `Layer`. La aplicación solo aporta una **preferencia** dentro de `ActorTemplate` (`preferred`, `priority`, `scrolls`, geometría y planos); el `RepresentationAllocator` elige y consume recursos con `choose_representation`, cuyo orden es `Layer > Sprite > Bob > Cpu`.

Reglas de elegibilidad ya codificadas:

- Un sprite requiere `width <= 16` y `height <= 32` (`fits_sprite`, límites `kSpriteMaxWidth`/`kSpriteMaxHeight`) y un canal libre.
- Una capa (`Layer`) requiere `scrolls` y un slot de capa disponible. Una capa es un **playfield con scroll**: lo habitual es que sea el fondo de un DPF (uno de los dos campos), pero también puede ser un **bitmap suelto** compuesto por software (el "Soft DPF" estilo RoboCod), y en ese caso es simplemente una superficie más de la composición.
- Los **BOB y objetos CPU** no están atados a un playfield concreto: `ActorDesc::surface` elige en cuál se dibujan, de modo que en un DPF unos objetos pueden ir al fondo (PF1) y otros al frente (PF2) con la misma descripción y distinto `surface`.
- El BOB consume presupuesto de Blitter (hoy una heurística de palabras por fila; el refinamiento es contabilizar `palabras_por_fila * planos * alto` con el `BlitBudget` real).
- Si nada cabe, la representación es `Cpu`.

La cascada de degradación es **transparente para la aplicación**: sprite sin canal o sin DMA → BOB; BOB sin presupuesto de Blitter o de Chip RAM → CPU; CPU sin capacidad → el alta falla con diagnóstico (nunca corrupción silenciosa). La aplicación puede consultar la representación efectiva y el presupuesto, pero no cambia su descripción.

## 5. Transparencia y mezcla

El modo de transparencia es una **política paramétrica**, no un tipo de objeto distinto. Cada modo se materializa con un mecanismo concreto según la representación:

| Modo | Sprite hardware | BOB (Blitter) | Objeto CPU |
|---|---|---|---|
| `Opaque` | color opaco en los índices usados | minterm `$F0` (`D = A`) o copia | escritura directa |
| `ColorKey0` | transparencia nativa de sprite (índice 0 siempre transparente) | requiere máscara derivada del índice 0 | comparación por índice en el bucle |
| `Mask1Bit` (cookie-cut) | no aplica (la forma es del sprite) | minterm `$CA` (`D = A·B + ¬A·C`) con plano de máscara (`BlitJob::mask`) | máscara de 1 bit |
| `AdditiveOr` (glow) | OR de canales de sprite | minterm `$FC` (`D = A \| D`), kind `OrBlob` | OR de words |

Notas de diseño:

- El **minterm es un campo del `BlitJob`** (`BlitJob::minterm`, por defecto `$CA`), así que el mismo camino de ejecución sirve para cookie-cut, OR, copia y borrado; no se multiplican los tipos de job ni las ramas de la aplicación.
- El **layout** del destino también es un campo explícito (`BlitJob::interleaved`): con planos intercalados, un objeto es **un solo blit** con `height = alto × planos`; con planos contiguos son N blits (uno por plano).
- La **máscara** del cookie-cut puede venir de dos formas (`Bob::mask_pack`, `graphics/bob.hpp`): en un **plano aparte** (`SeparatePlane`, el camino planar) o **intercalada por pares** `[máscara][imagen]` en cada fila de cada plano (`InterleavedPair`), que resuelve el cookie-cut con destino intercalado en **un solo** blit `$CA` (kind `MaskedBobCookieCut`) sin materializar una máscara expandida; es la forma del BOB de la demo 213.
- La transparencia se combina con el **orden de dibujo dentro de la superficie**: los BOB y objetos CPU de un mismo playfield se emiten de atrás hacia delante por `z` (estable, y solo entre objetos de esa misma superficie). Los sprites no entran en ese orden: se superponen por su prioridad de hardware, que además decide si van delante o detrás de cada playfield (`sprite_priority`).
- Un sprite multiplexado reutiliza el canal y, por tanto, sus registros `COLOR16..31`: los cambios de paleta de dos objetos que compartan canal deben respetar el par N/N+1 o degradarse (ver §7).

### Regla obligatoria: BOB ≠ polígono

La regla del chipset que hay que preservar: **un BOB es una COPIA de bitmap**, y **no** un polígono.

- **Un BOB** se dibuja copiando un bitmap pre-renderizado (planos + máscara) con el barrel shifter y el minterm adecuado: cookie-cut `$CA` (`D=A·B+¬A·C`, transparencia) o **OR** (`$FC`, `D=A|D`, bobs aditivos/glow). Con **planos intercalados** es **un solo blit por objeto** para todos los planos (`height = alto_bob × planos`, ver §5); con planos contiguos son N blits. Es el camino barato (~1 blit/objeto).
- **Un polígono del Blitter es otra cosa**: line-draw (`BLTCON1` LINE, XOR/SING) + **area fill** (`IFE`/`EFE`, descendente) para rellenos **vectoriales** (caras 3D, `blitter_fill_polygon`). Es por polígono y por plano, con setup caro: **no se usa para objetos**.
- No confundirlos: mezclarlos (p. ej. rellenar un disco con `blitter_fill_polygon` para hacer un BOB) cuesta ~4 blits + máscara por objeto y **satura el bus** (caso real: la demo 085 pasó de 25 a 3 campos/frame; ver su README).
- Referencias: AHRM 3.ª (cap. Blitter), `amiga-bootcamp/08_graphics/blitter_programming.md` (minterms, cookie-cut, *Use Case 4: interleaved bitplane BOBs*), `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md` (receta del polígono relleno) y `demoscene-repo-orig/effects/bobs3d/bobs3d.c` (OR-bobs intercalados, 1 blit, clear en 1 blit).
- **Antes de programar cualquier cosa de Blitter/objetos/sprites**: leer la referencia oficial (AHRM en `docs/reference/ahrm/`), la secundaria (`blitter_programming.md`) y un ejemplo ajeno (`bobs3d.c`, demos 050/051/053/054).

## 6. Gestión del fondo

La política de fondo decide cómo se deja limpio el rastro del objeto al moverse. Es una política por actor con un modo automático que el planner resuelve:

| Política | Mecanismo | Coste | Requisitos |
|---|---|---|---|
| `None` | la escena se repinta entera cada frame | 0 blits extra | fondo de coste despreciable o estático |
| `ClearRect` | un blit sin fuentes (`BlitJobKind::ClearRect`, minterm `$00`) sobre la caja previa | 1 blit (o N por planos) | fondo **liso** bajo el objeto |
| `SaveUnder` | copiar el fondo antes de pintar (`CopyRect`) y restaurarlo al mover (`RestoreRect`) | 2 blits + memoria de guardado | área pequeña y memoria de Chip RAM por actor y **por buffer** |
| `DirtyRect` | registrar la unión de rectángulos y repintar solo esa región | depende del repintado | el fondo sabe redibujarse por región |
| `Auto` | elige entre las anteriores por área y presupuesto de Blitter | — | heurística por umbral |

Puntos clave del save-under:

- El buffer de guardado se dimensiona con el **rectángulo efectivo** (`posición − anclaje + offset`, con el `w × h` del frame actual), y se mantiene **uno por buffer de display**: al restaurar hay que devolver el fondo del buffer que se va a reutilizar.
- Si el frame cambia de tamaño o de anclaje, el rectángulo anterior guardado manda: no se puede asumir geometría constante.
- `RestoreRect` y `CopyRect` ya existen como kinds del plan; el save-under es una **secuencia de jobs**, no un mecanismo nuevo.

## 7. Necesidades de Copper

Un objeto no escribe registros: **declara** `CopperIntent` (vocabulario de `raster_intent.hpp`) y el compositor las ordena. Las intenciones cubren cambio de paleta por línea (`PaletteLine`), cambio a mitad de línea (`PaletteSpan`), desplazamiento fino por línea (`ShiftLines`), reparto de planos a media pantalla (`BitplaneSplit`), rearme de sprite (`SpriteRearm`) y prioridad (`Priority`).

Prioridad del sprite frente a los playfields: un sprite hardware puede quedar **delante o detrás** de cada playfield según la prioridad de `BPLCON2` (y ordenarse entre canales por su propia prioridad). El actor la declara en `sprite_priority` (0..3) y el compositor la materializa con la intención `Priority`. No se confunde con el `z` de los BOB: `z` ordena objetos **dentro de un mismo playfield**; `sprite_priority` sitúa el sprite en la pila de prioridades del chipset.

Reconfiguración intra-scanline: un sprite se puede **reapuntar mientras avanza el haz**. La plantilla declara franjas (`HwSpriteSegment`) con su altura y su desplazamiento dentro de la imagen, y los puntos de rearme (`SpriteRearm`), los cambios de posición (`hpos_delta`) y los cambios de color (`HwSpritePaletteSwitch`) se convierten en intenciones que el compositor emite en la línea que toca. `sprite_template_to_intents` hace esa proyección sin escribir registros: una `SpriteIntent` por franja (con el tramo que le toca tras el gap de 1 línea del DMA), un `SpriteRearm` por franja a partir de la segunda y una `PaletteLine` por cada cambio de paleta dentro del tramo. Con eso se construyen los fondos de sprites tipo Risky Woods o Jim Power. La composición **horizontal** (varios tramos contiguos en la misma línea) se hace con **varios canales** cubriendo tramos uno al lado del otro: un solo canal no puede aparecer dos veces en la misma línea, porque su *fetch* se resuelve al principio de la línea. El modelo lo expresa como plantilla más lista de franjas; cuántos canales contiguos se pueden sostener lo decide el `SpriteAllocator`.

Anclaje al objeto: las intenciones de un actor se declaran **relativas a su Y** (o a su Y de pantalla) y el planner las convierte a líneas absolutas sumando la posición efectiva. Así un degradado de paleta «viaja» con el objeto sin que la aplicación calcule la línea del raster.

```text
actor (y de pantalla = 84)                      líneas absolutas
  CopperIntent PaletteLine  rel[ 0.. 8)   ──►    [ 84.. 92)
  CopperIntent PaletteLine  rel[ 8..16)   ──►    [ 92..100)
  CopperIntent Priority      rel[16..20)  ──►    [100..104)
```

Fusión y conflictos, en orden de prioridad decreciente: intenciones del **frame actual** del actor, después las del **actor** y después las de la **capa**. Si dos intenciones escriben el **mismo registro en la misma línea**, gana la del actor con mayor `z` dentro de la **misma superficie**; entre superficies distintas decide el orden de las superficies, y en empate final el identificador menor (orden determinista). El `Plan` lo resuelve con `add_prioritized(intents, count, surface, z)`: ordena por prioridad **ascendente** dentro de cada línea y emite la de mayor prioridad la última, de modo que en el Copper manda la última escritura. La escena lo cablea con `actor_add_copper` (que pasa `(surface, z)` del actor) o con `compose_sprites(..., plan)`. Alcance actual: cada intención se materializa como un **punto en `top`** (el scheduler no usa `bottom`), así que el espacio de conflicto es exactamente la misma línea y queda resuelto por completo; si en el futuro se quieren tramos de varias líneas, habrá que expandirlos por línea antes de ordenar, con el coste de memoria correspondiente (cuadrar con `max_intents`). Las reglas específicas que el compositor debe hacer cumplir:

- `copper::Plan` ordena por scanline relativo al inicio del display, con ordenación estable y sin que importe el orden de alta.
- Un `SpriteRearm` (multiplexado) exige recargar `SPRxPT` al principio del VBL; el `SpriteManager` es el emisor, no el actor.
- Los cambios de paleta de un canal de sprite afectan al **par** de canales: dos objetos que compartan canal no pueden tener paletas distintas en líneas solapadas; en ese caso se degrada el de menor z a BOB.
- Las intenciones a mitad de línea (`PaletteSpan`) solo caben en el H-Blank residual: si no caben, se reportan como desbordamiento (nunca se recortan en silencio).
- Un desbordamiento del plan de intenciones se traduce en un **rechazo controlado** con diagnóstico, no en una copperlist corrupta.

## 8. Algoritmos

### 8.1 Asignación y multiplexado de sprites

`SpriteAllocator::assign` implementa **greedy first-fit con multiplexado vertical**: recorre los intents ordenados por `top` ascendente y asigna el primer canal cuyo último uso terminó en `<= top` (el `bottom` es exclusivo, de modo que dos sprites contiguos comparten canal y uno que arranca en la línea 0 también encuentra canal); si no hay canal, marca `SpriteSlot::as_bob`. Es O(n·8) sobre una lista ordenada (el llamador ordena; el asignador no reserva memoria).

**Tiras horizontales** (`SpriteIntent::strip_id`, `strip_index`, `strip_span`): un objeto más ancho que 16 px se compone con varios canales contiguos. El líder (`strip_index == 0`) reserva la primera corrida de `strip_span` canales libres en su franja y los miembros toman `base + strip_index`; si no hay corrida del tamaño pedido, o la tira no llega con el líder primero, la tira **entera** va a `as_bob` (nunca se parte a medias). Es lo que habilita los fondos de sprites uno al lado del otro descritos en §7.

Refinamientos previstos, sin cambiar el contrato:

- Consumir el presupuesto con el **ancho real** (`width_words`) y contemplar los pares `attach` (32 px o 15 colores), hoy no modelados.
- Contabilidad explícita por línea del **ancho de DMA** de sprites, además del número de canales.
- Empaquetado **creciente por línea** en lugar de first-fit global (mejor reutilización en escenas densas), manteniendo el determinismo.

### 8.2 Orden de dibujo (superficie y z)

El orden de emisión fija la superposición de BOBs y objetos CPU. La clave es `(superficie, z, índice de slot)`: primero todos los objetos de la superficie 0, después los de la 1, y dentro de cada superficie de atrás hacia delante por `z`, con desempate por índice (determinista). `plan_actor_order` lo resuelve con **ordenación por inserción** (sin heap) sobre una lista de capacidad fija y rechaza el frame si el número de actores no cabe en el buffer del llamador; `emit_actors_in_order` emite en ese orden. Los sprites no entran en este criterio: se superponen por su propia prioridad de hardware (§7).

### 8.3 Traslación al bitmap-anillo

Con un playfield en anillo (bitmap mayor que el viewport, con scroll circular), un BOB vive en un bitmap **físico**: hay que traducir la posición lógica a la dirección física del anillo.

```text
physical_x = (screen_x - anchor_x + scroll_x) mod ring_w
```

Con `ring_w` potencia de 2 el módulo se resuelve con una máscara (sin división); con otro tamaño (p. ej. 320) se paga `__divsi3`, así que conviene dimensionar el anillo a una potencia de 2 en rutas calientes. Si el rectángulo efectivo **cruza el módulo** del anillo, se emiten dos blits (o uno con la altura partida). Nunca se asume que el objeto es contiguo en memoria. La misma lógica de partición sirve para las **ventanas/splits** (`BitplaneSplit`, `ShiftLines`) de una composición estilo X-Limited: el objeto se recorta por scanline contra la ventana correcta, con rectángulos de clip precalculados por banda.

### 8.4 Clip

El recorte es siempre intersección de rectángulos enteros con bordes exclusivos (`DirtyRect`), contra la ventana, el borde del bitmap y la guarda del anillo. Sin divisiones ni módulos en el bucle: las coordenadas de word se obtienen desplazando (`>> 4`) y el desplazamiento fino lo absorbe el barrel shifter (BOB) o el propio bucle (CPU).

### 8.5 Save-under

Por frame y por actor con política `SaveUnder`: restaurar el rectángulo previo de **este** buffer (`RestoreRect`), copiar el fondo nuevo (`CopyRect`) y solo entonces dibujar el objeto. El rectángulo previo se actualiza por buffer. En `Auto`, si el área del objeto por planos supera un umbral o el presupuesto de Blitter está en aviso, se degrada a `ClearRect` o `DirtyRect`.

### 8.6 Los tiles también son BOBs

Un tile es, para el hardware, **una copia de bitmap en una rejilla**: la misma geometría que un BOB (origen en un banco compartido, destino alineado, desplazamiento fino por palabra, máscara opcional), con dos diferencias de gestión: la fuente es un **banco común** (`Tileset`/`TileSource`) y su posición sale de un **mapa** en vez de una lista de actores. El motor ya lo trata así en el camino de blits (`BlitJobKind::TileBlockCopy`, y los `add_draw`/`add_world_bitmap` del campo de tiles).

Consecuencia para este diseño: el algoritmo de scroll por tiles de X-Limited no es un sistema aparte, sino un **emisor masivo de BOB** que comparte el `FramePlan`, el presupuesto de Blitter, el orden dentro de la superficie y las reglas de módulo/guarda del anillo. Lo que cambia es quién decide qué se dibuja (el campo de tiles, por celdas del mapa) y que su emisión es por lotes y con su propio criterio de reuso (franjas, prefetch), no un `actor_emit` por objeto.

Regla práctica: cuando una entidad se pueda describir como "imagen de un banco, posición entera en pantalla, copia por Blitter", debe emitir `BlitJob`s por el mismo camino que un BOB, aunque su origen sea un mapa y no un `Actor`.

### 8.7 Capa declarativa de BOBs (`BobLayer`)

`eng/scene/bobs.hpp` es la capa de juego sobre `bob_draw`: una **hoja** homogénea (`graphics::Sprite`) y un vector fijo de **actores** (`BobActor`: `x`, `y`, `frame`, `visible`). El juego escribe la pose por frame y llama `layer.emit(plan, scene.bob_target())`; no nombra `BlitJob`, minterns ni strides. `scene::clear_box(plan, target, x, y, w, h)` limpia bandas/zonas del playfield con la misma geometría de borrado (un blit intercalado), sin que el juego describa un `BlitJob`. La demo 213 usa esta capa para sus 16 BOBs cookie-cut intercalados.

`scene::FastBobLayer` es la variante para **dual playfield** (PF frontal vacío): mantiene el historial de lo pintado por actor y decide, por frame, entre la **copia con padding** (`BobDraw::Opaque`, dibuja y limpia en un blit) y la degradación (**clear del área previa + cookie-cut**) cuando el actor se mueve más que el padding o su área se solapa con la de otro. El juego sigue moviendo solo actores; la técnica es una política, no un tipo de objeto (ver `docs/reference/amiga/techniques/dual-playfield-fastbobs.md` y `HOST-355`).

## 9. Memoria y presupuesto

- Contenido cocinado (`Visual.pixels`, `Visual.mask`, hojas de sprite) y buffers de save-under viven en **Chip RAM** (el Blitter y el DMA de sprite solo leen Chip).
- Estado de actores, planes de intención y telemetría pueden vivir en **Fast RAM**.
- La memoria se reparte en arenas de capacidad fija por dominio (por ejemplo `backend.memory().chip.allocate_block<Tag>(bytes, alineación)`), con los tags de `core/domains.hpp`: `PlaneTag`, `BobTag`, `MaskTag`, `PaletteTag`... Sin asignación dinámica en gameplay.
- Estimación de coste por actor (peor caso orientativo, 4 planos, 32×32, save-under, N=2): contenido ~1 KiB planar (+ máscara), estado del slot decenas de bytes, save-under ≈ 2 × (32×32×4/8) ≈ 1 KiB. Se acota en compilación y se valida con `can_add` antes de saturar.

## 10. Telemetría y degradación

La cascada de degradación (§4) se observa con contadores: canales de sprite usados y libres, palabras de Blitter consumidas y libres, número de sprites degradados a BOB, intenciones de Copper usadas y libres, y bytes de Chip RAM usados y libres. `FrameTelemetry` y el `detail` de `RunStatus` ya son el soporte; el planner expone una vista de presupuesto consultable.

La aplicación nunca ve el cambio de representación: consulta, como mucho, la representación efectiva y el presupuesto con fines de depuración o de diseño de niveles.

## 11. Pruebas

Host (deterministas, sin hardware):

- Elección de representación y degradación con presupuestos agotados (extiende el test de `representation.hpp`).
- Asignación de sprites: empaquetado por intervalos, `as_bob` cuando no cabe, independencia del orden de alta.
- Resolución de conflictos de Copper: mismo registro en la misma línea → gana el de mayor z; empate → identificador menor.
- Anclaje y offset por frame: rectángulo efectivo correcto, incluido el cambio de tamaño entre frames.
- Traslación al anillo: posición física correcta y partición al cruzar el módulo.
- Clip por ventana/split y bordes de guarda.
- Save-under: rectángulos previos correctos con N = 1, 2 y 3 buffers.
- Geometría de los `BlitJob` del BOB (ya cubierta por `tests/host/scene/072_actor`).

Demo con gate visual (secuencias, no un frame suelto, y con veredicto de visión):

- Los mismos cinco contextos del contrato de versatilidad (single EHB, doble playfield 3+3, anillo 8 direcciones con splits, bandas/varios offsets, degradado anclado más objeto aditivo solapado).
- Un actor reasignado de sprite a BOB a mitad de la secuencia, sin salto visible (misma ancla y offset).
- Un BOB con animación de formas distintas y cambios de paleta anclados a su Y.

## 12. Trampas de hardware que el sistema debe absorber

- **Barrel shifter del Blitter**: los bits desplazados fuera se reinyectan al principio de la fila siguiente; la hoja del BOB necesita una **palabra de guarda** por fila y el módulo de origen debe ser coherente con ella (`sheet_row_bytes − words_procesadas × 2`), no cero salvo cuando coinciden.
- **Módulo del Blitter**: al procesar una palabra extra por el desplazamiento hay que ajustar `BLTSIZE` (palabras por fila) y los módulos, o el objeto se deforma.
- **La caja de borrado y el save-under cubren las MISMAS palabras que el dibujo** (`base + (shift != 0)`): con desplazamiento fino el blit escribe la palabra extra del barrel shifter, así que limpiar solo `base` deja hasta 15 px por fila sin limpiar y el objeto deja rastro. La política de borrado por caja o save-under exige además que el objeto **no invada la caja de otro** (el borrado de uno taparía al vecino).
- **Transparencia = minterm**: cookie-cut `$CA`, OR `$FC`, copia `$F0`, borrado `$00`. El color de fondo no es «transparente» salvo que el minterm o la máscara lo digan.
- **Paleta de sprite**: los sprites usan `COLOR16..31`, independientes del playfield; los canales par e impar **comparten** sus 3 colores.
- **Recarga de `SPRxPT`**: el puntero de datos hay que reescribirlo cada VBL; el multiplexado se apoya en ello.
- **Anillo**: un objeto que cruza el módulo no es contiguo en memoria; se parte.
- **Intercalado frente a contiguo**: un solo blit por objeto solo es posible con planos intercalados y el layout declarado explícitamente.
- **Prioridad sprite/playfield frente a z de la aplicación**: la resolución debe ser consistente entre `BPLCON2` y el orden de emisión.
- **Coste del objeto CPU**: el Blitter hace las copias rectangulares mucho más baratas; el objeto CPU se reserva a lo que el Blitter no puede hacer (lógica por píxel, direccionamiento por bytes, dependencias), y debe entrar en el presupuesto del frame.
- **DATA de sprite frente a hoja de BOB**: un sprite hardware lee su DATA como DAT/DATB **intercalados por línea**, mientras que una hoja de BOB planar lee planos **contiguos con máscara**. El `Visual` tiene una sola vista de píxeles, así que el mismo contenido no sirve tal cual para los dos caminos: la transición sprite↔BOB exige que el pipeline de contenido cocine ambos (o que se adopte un layout canónico y el ejecutor lo soporte). Es la razón de que la degradación a BOB pueda contarse sin poder dibujarse cuando el asset solo existe en formato sprite.

## 13. Antipatrones

- Usar el camino poligonal del Blitter (line-draw + area-fill) para dibujar objetos.
- Emitir registros de Copper, Blitter o sprite desde un actor o desde la aplicación.
- Duplicar los caminos de `FramePlan`, `copper::Plan`, `SpriteAllocator` o `RepresentationAllocator`.
- Suponer que la paleta de un sprite colisiona con la del playfield.
- Asumir geometría, ancla o tamaño constantes en el save-under.
- Tipos de actor distintos por cada combinación de animación, transparencia y Copper: todo es paramétrico sobre el contenido más políticas.
- Degradar en silencio o permitir que el orden de alta altere el resultado.
- Aritmética de coma flotante, divisiones o módulos en el bucle caliente.
- API pública de tamaño o modo fijo (EHB, doble playfield, resolución): todo se parametriza en la configuración del contexto.

## 14. Piezas a implementar

Ordenadas por dependencia, dentro del roadmap F6:

1. **Estado de actor retenido** (`Actor` + `ActorStore<Max>` con handles generacionales), consumiendo `ActorTemplate`/`RepresentationAllocator` y el contenido de `Visual`/`Animation`. **HECHO** (`scene/actor.hpp`), cubierto por el test host `072_actor`.
2. **Políticas de actor** (`TransparencyMode`, `BackgroundPolicy`) y su traducción a `BlitJob::minterm`/máscara y a la secuencia de jobs de save-under. **HECHO** (`transparency_plan`, `resolve_background`, `emit_save`/`emit_restore`), con `RestoreUnder` solo para destinos planares.
3. **Anclaje y offset** por actor y animación con velocidad entera. **HECHO** (`actor_screen_rect`, `actor_tick`); el anclaje por frame distinto queda pendiente.
4. **Emisión de BOB** desde el actor: `bob_draw`/`bob_erase_box` con el rectángulo efectivo y el origen del frame dentro de la hoja. **HECHO**; el **recorte parcial** a la ventana queda pendiente (hoy se rechaza el objeto que no cabe entero).
5. **Necesidades de Copper ancladas**: conversión de relativas a absolutas. **HECHO** (`actor_emit_copper`); la fusión con prioridad por z en el `Plan` queda pendiente.
6. **Cableado de la degradación sprite → BOB**: `SpriteAllocator::as_bob` a `BlitJob` con el mismo `Visual`. **HECHO en el engine**: `build_sprite_intents` (una intención por actor, ordenada por `top`) + `emit_bob_fallbacks` (emite como BOB los degradados, en orden por superficie y `z`, con `bob_from_visual`); falta reescribir la demo 054 para consumirlo.
7. **Objeto CPU** sobre `Surface` con política de fondo y presupuesto. **PENDIENTE**.
8. **Demo con gate visual** que consuma el sistema (hoy solo hay test host): pendiente, es lo que convierte la capa en verificada según `docs/testing/README.md`.

## 15. Repaso final: clasificación de abstracciones, fronteras y huecos

Esta sección fija **qué es cada pieza** (framebuffer, vista, descriptor, algoritmo, emisión), **cómo se gestiona el Copper** y **cómo se comparte el Blitter** (incluida la GUI), para que la separación no deje huecos ni ambigüedades.

### 15.1 Clasificación

```text
  ALGORITMO/ESTADO        VIEWPORT/SECTOR (vista)      FRAMEBUFFER (dueño)        EMISIÓN
  ─────────────────       ──────────────────────       ───────────────────        ───────
  Camera2D (scroll)  ─┐
  TileScrollDriver    ├─► Surface (Playfield+clip)    Bitmap (bloque+geom)  ─►  BlitJob
  FineScroll          │   DrawTarget (+raster+plan)   Playfield (mapeo)         FramePlan (cola+presupuesto)
  Palette*/RasterGrad  │   Screen (contexto)           Scene (bitplanes+copper)  CopperIntent
  SpriteAllocator     │   BobTarget (geom. destino)                            ─► copper::Plan (listas)
  RepresentationAlloc ┘   ActorEmitContext (targets)                             copper::Scheduler (emisor)
                          Layer/Camera2D (ventana)                               copper::Timeline (presupuesto)
```

- **Framebuffer (dueños de memoria)**: `MemorySystem`/`Block`, `eng::gfx::Bitmap`, `field::Playfield` (+ derivados), `composition::Scene`.
- **Vistas/sectores (no poseen)**: `field::Surface`, `field::DrawTarget`, `Screen`, `BobTarget`, `scene::ActorEmitContext`, `scene::Layer`/`Camera2D`, `copper::BandScope`.
- **Descriptores de contenido**: `graphics::Visual`, `graphics::Sprite` (BOB cocinado), `graphics::Bob` (crudo), `HwSpriteTemplate`/`HwSpritePlacement`, `SpriteIntent`, `BlitJob`, `CopperIntent`.
- **Algoritmos**: `Camera2D`, `TileScrollDriver`/`FineScroll`, `PaletteTransition`/`PaletteCycle`/`RasterGradient`/`Rotozoom`, `SpriteAllocator`, `RepresentationAllocator`, `copper::Timeline`/`Plan`, `FramePlan`, `Animation`.
- **Retenido/planner**: `Actor`/`ActorStore`, `World`/`Layer`, `SceneResources`/`DisplayLimits`/`compose`.

### 15.2 Tabla de responsabilidades

| Pieza | Clase | Qué es |
|---|---|---|
| `gfx::Bitmap` | framebuffer | bloque + geometría + layout + addressing |
| `field::Playfield` | framebuffer | mapeo lógico→físico + rasterizer + sinks de relleno |
| `composition::Scene` | framebuffer/planner | posee bitplanes + copperlist + buffers; ciclo |
| `field::Surface`/`DrawTarget` | vista | `Playfield`+clip (+raster+plan); primitivas |
| `Screen` | vista/contexto | contexto de dibujo de juego |
| `BobTarget`/`ActorEmitContext` | vista | geometría de destino / targets+clip+cam |
| `Camera2D`/`Layer`/`WorldRect` | vista+algo | ventana al mundo (scroll) |
| `Sprite`/`Bob`/`Visual`/`HwSprite*` | descriptor | contenido dibujable (BOB/hardware/tile/rect) |
| `BlitJob`/`FramePlan` | emisión | trabajo de Blitter y su cola/presupuesto |
| `CopperIntent`/`copper::Plan`/`Scheduler` | emisión | intención y lista de Copper |
| `Camera2D`/`TileScrollDriver`/`FineScroll` | algoritmo | scroll |
| `PaletteTransition`/`Cycle`/`RasterGradient` | algoritmo | color/raster |
| `SpriteAllocator`/`RepresentationAllocator` | algoritmo | reparto/representación |

### 15.3 Copperlist

`copper::Plan` es **dueño** de la(s) lista(s) (doble buffer); `copper::Scheduler` es el **emisor tipado** (`move`, `move_bitplane_pointer`, `wait_line/_position`, `emit_palette[_zone]`); `copper::Timeline` da el presupuesto por línea; `copper::static_plan`/`double_buffer` los casos fijos. Nadie escribe `$DFFxxx` a mano. `Scene` posee el `Plan` y orquesta (`begin_build/end_build`, `takeover/present/commit/flip`); un juego con su propio `Plan` usa `app.device().takeover_copper/commit_copper`. Las `CopperIntent`/`HwSpritePlacement` se materializan en el `Plan` (que ordena y respeta bandas/presupuesto).

### 15.4 Subsistema gráfico y primitivas

`Screen` (`app.screen()`) es la **API de dibujo**: `fill/line/frame/text/sprite/erase_sprite/blit/c2p`. Internamente `DrawTarget`→`Surface`→`Rasterizer` (seam CPU/Blitter) + `FramePlan` + `BobTarget`. Los **sinks** `RectFillSink`/`PolygonFillSink` (instalados por el backend) convierten rellenos en jobs de Blitter. La ejecución la hace `app.device().execute_frame_plan(...)` (o `blitter_*`), serializada con la ventana segura del Copper.

### 15.5 GUI acelerada por Blitter

`eng::ui` (`Context`/`Painter`/`Compositor`/`backing`/`double_buffer`/`HardwareCursor`) dibuja sobre `Surface`; los rellenos de widgets van por `RectFillSink` (Blit D-only) y las formas por `PolygonFillSink`, el texto por `GlyphCache`+blits. Todo **encola en el mismo `FramePlan`** que sprites/BOBs → **un solo Blitter serializado**. Las **paletas** son compartidas (`Palette`/`Palette32` + parches de `FramePlan`); los **recursos** salen de `MemorySystem`/`res::load` (backing como `Bitmap`). El reparto ordenado lo garantizan el `FramePlan` (presupuesto) y el `copper::Timeline` (bandas de efectos).

### 15.6 Huecos detectados y resolución

| Hueco | Resolución |
|---|---|
| Tres descriptores de objeto (`Visual`/`Sprite`/`Bob`) y `Sprite` no integrado en `ActorStore` | **Resuelto (F4a)**: `Sprite` declara `sheet_bytes`/`mask_bytes` y expone `visual()`; `actor_desc_from_sprite` une `add_actor`/`screen.sprite` (HOST-335, gate 214) |
| Tiles (`VirtualScene`/`TileLayer`) fuera de `World`/`Layer` | **Resuelto (F4b, modelo)**: `World` capas con contenido (actores o tilemap vía `TileLayer`, HOST-336); la materialización de capas es el planner (F4c) |
| UI con compositor propio, no es una `Layer` | Composición vía `Surface`+sinks; integrar como capa/efecto cuando haya planner |
| Copper por objeto aún a mano (086) | Subir `CopperIntent`/`actor_add_copper` a la fachada |
| `Screen` vs `Surface`/`DrawTarget` (solape) | F3b: `Screen` única; el resto internos |
| `eng::gfx::PlaneLayout` homónimo | Aliasar a `eng::graphics::PlaneLayout` (`Separate = Contiguous`) |
| `World` sin planner; `emit` recibe `BobTarget` | Planner de capas + `World::present(Screen&)` |

### 15.7 Reglas de frontera (no romper)

1. **Un dueño de framebuffer**: solo `Scene`/`Bitmap` reservan; las vistas (`Surface`/`Screen`/`BobTarget`) no.
2. **Un emisor por coprocesador**: `FramePlan` (Blitter) y `copper::Plan` (Copper); nadie más.
3. **El juego no ve hardware**: solo `App`/`Screen`/`World`/`Device` (gate `api-facade`).
4. **Descriptor único de objeto**: `Visual` es la intención; `Sprite` la cocina; no multiplicar tipos.

### 15.8 Capas declarativas y planner: «la capa pide, el planner dispone»

`Layer` **no es** un tipo de playfield: describe identidad, profundidad, cámara, **contenido** (actores o tilemap), el **scroll pedido** (`ScrollKind`) y el **playfield preferido** (`LayerPlayfield`). La **técnica de cada región** es **genérica**: no es solo scroll, sino **modo de display × scroll**, con **coste declarado** (`region_cost` → `RegionCost`: palabras de Copper/línea, planos, palabras de Blitter, uso de sprites). El **planner** (F4c) materializa: asigna cada capa a `(PF1|PF2 × región)` o a la capa de sprites, valida y **degrada** si no cabe.

`SceneMode` (`Standard`/`Ham`/`Ehb`/`DualPlayfield`/`CopperChunky`) y `ScrollKind` (`None`/`Fine`/`BlitterColumns`/`CopperRing`/`CopperSplit`) se combinan por región: así los **48 px inferiores** pueden ser **copper-chunky** (`mode=CopperChunky`, `planes=0`) mientras el resto es un **DPF con scroll por Copper** (`CopperRing`); y un playfield suelto puede usar **scroll por columnas de Blitter** (técnica tipo *robocod*, demo 112). El chipset tiene **2 playfields** + **8 sprites**; más capas se logran con **regiones verticales** (`WorldRegion {top,bottom,playfield,mode,scroll,planes}`, cambio por Copper en `top`) y con sprite-layers. Los costes **no son gratis**: `CopperSplit` usa *split por línea* (Copper), `CopperRing` usa `BPLxPT`/módulo, `BlitterColumns` usa Blitter, `Fine` usa `BPLCON1`. El planner aplica reglas como **una `CopperSplit` por banda** y degrada `CopperSplit→CopperRing→Fine` con `ConfigError`/`config_error()`. La capa nunca elige registros ni modo.
