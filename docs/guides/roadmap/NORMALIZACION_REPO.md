# Normalización del repositorio (roadmap)

Plan por fases para dejar **un único dueño por responsabilidad** tras la mezcla de ramas,
eliminar desarrollos duplicados y marcar lo que no está verificado. El modelo objetivo de
buffers y copper está en `docs/engine/architecture/DISPLAY_COMPOSITION.md`.

## Principios de trabajo

- **Por partes**: una fase = un commit (o unos pocos) con su **gate** propio; si el gate
  falla, se revierte la fase, no se acumula deuda.
- **Gate por fase**: suite host + regresión de las demos afectadas + gate visual/secuencia
  cuando toque render.
- **Nada sin consumidor**: una API del engine sin demo ni test se marca **NO VERIFICADA**
  y no se documenta como validada.
- **No duplicar**: si algo existe, se generaliza o se sustituye; nunca se añade una variante
  paralela.

## Fases

### F0 — Higiene y verdad documental (sin cambio funcional)

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 0.1 | Arreglar la rama no-ASM de la 080 (`m_scene[active]` sin declarar) | `demos/amiga/080_fire_rgb/src/main.cpp:361` | **hecho** (`418dc52`) |
| 0.2 | Corregir el comentario: `XlimitedDisplayComposer` **reemite** la lista completa, no parchea 13 words | `engine/include/eng/field/xlimited.hpp:1488-1491` | **hecho** |
| 0.3 | Marcar `DoubleBufferedHiddenMargins` / `TileScrollStrategy::double_buffered` como **política no implementada** | `engine/include/eng/scene/virtual_scene.hpp:168,186` | **hecho** |
| 0.4 | Test host de `DoubleBufferScrollPlayfield` (2 bitmaps, `flip()`, `hardware_view()` del delantero) | `tests/host/068_double_buffer_scroll` | **hecho** |
| 0.5 | Test host de `TileScrollScene` (geometría de la lista + alternancia de los 2 bloques + 13 words parcheadas) | `tests/host/069_copper_double_buffer` | **hecho** |
| 0.6 | Crear los documentos canónicos de F0: contrato (`DISPLAY_COMPOSITION.md`) y roadmap (este) + enlazarlos en los índices | `docs/engine/architecture/`, `docs/guides/roadmap/` | **hecho** |
| 0.7 | Re-medir y corregir las cifras de fps de `BITACORA_SCROLL_TILES.md` (101/102/103/104) o indicar su contexto de medida; hoy 103 mide 32,7 y no 50 | `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` | **investigado, pendiente de bisect** (ver abajo) |
| 0.8 | **Demo canónica rota**: 107 (corkscrew 8-way) **no alcanza READY**. Estado: la demo escribe su marcador previo a `scene.begin` (`detail=0x107ab`) y luego **muere antes de la primera instrucción de `XlimitedScene::begin`** (instrumentado el `begin` con marcas de paso en el engine: ninguna se ejecutó, ni con build limpio). PC clavado en ROM de Kickstart y marcador superviviente → **guru sin reboot**. A/B con y sin F1.3 → pre-existente. Hipótesis: fallo en la **entrada/llamada** (desbordamiento del marco de pila o arena corrupta antes), no en el cuerpo de `begin`. Requiere sesión GDB (breakpoint en la entrada + volcado de pila); la instrumentación temporal ya se retiró | `demos/amiga/107_xlimited_corkscrew`, `engine/include/eng/field/xlimited_scene.hpp` | **localizado, pendiente (no es de un turno)** |
| 0.9 | **Triaje runtime de demos**: barrido de salud con `tools/analyze/sweep-demo-health.sh` → **53 OK, 0 FAILED, 3 TIMEOUT (070, 071, 107), 4 NOEXE (assets sin construir: 072/073/074/076)**. El build da 0 FAIL; las "rotas" son runtime y son 3, no "muchas" | todas | **hecho** |

Gate: suite host + encoding. Sin cambios de comportamiento, así que no exige regresión de demos.

