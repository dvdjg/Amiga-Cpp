# Capa de aventura gráfica del engine (`eng::adv`)

Diseño de la capa que permite construir **aventuras gráficas point & click** (estilo Monkey Island, Loom, Indy) sobre el engine existente: un **runtime de verbos** (estado de historia, inventario, diálogos, disparadores) más un **Stage Composer** que traduce la descripción de una sala a la **escena Amiga** real (capas de playfield/DPF, spr-layer, BOBs/sprites HW, Copper, HUD, audio y streaming). El guion decide **qué** pasa; la capa de aventura decide **cómo** se mapea a capas y presupuestos.

El diseño parte de una consulta externa (plan de partida tipo «Adventure Motor» + «AIR/VM») y la **adapta al engine real**: se reutiliza el **kernel de mundo de `eng::rpg`** (hechos, objetos, inventario, save) en vez de duplicarlo; no hay VM ni bytecode en el objetivo (**la lógica es C++ compilado AOT** contra la fachada, el contenido son tablas ROM/UAF); y no hay jerarquía de «Context» omnisciente, sino `struct` de datos con capacidad fija y **funciones libres** (estilo del repo). Los hitos de implementación están en [`ROADMAP_ADVENTURE.md`](../../guides/roadmap/ROADMAP_ADVENTURE.md) (A0–A6) y la conversión semiautomática de juegos clásicos (SCUMM/AGI/SCI) en [`ROADMAP_ADVENTURE_IMPORT.md`](../../guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md) (I0–I5). La capa está **parcialmente implementada**: A0 y el núcleo de A1 tienen tests y demos, mientras diálogo, Stage Composer, streaming y vertical slice siguen abiertos; `eng::adv` permanece **NO VERIFICADA** hasta cerrar A5.

## 1. Principios de diseño

- **La unidad es la escena Amiga, no un framebuffer.** Una «room» no es un PNG de fondo: es un **`StageDesc`** (capas tipadas + walkboxes + hotspots + cues de Copper + manifiesto de streaming + presupuesto declarado) que el compositor materializa con las técnicas que el engine ya domina. Se conserva la idea de *Stage Graph* de la consulta, mapeada al vocabulario real de `eng::scene` (`LayerPlan`, `LayerRole`, `SceneStrategy`).
- **Un solo kernel de mundo: `eng::rpg`.** Hechos persistentes (`FactStore`), objetos tipo+instancia (`ItemStore`), bolsas/inventario (`ItemBag`), cerraduras (`LockSet`), reloj y **save por componente** ya existen y son exactamente lo que una aventura necesita. `eng::adv` **no inventa** un segundo sistema de hechos/inventario: añade solo el dominio point & click (sala, walkboxes, cámara de mundo, verbos, diálogo, director, compositor). La consulta proponía un `FactStore`/`Inventory` nuevos por desconocer esta capa.
- **Sin VM en el 68000 (decisión).** Los guiones son **C++ compilado** (AOT desde el PC) contra la fachada `eng::adv`, registrados en **tablas `constexpr` de punteros a función**; el contenido declarativo (salas, diálogos simples, cues, condiciones/acciones) son **tablas en ROM/UAF** con el mismo modelo que `rpg::QuestBook` (Spans sobre tablas, sin `if (quest == 3)`). Un intérprete de bytecode, si algún día hace falta, es **solo del previewer host** (herramienta de escritorio), nunca del binario Amiga: en un A500 cada opcode del intérprete compite con el Blitter en el mismo bus. Esta es la misma decisión de pipeline de contenido que `RPG_LAYER.md` §5.7 y la regla de coste de `ZERO_COST_FRAME_PATH.md`.
- **El guion es una máquina de estados cooperativa, no un hilo.** `wait` es «ceder hasta el frame N» (contadores/timers del mini-SO), nunca bloqueo activo: el bucle duerme en `Wait()` y la capa avanza al drenar mensajes.
- **Datos vs materialización.** El juego declara actores y capas por **intención**; el compositor decide sprite HW / BOB / capa / CPU según presupuesto y puede degradar en runtime (`scene::choose_representation`, `SpriteAllocator`), como ya hace el resto del engine.
- **Cambio de sala = corte de guion.** Salir de una sala **aborta sus guiones locales** (y su cola de intents); los guiones globales (historia) persisten. Evita guiones «zombi» escribiendo estado de una sala que ya no existe.
- **Cero heap, capacidades fijas, sin virtuales en el camino caliente.** Todo el estado es POD con parámetros de plantilla (`WalkboxSystem<MaxBoxes,…>`, `DialogueRuntime<MaxNodes>…`); las variaciones son datos, no despacho dinámico.
- **Diálogo.** El género necesita diálogos antes que la narrativa RPG; el primer reproductor nace en `eng::adv` (`eng/adv/dialogue.hpp`) y el hito M5 de [`ROADMAP_RPG.md`](../../guides/roadmap/ROADMAP_RPG.md) lo reutiliza en vez de implementar un segundo.
- **Verificación por demo, como todo el engine.** Ningún hito se declara hecho sin su demo con visión y presupuesto medido; la capa queda **NO VERIFICADA** hasta cerrar su vertical slice (A5).