### F1 — `CopperDoubleBuffer` (una sola implementación del doble buffer de copperlist)

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 1.1 | Extraer `eng::copper::DoubleBuffer` (2 bloques + `flip` + `install`/`takeover` + `inactive_scheduler`), con soporte de **handles** de parcheo en `Scheduler` (`move_at`/`patch_data`) | `engine/include/eng/graphics/copper/double_buffer.hpp`, `copper/scheduler.hpp` | **hecho** |
| 1.2 | Usarlo en `TileScrollScene`, sustituyendo los offsets cableados (`words[5]`, `words[21+4p]`) por handles | `engine/include/eng/graphics/drivers/tile_scroll.hpp` | **hecho** |
| 1.3 | Usarlo en `XlimitedDisplayComposer` y `XlimitedDualComposer` (y decidir `Patch` vs `Reemit` con dato de coste) | `engine/include/eng/field/xlimited.hpp:1739,1928` | **hecho** (política sigue `Reemit`) |
| 1.4 | Documentar la receta «2 bloques + install tras VBlank, nunca COPJMP1» en `C2P_BLITTER.md`/`GRAPHICS_DRIVERS.md` | `docs/engine/architecture/` | pendiente |

Gate F1.2: demos `101/103/104/105` idénticas (analyze + fps). Gate F1.3: demos
`107/110/111/112/120/121/201/202`.

**Resultado de F1.3**: migrados los dos compositores. Verificado: **202** READY + 26,12 fps;
**112** READY + 8,29 fps y **gate de visión de secuencia limpio** (5 frames); 201/120/121
compilan; 110/111 pendientes de ejecutar. **107 no se puede validar: ya estaba rota en
master** (ver 0.8).

### F2 — `MultiBuffered` como única capa de buffers de display

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 2.1 | Migrar `082_plasma` y `083_fbm_noise` (hoy 2 instancias + `m_active` manual) | `demos/amiga/082_plasma`, `083_fbm_noise` | **hecho** |
| 2.2 | Exponer **N por configuración** (`-DK_<DEMO>_BUFFERS=1\|2\|3`) y documentar «con y sin doble/triple buffer» | `demos/amiga/061…`, `080…`, `082…`, `083…` | **hecho** |
| 2.3 | Evaluar `079` (anillo de 5, rastro temporal) y `116` (triple buffer real): ¿encajan como `MultiBuffered<Driver,N>` o son otro caso? | `demos/amiga/079…`, `116…` | **decidido** (ver abajo) |

**Decisión F2.3**: `079_wireframe` **no** encaja: su anillo de 5 es de **planos sueltos**
(`kRing = planes+1`, un `PlaneView` por ranura) que la copperlist combina como rastro
temporal (bit3=active, bit2=active-1, …); no son buffers de display con bitmap propio.
`116_flatshade_convex` **sí** encaja: 3 bitmaps completos (4 planos cada uno) + 3
copperlists con semántica exacta de `commit()` (dibuja en el trasero, convierte y publica
el recién escrito; rota). **Migrado**: usa `MultiBuffered<HamScene, 3>` y `commit()`;
verificado READY + **20,33 fps** (el README dice ~20,7), `verify-116` PASS y visión de
secuencia sin anomalías (poliedro convexo girando).

**Hallazgo F2**: `HamScene` emite la lista **por línea** (WAIT + BPLMOD + BPLCON1 por cada
una de las 256 líneas) **aunque `row_repeat == 1`**, donde no hace falta ninguna: ~2 KB de
copperlist y trabajo de Copper por línea regalados. El original de la 116 usaba una lista
plana de 512 B. Es una mejora pendiente del driver (no rompe nada, pero cuesta DMA).

Gate: regresión de esas demos (fps + gate visual).

**Resultado de F2**: `CopperChunkyScene` gana `bind()` y declara `bitplane_bytes_for == 0`
(su "buffer" es la copperlist, no hay bitplanes); `MultiBuffered` no reserva planos en ese
caso. 082 y 083 usan ya `MultiBuffered<CopperChunkyScene, N>`. Verificado: 082 READY +
36,49 fps + visión limpia; 083 READY y **frames con MD5 distintos pero idénticos a los de
antes del refactor** (byte-idéntico en comportamiento; el modelo de visión dio falso
negativo «no se mueve»). N por configuración en 061/080/082/083: los 4 casos (N=1 y N=2)
compilan y **082 con N=1 arranca** (`A500_k_082_buffers1_debug`).

### F3 — Superficies/capas sin memoria de display

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 3.1 | `DoubleBufferScrollPlayfield` pasa a **superficie ligada a slots** (conserva cámaras/mapper/`hardware_view`; deja de poseer `Bitmap[2]`) | `engine/include/eng/field/double_buffer_playfield.hpp:100` | pendiente |
| 3.2 | Migrar la demo `122` al nuevo contrato | `demos/amiga/122_doublebuffer_scroll` | pendiente |
| 3.3 | Decidir el destino de `FlatScrollPlayfield`/`MirrorScrollPlayfield` (misma regla: no poseer memoria) | `engine/include/eng/field/{flat,mirror}_playfield.hpp` | pendiente |
| 3.4 | `PlaneView`/`SoftDpfComposition`: documentar que es flip **de un plano** (excepción deliberada) | `plane_view.hpp:43-65`, `soft_dpf.hpp` | pendiente |

Gate: demo `122` + gate visual/secuencia; host `038/039`.

### F4 — `CopperPlan`: orquestación de copper a nivel de escena

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 4.1 | Definir `CopperPlan` (un plan por buffer de display: recolecta `CopperIntent`/`SpriteIntent`, ordena por scanline, `Patch`/`Reemit`, handles, presupuesto) | `engine/include/eng/graphics/copper/plan.hpp` | **hecho** |
| 4.2 | Integrarlo con `MultiBuffered` (el plan se rellena en el buffer trasero y se publica en `commit`) | `engine/include/eng/graphics/drivers/multi_buffered.hpp` | **parcial**: el plan ya es **no propietario** (`attach(DoubleBuffer&)`, cubierto por HOST-070) y publica con `commit(backend)`; falta el plan **por slot** de `MultiBuffered` (resolver la semántica de rotación: hoy `MultiBuffered::commit` instala Y rota, y el `Plan` voltea aparte) |
| 4.3 | Portar como **tracks** los casos que hoy emiten copper a mano: empezar por `055_copper_rainbow` | demo `055` | **hecho** (055 usa ya el plan y gana doble buffer de copperlist) |
| 4.4 | Test host del `CopperPlan` (orden por línea, patch vs reemisión, handles válidos, presupuesto) | `tests/host/070_copper_plan` | **hecho** |
| 4.5 | **Topología de display conocida por el plan**: declarar zonas (rango de líneas + ventana de contenido) para que un efecto exprese su necesidad en coordenadas de contenido y el plan la traduzca a raster — el caso XYlimited, que reparte la pantalla en campos/splits, debe ser transparente para los efectos | `engine/include/eng/graphics/copper/plan.hpp`, `xlimited*` | pendiente |
| 4.6 | **Gradiente por línea**: hoy el `Plan` ya lo soporta (capacidad 320 + sort O(n)), pero en la 085 el cielo con 256 intenciones dispara el frame a **14 campos** y el BOB por Blitter a **3** (update ~65k, bucle ~425k). Perfilar el **bucle** (VPOSR/`wait_vblank` con Copper+Blitter activos) antes de dar la versión por línea | `demos/amiga/085…` | pendiente |

**Resultado de F4.1/F4.3/F4.4**: `eng::copper::Plan` implementado
(`begin`/`begin_frame`/`scheduler`/`add`/`materialize`/`end_frame`/`commit`/`takeover`),
con **ordenación por scanline** dentro del plan (el invariante deja de ser del llamador),
doble buffer de copperlist y detección de overflow. Verificado por HOST-070 y por la demo
**055** (arranca, anima y mide **49,92 fps**; se le añadió `mark_frame`, que no tenía).
Nota: la visión marcó como «anomalía» las franjas planas y negras de la 055, que son el
diseño (arcoíris de 32 bandas discretas que pasa por el negro); el prompt pedía «degradado»
y era el prompt el que estaba mal.