## 2. Posición en la arquitectura

```text
Juego (contenido): StageDesc por sala · guiones C++ + tablas · diálogos · textos ·
                   paletas · manifiestos de sala · tablas de verbos
─────────────────────────────────────────────────────────────────────────────
Capa de aventura (eng::adv)
  runtime de verbos · diálogo · director (cutscenes) · cámara de mundo ·
  walkboxes · copper score · transiciones · Stage Composer (planificador)
─────────────────────────────────────────────────────────────────────────────
Kernel de mundo (eng::rpg)
  hechos · objetos/instancias · inventario/equipo · reloj · quests declarativas ·
  save — un solo kernel para aventura y RPG
─────────────────────────────────────────────────────────────────────────────
Presentación y servicios del engine
  escena/objetos (eng::scene) · field/scroll (Xlimited/DPF/spr-layer) ·
  hud · ui · copper (Copper/Scheduler/Plan/DoubleBuffer) · audio · res · os
─────────────────────────────────────────────────────────────────────────────
Plataforma (backend Amiga: OCS/ECS; AGA futuro por variante de build)
```

| Capa | Qué aporta | Documento |
|---|---|---|
| Aventura | Verbos, diálogo, director, cámara, walkboxes, composición de sala | este documento §4–§8 |
| Kernel de mundo | Estado de partida y objetos (compartido con RPG) | [`RPG_LAYER.md`](RPG_LAYER.md) |
| Simulación | Criaturas/sociedad para NPC con vida (opcional; la aventura clásica no la necesita) | [`SIM_ECOSYSTEM.md`](SIM_ECOSYSTEM.md) |
| Escena y piezas | Capas, scroll, objetos, sprites, BOBs | [`SCENE_AND_RESOURCES.md`](SCENE_AND_RESOURCES.md), [`OBJECT_SYSTEM.md`](OBJECT_SYSTEM.md), [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](PLAYFIELD_SCROLL_ARCHITECTURE.md) |
| HUD y GUI | Barra de verbos, inventario, subtítulos, menús | [`HUD_SYSTEM.md`](HUD_SYSTEM.md), [`GUI_LIBRARY.md`](GUI_LIBRARY.md) |
| Servicios | Mensajes, timers, E/S, audio, recursos | [`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md), [`GAME_AUDIO.md`](GAME_AUDIO.md), [`RESOURCE_SYSTEM.md`](RESOURCE_SYSTEM.md) |

## 3. Qué se reutiliza y qué no se duplica

Antes de crear cada pieza se busca su equivalente en el engine (regla §1.7 de `AGENTS.md`):

| Necesidad de la aventura | Ya existe en | Decisión |
|---|---|---|
| Hechos de historia (`door_open`, `met_pirate`) | `eng::rpg::FactStore`/`FactSet` (`eng/rpg/facts.hpp`) | **Reutilizar** |
| Objetos con identidad, llaves, pilas | `ItemCatalog`/`ItemStore`/`ItemBag`/`LockSet` (`eng/rpg/items.hpp`, `inventory.hpp`, `interaction.hpp`) | **Reutilizar** |
| Condiciones/acciones declarativas (diálogos, disparadores) | `eng/rpg/quest.hpp` (`CondKind`/`ActKind`, HOST-463) | **Reutilizar el modelo**; `eng::adv` añade acción de presentación (say, anim, cue, cámara) en su propia tabla |
| Guardado de partida | `eng/rpg/save.hpp` (por componente, versionado) | **Extender** con un bloque `adv` (sala actual, estado de guion/diálogo) |
| Verbos e intenciones | `eng::util::IntentQueue` + verbos de `eng/rpg/interaction.hpp` | **Generalizar**: el conjunto de verbos es **dato** (`VerbSet`), no código por juego |
| Composición de capas de la sala | `scene::LayerPlan`/`LayerRole`/`SceneStrategy` (`eng/scene/plan.hpp`) | **Reutilizar**: el Stage Composer produce esas capas |
| Técnicas de scroll y su coste | `scene::{ScrollKind,ScrollSpec,RegionCost}` (`eng/scene/world.hpp`), `eng/scene/scroll_plan.hpp` | **Reutilizar**: la técnica es dato de capa y el planner ya sabe degradar |
| Elección sprite/BOB/capa/CPU | `scene::Representation`/`choose_representation` (`eng/scene/representation.hpp`) | **Reutilizar** (con `RepresentationBudget` del compositor) |
| Sprites HW (cursor, props) y degradación | `sprite_allocator`, `sprite_channel_window`, `sprite_chain`, `scene/sprite_scene.hpp` | **Reutilizar** (cursor por sprite HW ya es patrón de `eng::ui`) |
| BOBs, compuestos, trayectorias y pool | `scene/composite_actor.hpp`, `trajectory.hpp`, `entity_pool.hpp`, `scene/composite_scene.hpp` | **Reutilizar** |
| Navegación por polígonos convexos | `eng::ai::NavMesh` (`eng/ai/navigation/navmesh_lite.hpp`, HOST-118) + `eng::util::graph`/`pathfinding` | **Reutilizar**: las walkboxes son polígonos convexos con portales; el grafo se deriva de la adyacencia |
| Cámara de mundo (follow/pan/dead-zone) | No existe fachada (`scene::RouteCamera` es de demos) | **Nuevo** `eng/adv/camera.hpp`, puro y host-testable |
| Copper por línea y doble buffer | `eng::Copper` (`eng/api/copper.hpp`), `copper::{Scheduler,ListBuilder,Plan,DoubleBuffer}`, validadores `eng::setup` | **Reutilizar**: el *copper score* es una capa de datos encima |
| HUD (barra de verbos, inventario, subtítulos) | `eng::hud` (regiones `Band`/`Overlay`/`Screen`; `VerbBar`/`Tooltip` y el puente `eng/rpg/hud_bridge.hpp`, `HUD_SYSTEM.md` §5.7) | **Reutilizar** como vistas del estado; no duplicar modelo |
| Ventanas (menús, guardar, journal) | `eng::ui` (compositor, widgets, texto) | **Reutilizar** |
| Texto y fuentes | `Surface::draw_text`, `Font8`, `eng/ui/text.hpp` | **Reutilizar** |
| Secuencias temporizadas (cutscenes) | `eng::util::sequence`, `state_machine` | **Componer** en el director |
| Espera reactiva / mensajes | `eng::os` (puerto, señales, timers, `Request`) | **Reutilizar** (nada de polling nuevo) |
| Audio (música por sala, SFX, stings) | `eng::audio::GameAudio`/`SampleBank`/P61/pt (+ `stream_intent`) | **Reutilizar** |
| Assets y streaming | `eng::res::AssetCache`/`asset_table`/`budget`, `os/file`,`request`,`trackdisk`, UAF (`eng/assets/uaf.hpp`) | **Reutilizar**; el manifiesto de sala es contenido |
| Presupuesto de bus y validadores | `eng/hw/bus_budget.hpp` ([`BUS_BUDGET.md`](BUS_BUDGET.md)), `eng::setup` ([`COHERENCIA_DE_SETUP.md`](COHERENCIA_DE_SETUP.md)) | **Reutilizar** en compositor y en el validador host |
| Diálogo declarativo con opciones | No existe | **Nuevo** `eng/adv/dialogue.hpp`; M5 de RPG lo reutilizará |
| Walkboxes con escala/profundidad por punto | No existe como capa de género | **Nuevo** `eng/adv/walkbox.hpp` sobre `NavMesh` |

## 4. Modelo de datos: la sala como `StageDesc`

La sala se describe en **datos** (tablas `constexpr` en ROM para el vertical slice; UAF/fichero cocinado cuando el contenido crezca, con el mismo layout de structs — decisión de `RPG_LAYER.md` §5.7):

```text
StageDesc {
  id, world_bounds                    // espacio de mundo en píxeles (cámara y walkboxes)
  layers[]                            // capas tipadas (tabla siguiente)
  copper_cues[]                       // partitura de Copper (§7)
  walkboxes[]                         // polígonos convexos + escala/profundidad
  depth_zones[]                       // bandas Y que fijan escala/orden Z de actores
  hotspots[]                          // región → EntityId de rpg + verbos disponibles
  spawns[]                            // actores y props iniciales
  on_enter / on_exit                  // guiones (punteros a función, §5)
  stream_manifest                     // resident / room_pack / optional (§8)
  budget                              // tope declarado de Chip, copper words/línea, canales
}
```

La descripción **declarativa de sala orientada al hardware** que consume el compositor es el formato **`adv-stage/1`** ([`STAGE_FORMAT.md`](../../../tools/adventure/formats/STAGE_FORMAT.md)): display single/DPF/EHB/HAM, capas con scroll por hardware, parallax 8.8, transparencias por truco de color, Copper Score (cues y disparadores), canales de sprite, BOBs, HUD, streaming y presupuesto A500. El andamio se genera del SIM v2 (`tools/adventure/formats/scaffold-stages.mjs` → `edited/stages/`).

Tipos de capa (campo de la tabla de capas) y su materialización real:

| Tipo de capa | Materialización en el engine | Uso típico en la aventura |
|---|---|---|
| `PfFar` / `PfNear` | Campos de un DPF (`XlimitedDualComposer`/`XlimitedScene`) con `ScrollKind` (`CopperRing`, `Strip`, `Fine`) | parallax de isla/calle/cielo |
| `SprLayer` | Capa de sprites HW (`api/effects/sprite_layer.hpp`; técnica en `sprite-layer.md`) | fachadas free-form, público, mar |
| `BobLayer` | `CompositeScene`/`scene::Actor` (BOB + sprite + degradación automática) | protagonista, props, FX |
| `Panel` (EHB/HAM) | Región vertical con su propio modo y paleta (`WorldRegion` con modo por banda; EHB validado en demos 201/202; HAM pendiente de validar por franja) | retrato/close-up al hablar |
| `FxOverlay` | Copper por línea (`eng::Copper`/`Scheduler`): raster bars, agua por `BPLCON1`, destellos | efecto demoscene *dentro* de la sala |
| `HudStrip` | Región `eng::hud` (`Band`) | barra de verbos, inventario, subtítulos |

**Transparencias «con trucos de color»** (elección por dato de capa, no alpha genérico): cookie-cut de color clave en BOBs, prioridad del DPF (color 0 del PF delantero), prioridad sprite/playfield por banda (`BPLCON2` programado por Copper), fondo de franja EHB/HAM bajo el contenido y paletas locales por línea. Cada modo se declara en la capa y el compositor lo traduce a la técnica ya existente; lo no soportado se **rechaza en compilación del plan** (escape a `eng::field`), no en pantalla.

## 5. Guiones: C++ AOT, datos y Director

Tres niveles, del más barato al más libre:

1. **Tablas declarativas (ROM/UAF)**: verbos por hotspot, respuestas simples (`Look → say(id)`), condiciones/acciones de diálogo y disparadores con el modelo de `CondKind`/`ActKind` (hecho/ítem como datos). Es contenido puro: el motor solo evalúa y ejecuta.
2. **Funciones C++ registradas**: un guion por sala/objeto (`on_enter_room_dock`, `on_use_key_door`) registrado en **tablas `constexpr` de punteros** (`adv::ScriptTable`) — sin virtuales ni despacho dinámico. Es lo que genera el pipeline de importación (I1) para los guiones complejos descompilados.
3. **Director (cutscenes)**: secuencia de pasos con grupos en paralelo (`CameraPath`, `ActorWalk/PlayAnim`, `Say`, `Cue`, `WaitFrames/WaitFact`, `LockInput`), construida sobre `eng::util::sequence` y temporizadores del mini-SO; **abortable en el cambio de sala** y con **tiempos límite en modo debug** (si un asset no llega, el guion no se bloquea: lo informa).

Un guion es una máquina de estados que avanza **un paso por frame** (cooperativa), nunca un hilo bloqueado: el frame sigue durmiendo en `Wait()` entre mensajes. El mismo `ScriptTable` sirve al `on_enter` de sala, al `on_verb` de un hotspot y al disparador de un trigger — cambia el emisor, no el camino (patrón de intenciones de `RPG_LAYER.md` §5.1).

## 6. Cámara de mundo y profundidad

La cámara vive en **coordenadas de mundo** y es lo único que la capa de presentación lee para el scroll; el compositor convierte su posición en offsets **por capa**:

```text
offset_capa = clamp(lerp/follow/pan/bounds) * parallax_factor   →  ScrollSpec / set_camera
```

- **Superficie de la cámara**: `set_bounds(world, soft)` (dead-zone), `follow(actor, style)`, `pan_to(target, frames, ease)`, `play_path(keys)`, `stop_follow`, `shake(amp, frames)`; interpolación en mundo con fixed-point y **clamp a bounds después de interpolar** (nunca sale del mundo).
- **Parallax** es dato de capa (`parallax_factor`), no código: `PfFar` a mitad de velocidad, `PfNear` a 1.0. El scroll efectivo usa las técnicas existentes (`ScrollKind`) y su coste ya está modelado (`RegionCost`).
- **Profundidad/escala**: las `depth_zones` (o el `scale` de la walkbox) fijan la escala del actor; en A500 la escala es **contenido pre-escalado por zonas** (juegos de 2–4 escalas), y el zoom real de hardware (`BPLxMOD`, [`ROADMAP_HW_ZOOM.md`](../../guides/roadmap/ROADMAP_HW_ZOOM.md)) queda como mejora opcional por banda, no como dependencia.

## 7. Copper Score: el Copper como partitura

La consulta proponía tratar el Copper como **partitura con disparadores**, no como una lista fija de la sala. Adaptado al engine:

```text
CopperCue {
  tracks[]: PaletteTrack   // rango de COLORxx + keyframes (pasos precalculados)
            RegTrack       // línea + registro + valor (p.ej. BPLCON1, prioridades)
            WaitMarker     // ancla para parchear un tramo ya emitido
  trigger:  OnEnter | OnScrollX(x) | OnActorY(entity) | OnDialogue | OnBeat | OnScript(id)
  frames / loop / restore   // duración, repetición y vuelta al estado previo
}
```

- **Materialización**: los cues se expanden a movimientos del `copper::Plan` sobre el **doble buffer** existente; los parches puntuales usan los `MoveSlot`/`patch_*` canónicos. El presupuesto se mide en **palabras por línea** contra el límite del plan (`eng::Copper::words_used`, `RegionCost`), y la coherencia la validan `eng::setup::check_copper_list`/`check_copper_column`.
- **Interpolación** (atardecer, ciclos): los keyframes se **precalculan en compilación/cocina** (tabla ROM de N pasos entre color A y B) y en runtime solo se indexa; un cambio lento puede emitirse con pocos MOVEs por frame (paleta de trabajo pequeña), no con una re-emisión completa.
- **Ejemplos que la capa debe cubrir**: cambio de 4 colores al cruzar el muelle (disparador `OnScrollX`), «atardecer» de 30 s sobre los slots 1–7, rayo = 2 frames de paleta + `restore`, agua por `BPLCON1` en una banda media, y franja EHB/HAM al abrir el retrato de diálogo **pausando** el scroll del fondo (cue `OnDialogue`).

El compositor garantiza que un cue **no rompe el presupuesto**: si al añadirlo se pasa del tope declarado por la sala, el plan falla en el **validador host** (build) o degrada según la política declarada (nunca «a ver qué sale»).

## 8. Salas, transiciones y streaming

```text
StreamManifest { resident[] · room_pack[] · optional[] }
```

- **Carga**: `eng::res::AssetCache` (prioridad/presupuesto) + E/S asíncrona del mini-SO (`os::request`/`trackdisk`); el **prefetch** de salas adyacentes es una lista declarada en el manifiesto. El material del vertical slice vive en ROM; a partir de contenido grande, los packs se cargan al disco con este camino.
- **Transición de sala**: salir → cue de salida (fade por paleta / iris / wipe) → liberar pack anterior → cargar el siguiente (**solapando con el puente musical** si hay buffer de audio) → el compositor arma la escena → `on_enter`. La cámara hace *handover* (posición final + a quién sigue).
- **Presupuesto**: el manifiesto declara tope de Chip y el **validador host** (I4 del roadmap de importación) calcula por sala bitplanes + buffers de scroll + sprites + copperlist y **falla el build de contenido** si la sala no cabe en el perfil A500/A1200 elegido. La degradación en runtime (menos capas, técnica de scroll más barata, sprite→BOB) es política del compositor, documentada y determinista.

## 9. Importación de juegos clásicos (SIM → C++ → datos)

La conversión de juegos SCUMM/AGI/SCI **no ejecuta bytecode original** en el Amiga ni porta ScummVM: usa ScummVM y sus herramientas como **oráculo de formato y comportamiento en el host**, extrae a un **SIM** (Semantic Intermediate Model — la idea «AIR» de la consulta), **traduce los scripts a C++ de `eng::adv`** (reglas deterministas + IA local opcional con revisión humana) y **cuece assets** al pipeline del engine (UAF/tiles/EHB/BOB/sprite). El detalle, fases y licencias están en [`ROADMAP_ADVENTURE_IMPORT.md`](../../guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md). El **enriquecimiento Amiga** (separar fondos en capas de parallax, transparencias por color clave, cues de Copper) es una fase propia con revisión humana y **modo diff** contra la versión importada.

## 10. Qué va en el engine y qué en cada juego

| Engine (`eng::adv`) | Juego |
|---|---|
| Runtime de verbos, diálogo, director, cámara, walkboxes, compositor | `StageDesc` de sus salas y tablas de hotspots/verbos |
| Máquina de cues de Copper y presupuesto | Partituras de paleta/efecto por sala |
| Manifiesto de streaming y transiciones | Contenido: textos, diálogos, mapas de sala, música |
| Políticas de degradación (config) | Prioridades concretas y tope declarado por sala |

El engine **no** define «la aventura de piratas»: define verbos, cámara, walkboxes, cues y composición; el juego aporta el contenido y los guiones registrados. Un vertical slice (el muelle del criterio de éxito A5) valida la capa mejor que diez sistemas a medias.

## 11. Coste, memoria y presupuesto

- **El compositor corre al cambiar de estado** (entrar en sala, abrir retrato, disparar un cue), no por frame; el coste por frame es el ya conocido de las piezas que usa (scroll/BOB/sprites/HUD), más la actualización de la cámara (sumas y un clamp) y el parcheo de cues activos.
- **Capacidades fijas** por plantilla: walkboxes, hotspots, nodos de diálogo y cues viven en el tamaño que declara el juego; sin heap en target.
- **Nada de `*`/`/`/`%` en el bucle**: parallax por desplazamiento sobre factor fijo, interpolaciones con `mul_norm`/`div_norm`, avance de cámara con sumas acumuladas ([`METODO_OPTIMIZACION.md`](../../guides/optimization/METODO_OPTIMIZACION.md), §1.15 de `AGENTS.md`).
- **Rutas que no corren siempre se miden igual**: cambio de sala, disparo de cue y arranque de cutscene llevan su medición en el `VALIDATION.md` de la demo correspondiente (regla de presupuesto por elemento y peor caso).

## 12. Verificación y estado

- **Contenido del engine hecho** (capa de datos): `eng/adv/{types,walkbox,hotspot,room}.hpp` (walkbox convexa con `contains`/`closest_point`, hotspots con `hit_test`, `RoomContent` como vista), verificada por **HOST-473** (dos escalares) y alimentada por el pipeline (`decode` produce el SIM v2 y `codegen/emit-content.mjs` genera las tablas `constexpr`; ciclo probado end-to-end con MI1).
- **Núcleo de navegación y cámara hecho (A0)**: `eng/adv/navigation.hpp` (ruta por walkboxes con adyacencia derivada de la geometría + suavizado por visibilidad) y `eng/adv/camera.hpp` (bounds, dead-zone, seguimiento y paneo con easing), verificados por **HOST-475** (`s16`/`float`/`Fixed`). La demo **`demos/features/adventure/amiga/001_adv_walk`** los ejerce sobre un mundo propio (352×208, tres cajas, ventana de display por hardware con `fine_scroll_coarse`/`fine_delay`, bobs enmascarados con save-under y HUD 1bpp) y está **VERIFICADA**: ~48-49 fps estables (~1,0 campos/frame), flicker OK (`max_candidates: 0`) y visión canónica OK (`src/VISION.md`); ver `VALIDATION.md` de la demo.
- **Verbos, hotspots priorizados y réplicas (A1, en curso)**: `eng/adv/verb.hpp` (tabla de verbos como contenido — etiqueta, `VerbSemantic`, `VerbArity`, `CursorMode`—, `AdvAction`/`resolve_action` con validación de aridad y `pick_pointer` con la prioridad GUI → hotspot → suelo) y `eng/adv/say.hpp` (`SayQueue` FIFO con duración exacta por VBlank), verificados por **HOST-480**; el cursor por verbo es dato del contenido. La demo **`demos/features/adventure/amiga/002_adv_verbs`** ejerce A1 completo sobre la sala de la 001 (barra 2×5, frase, réplicas, inventario proyectado a una rejilla de `eng::hud` y puerta con `LockSet` + llave de `eng::rpg`) con guion de atracción determinista; régimen ~48-49 fps y visión canónica OK. Una ejecución anterior de la trayectoria Bézier del MCP mostró CIERRA en vez de MIRA, pero una repetición instrumentada no reprodujo movimiento, botón, hover ni selección en el invitado; la entrega de entrada real y la calibración de `HudHit` siguen abiertas junto con el click de hotspot y la regresión/flicker completa (`VALIDATION.md` de la demo).
- **Guiones y formato Amiga**: parser descumm→AST y **traductor AST→C++** (`cook/cpp/scripts/`, contrato `eng/adv/script_hooks.hpp`; MI1 748/748 y MI2 1004/1006 guiones; 25 433/35 871 llamadas mapeadas). El contrato cubre listas de argumentos del original (`arg_list` vía `w.list`), `ActorOps`/`VerbOps` como llamadas tipadas por sub-operación (`ActorOp`/`VerbOp` en `eng/adv/types.hpp`) y `print` estático como réplicas. Formato **`adv-stage/1`** con validador para las salas del port. El **runtime** de la capa (A0–A6) no está completo: plan en [`ROADMAP_ADVENTURE.md`](../../guides/roadmap/ROADMAP_ADVENTURE.md); la importación de clásicos, en [`ROADMAP_ADVENTURE_IMPORT.md`](../../guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md).
- Cada hito exige **tests host** (walkboxes/cámara/diálogo/compositor son lógica pura host-testable) y **una demo que lo ejerza** con visión Ollama, fps y flicker según las reglas del repositorio ([`PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`](../../guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md)).
- La capa se declara **VERIFICADA** cuando el vertical slice A5 (el muelle: parallax + walk + use con ítem + atardecer por copper + close-up EHB + barra de verbos estable + cambio de sala desde «disco» + presupuesto A500 declarado) pasa el pipeline completo; hasta entonces, toda cabecera de `eng/adv/` lleva la marca **NO VERIFICADA**.