Gate: demos de copper idénticas + test host + informe de presupuesto sin spill.

### F5 — Unificar los dos mundos de scroll

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 5.1 | Unificar el mapper de scroll (`map_ring_scroll` vs `TileScrollScene::compute_display`) | `amiga_display_mapper.hpp:52`, `tile_scroll.hpp:670` | pendiente |
| 5.2 | Decidir el destino de `TileScrollScene` y de las demos `100/101/103/104/105` (¿canónica, laboratorio o retirada?) | `SCROLL_DEMOS_CLEANUP.md` | pendiente |
| 5.3 | Unificar cámaras (`Camera2D`, `RouteCamera`, `BigBufferScroll`, `CameraQ16`) en un solo vocabulario | `scene/`, `field/` | pendiente |
| 5.4 | Decidir `XlimitedScene` vs `VirtualScene` (y si nace `DisplayComposition`) | `field/xlimited_scene.hpp`, `scene/virtual_scene.hpp` | pendiente |

Gate: regresión de las demos de scroll + docs actualizados.

### F6 — Infraestructura de objetos: BOB (blitter) / objeto CPU / sprite HW

Pregunta que la origina: *«¿tenemos infraestructura de gestión de Blitter objects, CPU
objects ni HW Sprites?»*. Respuesta: **hay piezas, no la capa de objetos**. Y ojo con la
distinción clave (regla en `docs/engine/architecture/OBJECT_SYSTEM.md`): **un BOB es una copia de bitmap** (cookie-cut
`$CA` u OR, con shift y, si los planos van intercalados, **1 solo blit por objeto**), **no**
un polígono del Blitter (line-draw + area-fill, para relleno vectorial/3D).

| Pieza | Estado | Dónde |
|---|---|---|
| Blit de BOB: descriptor + cola + ejecución HW | **EXISTE** | `frame_plan.hpp:44-50,182-199` (`BlitJob`, kinds `MaskedBobCookieCut`/`MaskedBlobNoSave`/`CopyRect`/`RestoreRect`), `MinimalBackend::execute_frame_plan` (`amiga_minimal.cpp:619-716`) |
| Minterm elegible (OR-bob, glow) | **EXISTE** | `BlitJob::minterm` (`frame_plan.hpp`); kinds `OrBlob` (`$FC`, `D=A\|D`) y `ClearRect` (`$00`) en el ejecutor |
| Layout intercalado en el path de blits | **EXISTE** | `BlitJob::interleaved` explícito (altura = alto×planos, `bitplane_count=1`, módulos propios); el patrón `bitplane_count=1` + altura = planelíneas sigue usándose en `xlimited.hpp`/`soft_dpf.hpp` |
| Clase/manager de BOBs (posición, frame, máscara, clip, save/restore, ciclo de vida) | **PARCIAL** | `graphics/bob.hpp` (`Bob`/`BobTarget`/`bob_draw`/`bob_erase`; `RestoreUnder` pendiente, sin manager de actores); 050 sigue armando su save/restore a mano |
| Objeto CPU 2D (posición+imagen+clip) | **NO EXISTE** | solo `Surface` (`field/surface.hpp`) y structs locales en demos (110/111) |
| Sprite HW: emitter + plantilla + allocator + intents | **EXISTE** | `sprite_manager.hpp`, `sprite.hpp`, `sprite_allocator.hpp`, `raster_intent.hpp:96-104` |
| Actor-sprite con estado y **overflow sprite→BOB cableado** | **NO EXISTE/PARCIAL** | 054 solo cuenta el `as_bob` (`054/.../main.cpp:145-149`) |
| Escena/actores (World/Actor/Layer/recursos/`Visual.id`) | **SOLO DISEÑO** | `docs/engine/architecture/SCENE_AND_RESOURCES.md`; la política sí existe (`scene/representation.hpp` + HOST-028) |

| # | Tarea | Estado |
|---|---|---|
| 6.1 | `scene/actor.hpp`: `Actor{Tipo, ActorTemplate, pos, clip, estado}` + `World` con arrays fijos por feature, consumiendo `RepresentationAllocator` | **HECHO (sin `World` de features)**: `scene/actor.hpp` con `ActorDesc`/`Actor` (superficie destino, `z` por superficie, prioridad de sprite, transparencia, fondo, anclaje/offset, Copper anclado, velocidad de animación), `ActorStore<Max>` con handles generacionales y orden de emisión por superficie/`z` (`plan_actor_order`, `emit_actors_in_order`). Test host `072_actor`. El contenedor de features/`World` sigue pendiente |
| 6.2 | `Bob` (bitmap) + manager: hoja de planos (+máscara opcional), frame de animación, clip y política de save/restore; emite `BlitJob`s con el presupuesto del `FramePlan` | **PARCIAL**: `engine/include/eng/graphics/bob.hpp` (`Bob`, `BobTarget`, `bob_draw`, `bob_erase_box`; minterm `$CA`/`$FC`/`$F0`, stride de hoja explícito) + emisión desde el actor (`actor_emit`: borrado, save-under y dibujo) en `scene/actor.hpp`. Geometría cubierta por `tests/host/071_bob` y `072_actor`; el camino Blitter **no** lo ejercita aún una demo con gate visual (ver nota) |
| 6.3 | **Minterm en `BlitJob`** (cookie-cut / OR / copy) para OR-bobs y uniformar 050/051/bobs3d | **HECHO**: `BlitJob::minterm` (por defecto `$CA`); kind `OrBlob` (`$FC`) y `ClearRect` (`$00`) en `frame_plan.hpp`/`execute_frame_plan` |
| 6.4 | Layout **explícito** en `BlitJob` (interleaved vs planar) para expresar «1 blit/objeto» sin el placeholder de stride | **HECHO**: `BlitJob::interleaved` (altura = alto×planos, un blit/objeto) con validación propia |
| 6.5 | Cablear `SpriteAllocator::as_bob` → `BlitJob` (transición sprite→BOB real) y cerrar el bug de la 054 | **HECHO en el engine y consumido por la demo 054**: `compose_sprites` ordena, reparte canales, publica `SpritePlacement` (que `SpriteManager::apply` materializa) y manda los `as_bob` al `FramePlan`. El bug visual de la 054 sigue siendo de la EMISIÓN de sprites (ver Nota 6.5b) |
| 6.6 | Objeto CPU 2D sobre `Surface` (posición + imagen/redibujo + clip) | pendiente |

**Nota 6.2 — artefacto de planos al montar el BOB en la 085**: la reescritura del BOB de la
085 al camino Blitter (hoja planar única + barrel shifter + `ClearRect`) compila y llega a
`READY`, pero el render presenta artefactos (mitad inferior del disco en índice 5 y restos
tipo media luna/franja bajo el objeto), así que **no pasa el gate visual** y se revirtió. Dos
datos objetivos: (1) el fps no mejora (CPU byte-copy 16,6 vs Blitter 16,6), luego **el
copiado del objeto no es el cuello de botella** del frame (coincide con F4.6: el bucle es
~425k ciclos y el update ~65k); (2) el defecto está en la geometría de los `BlitJob`s o en
cómo los aplica el ejecutor (módulos de origen/destino y avance de planos), no en la
construcción en sí (que el host test valida). Depurar contra `execute_frame_plan`
(`amiga_minimal.cpp:619-748`) antes de retomar.


**Nota 6.2b — tiras horizontales de sprite**: el `SpriteAllocator` reserva corridas de canales
contiguos para objetos más anchos que 16 px (`SpriteIntent::strip_id`/`strip_index`/`strip_span`),
la base de los fondos de sprites uno al lado del otro (Risky Woods / Jim Power). Al añadirlo se
corrigió un fallo latente del reparto: la condición de canal libre era `busy_until < top` cuando
`bottom` es exclusivo, de modo que un intent que arrancaba en la línea 0 no encontraba canal y se
descartaba el canal que terminaba justo en `top`; ahora es `<= top`. Cubierto por `003_sprite_allocator`.

**Nota 6.5 — camino de sprite del sistema de objetos**: `build_sprite_intents` construye una
`SpriteIntent` por actor y las ordena por `top` (contrato del `SpriteAllocator`);
`sprite_template_to_intents` proyecta una `SpriteTemplate` a intenciones (franja + `SpriteRearm` +
`SpritePaletteSwitch` sin escribir registros); `compose_sprites` reúne todo (orden por superficie y
`z`, reparto de canales, `SpritePlacement` para el emisor, `as_bob` al `FramePlan` y necesidades de
Copper ancladas); `SpriteManager::apply` vuelca los placements a los 8 canales. La demo 054 ya lo
consume. Todo con tests host.

**Nota 6.5b — la 054 no dibuja los 8 sprites (defecto PREEXISTENTE de la emisión)**: con la
composición conectada, la 054 solo muestra **2 sprites** (ambos del mismo par de color) en lugar de
los 8 repartidos. Un A/B contra la versión anterior de la demo (misma imagen exacta) descarta una
regresión del sistema de objetos: la composición está validada en host (canales 0..7, geometría,
punteros de DATA, orden) y el fallo está en la **emisión** `SPRxPOS/CTL/PT` o en la terminación de
la DATA del sprite. Pista: los dos que se ven son del par 2, lo que apunta a `palette_base` o a que
la mayoría de canales quedan deshabilitados o con `vstop` inválido. Es el siguiente paso de
depuración de la 054. Nota aparte: `SpriteManager::dma_bits()` usa `1u << (6 - i)`, que para el
canal 7 es un desplazamiento negativo (UB); no lo usa la 054 (habilita SPREN con `DMACON`), pero
conviene revisarlo contra la doc del chipset.

Referencias obligatorias antes de tocar esto (regla de contexto técnico): AHRM 3.ª
(`docs/reference/ahrm/`), `amiga-bootcamp/08_graphics/blitter_programming.md` (minterms,
cookie-cut, *Use Case 4: interleaved bitplane BOBs*, presupuesto DMA) y
`demoscene-repo-orig/effects/bobs3d/bobs3d.c` (OR-bobs intercalados + clear en 1 blit).

El diseño objetivo del sistema de objetos (representación y degradación, transparencia y
fondos, necesidades de Copper ancladas, algoritmos y presupuesto) está en
`docs/engine/architecture/OBJECT_SYSTEM.md`; el vocabulario de intenciones y las plantillas
de sprite, en `docs/engine/architecture/VISUAL_EFFECT_SPRITE_DESIGN.md`.

## Incongruencias detectadas (checklist)

| Incongruencia | Dónde | Fase |
|---|---|---|
| Doble buffer de copperlist reimplementado 4 veces | `tile_scroll.hpp:791`, `xlimited.hpp:1739,1928`, `079`, `116` | F1 |
| Doble buffer de display a mano (2/3/5 buffers) | `082`, `083`, `079`, `116` | F2 |
| Superficie que posee bitmaps y hace flip (duplica el display) | `double_buffer_playfield.hpp:100` | F3 |
| Mapper de scroll duplicado | `amiga_display_mapper.hpp:52` vs `tile_scroll.hpp:670` | F5 |
| Tres motores de scroll/tiles | `field/scroll_engine.hpp`, `drivers/tile_scroll.hpp`, `tilemap/tile_scroll.hpp` (+ `field_controller.hpp` legacy) | F5 |
| Cámaras duplicadas (4+) | `scene/`, `field/`, demos | F5 |
| «Escena» duplicada | `scene::VirtualScene` vs `field::XlimitedScene` | F5 |
| Política de buffers sin implementación | `virtual_scene.hpp:168,186` | F0 |
| Doc↔código (13 words vs reemisión) | `xlimited.hpp:1488-1491` | F0 |
| Referencia con deriva de línea | `DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md:189` | F0 |
| Cifras de fps de `BITACORA_SCROLL_TILES.md` desactualizadas: dice 103≈50 fps y hoy mide 32,7 (A/B con y sin `DoubleBuffer`: 32,67 vs 33,50 → **no** es del refactor, es del merge) | `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` (nota de rendimiento del scroll) | F0 |
| Demo canónica **rota**: 107 (corkscrew) no alcanza READY (A/B confirma que no es de F1.3) | `demos/amiga/107_xlimited_corkscrew` | F0 |
| Supervisión de copper por escena inexistente | — (nace en F4) | F4 |
| NO VERIFICADAS sin consumidor | `PolygonFillSink` (`platform/amiga/polygon_fill.hpp:14`, `field/playfield.hpp:87`), `CameraQ16` (`field/tile_demo.hpp:16`) | F0/F5 |
| Tests host que solapan demos (o al revés) | `038/039` vs `112`; `067` vs `061/080`; `044` vs `120`; `061` vs `120/121/122` | transversal |

### Nota sobre las cifras de fps (0.7)

Evidencia recogida (medida con `measure-fps.mjs`, A500 `-O1`):

| Demo | `BITACORA_SCROLL_TILES.md` | medido hoy | A/B sin `DoubleBuffer` |
|---|---|---|---|
| 101 | ~48 | **49,92** | — |
| 103 | ~50 | **33,50** | 32,67 |
| 104 | ~47,6 | **30,12** | — |

- **No es de los refactors de normalización**: el A/B de la 103 (con y sin `MultiBuffered`/`DoubleBuffer`) da 33,50 vs 32,67.
- **101 sí cuadra** con la nota; 103/104 no. Los READMEs de esas demos no citan fps.
- **No se puede comparar contra un commit antiguo** de forma directa: al hacer checkout cambian también el layout de `out/demos/...` y las propias herramientas (los números de la nota pueden ser de ese layout anterior), así que el A/B envejecido no es fiable.
- Receta para cerrarlo: `git bisect` entre el commit donde se escribió la nota de `BITACORA_SCROLL_TILES.md` y `HEAD`, midiendo 103 en cada paso, **con el tooling de cada commit** (o, mejor, fijar el artefacto: medir siempre con el runner actual y anotar fecha+config junto a cada cifra). Mientras no se haga, las cifras de fps de `BITACORA_SCROLL_TILES.md` deben considerarse **no trazables**, no un objetivo.

## Triaje de demos (runtime) — 2026-09

`tools/build/build-all-demos.sh`: **56 OK, 4 ASSET, 0 FAIL** (todas compilan).

`tools/analyze/sweep-demo-health.sh` (lanza cada demo con el runner y clasifica el
estado por canal lateral): **53 OK, 0 FAILED, 3 TIMEOUT, 4 NOEXE**.

| Estado | Demos | Lectura |
|---|---|---|
| TIMEOUT | `070_mixer_three_voices`, `071_mixer_four_voices`, `107_xlimited_corkscrew` | No publican READY ni FAILED: cuelgue/guru. **NO VERIFICADAS** |
| NOEXE | `072_sample_channel`, `073_sample_mixer`, `074_mixer_drums`, `076_mixer_sample_channels` | Faltan assets generados (ASSET en el build), no rotura de código |

Conclusión: **no hay que borrar demos en bloque**. El parque está sano (53/60 arrancan);
lo que hay es 3 casos concretos que arrancar, empezando por la 107 (canónica del corkscrew).
Regla de cierre: una demo que no arranca se marca **NO VERIFICADA** y no se documenta
como validada hasta que el barrido la de el OK.

## Decisiones pendientes (requieren consulta)1. ¿`TileScrollScene` se promueve a canónico o queda como laboratorio y se retiran sus demos?
2. ¿Se renombra `XlimitedScene` → `DisplayComposition` (como propone `PLAYFIELD_SCROLL_ARCHITECTURE.md`)?
3. ¿`TileFieldController` (4 páginas) se retira?
4. Para el doble buffer del anillo XLimited: ¿se duplica el anillo (+80 KB y 2× blitter en 201) o se amplía solo el plano de fondo (soft DPF, ya probado)?
