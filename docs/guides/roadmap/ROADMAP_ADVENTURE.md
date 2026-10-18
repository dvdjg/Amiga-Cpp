# Roadmap: capa de aventura gráfica (`eng::adv`)

Plan por hitos de la capa de aventuras gráficas point & click sobre el engine. El diseño vigente está en [`ADVENTURE_LAYER.md`](../../engine/architecture/ADVENTURE_LAYER.md); este documento fija **qué se entrega, con qué dependencias y cómo se verifica** cada hito. La regla de cierre es la del repositorio: tests host por API y **una demo que la ejerza** (el vertical slice de A5) antes de declarar la capa VERIFICADA.

Principio rector: un **vertical slice** (un muelle con parallax + caminar + usar con ítem + atardecer por Copper + close-up + cambio de sala) valida la capa mejor que quince sistemas a medias. Los hitos se ordenan para que cada uno sea visible y medible; nada se entrega «a medias y sin consumidor». La lógica es C++ compilado AOT (sin VM en el 68000) y el kernel de mundo es `eng::rpg`, no uno nuevo.

## Estado

| Hito | Contenido | Estado |
|---|---|---|
| **A0 — Núcleo de sala** | Sala, walkboxes, cámara de mundo, actor que camina | **EN CURSO** — navegación por walkboxes (`eng/adv/navigation.hpp`) y cámara (`eng/adv/camera.hpp`) con **HOST-475**; demo `001_adv_walk` creada y funcional (ruta+cámara+HUD+click, visión OK) pero **NO VERIFICADA por presupuesto** (~16,6 fps con bug de cadencia abierto: `docs/debugging/investigaciones/001-adv-walk-pacing.md`) |
| **A1 — Verbos e interacción** | Verbos (dato), hotspots, hit-test, say, inventario | Pendiente |
| **A2 — Diálogo y Director** | Árboles de diálogo con hechos + cutscenes | Pendiente |
| **A3 — Stage Composer** | Capas, parallax, Copper Score, presupuesto y degradación | **EN CURSO** — formato **`adv-stage/1`** definido y validado ([`STAGE_FORMAT.md`](../../../tools/adventure/formats/STAGE_FORMAT.md)) con andamios generados desde el SIM; falta el compositor y las demos |
| **A4 — Salas y streaming** | Manifiestos de sala, transiciones, carga desde disco | Pendiente |
| **A5 — Vertical slice** | Juego piloto «muelle» en `games/` que cierra la verificación | Pendiente |
| **A6 — Pulido** | Perfil contextual, texto avanzado, journal y guardado | Pendiente |

**Numeración prevista**: demos en un ámbito nuevo `demos/features/adventure/amiga/` (001…) y el juego en `games/500_adventure_slice`; los tests host se numeran al crearlos (rango 474+; ámbito global según [`NUMBERING.md`](../../ai-dev-environment/NUMBERING.md)).

---

## A0 — Núcleo de sala: walkboxes, cámara y actor que camina

**Objetivo**: el jugador existe en una sala, camina a un punto cliqueado y la cámara le acompaña dentro del mundo.

**Entregables**:

1. `eng/adv/types.hpp`: ids (`RoomId`, `HotspotId`, `VerbId` como `u8`), punto de mundo (`Point2s`) y centinelas; sin tipos nuevos del backend.
2. `eng/adv/walkbox.hpp`: `WalkboxSystem<MaxBoxes, MaxVerts>` — cajas como **polígonos convexos** con `scale` (escala del actor dentro) y `depth` (orden Z); adyacencia derivada de aristas compartidas (los portales alimentan `eng::ai::NavMesh`, `eng/ai/navigation/navmesh_lite.hpp`, HOST-118) y `find_path` con string-pulling; consultas `box_at`/`scale_at`/`depth_at`. Puro, sin heap, host-testable.
3. `eng/adv/camera.hpp`: cámara de mundo (posición, `bounds` + dead-zone `soft`, `follow`, `pan_to(frames, ease)`, `play_path(keys)`, `shake`), `update(frames)` y `visible_rect()`; clamp **después** de interpolar. Puro.
4. `eng/adv/actor.hpp`: `AdventureActor` (posición lógica, ruta, estado de caminata, facing, anim, escala/profundidad desde el walkbox actual); avance de ruta determinista por frame sin `*`/`/` en el bucle.
5. Demo `demos/features/adventure/amiga/001_adv_walk`: sala cocinada como imagen Chip + parches (patrón de `games/400_rpg_slice`, lección DT-025), ratón para fijar destino (`eng::input`), cámara con dead-zone y bounds, franja de versión/depuración 1bpp (`AGENTS.md` §0).

**Dependencias**: `eng::input`/`InputAggregator` (existe), `eng::Screen` (existe). No depende del HUD interactivo (solo la franja 1bpp de depuración).

**Verificación**: tests host (grafo/path/escala de walkboxes con puntos fuera de caja y aristas degeneradas; cámara con dead-zone, bounds y pan con ease) + demo con visión por **secuencia** (ruta seguida y cámara acompañando), `measure-fps` y baseline de flicker. Rango de tests: HOST-474+.

## A1 — Verbos, hotspots, say e inventario

**Objetivo**: el jugador interactúa: mira, coge, usa (y usa objeto con objeto) y lee réplicas.

**Entregables**:

1. `eng/adv/verb.hpp`: `VerbId` y `VerbSet` como **tabla de contenido** (etiqueta, glifo de cursor, aridad: sin objetivo / un objetivo / dos objetivos); `AdvAction {verb, subject, target, secondary}` y `resolve_action` que valida y enruta a los verbos del kernel (`rpg::pick_up`/`drop_item`/cerraduras/`equip`…). El juego cambia su `VerbSet` sin recompilar el motor.
2. `eng/adv/hotspot.hpp`: regiones (rect/polígono) con orden Z y enlace a `rpg::EntityId`; **hit-test priorizado**: GUI/región HUD → hotspots (por profundidad) → suelo (walk-to vía walkbox); máquina de estado del cursor (`default/look/use/talk/exit`) guiada por datos.
3. `eng/adv/say.hpp`: cola de réplicas (`text_id`, hablante, duración en frames, beep opcional) avanzada por frame; el texto se dibuja en la región de diálogo del HUD con `draw_text`/`Font8`.
4. **Inventario como vista**: proyección de `ItemBag` a slots `eng::hud` (sin duplicar modelo).
5. Demo `demos/features/adventure/amiga/002_adv_verbs`: dos objetos recogibles, puerta con llave (`LockSet`), barra de verbos clásica en franja inferior, cursores por verbo y réplicas.

**Dependencias**: [`ROADMAP_HUD.md`](ROADMAP_HUD.md) H2 (slots y hit-test). *Fallback declarado si H2 no está*: franja H1 + hit-test de rectángulos dentro de `adv`, anotado en el `VALIDATION.md` de la demo (no se inventa un segundo HUD permanente).

**Verificación**: tests host (tabla verbo×objetivo: validez, dos-objetivos, prioridad de hit-test, timing de la cola de say) + demo con visión y presupuesto medido.

## A2 — Diálogo y Director (cutscenes)

**Objetivo**: hablar con NPCs por árboles con estado y dirigir escenas sin input.

**Entregables**:

1. `eng/adv/dialogue.hpp`: `DialogueBook`/`DialogueRuntime<MaxOpen>` — nodos con `when` sobre hechos/ítems (mismo modelo de condiciones/acciones de `rpg::quest.hpp`, ampliado con acciones de presentación), opciones con acciones y nodo destino; se apoya en la cola de `say` y el panel de diálogo del HUD. **M5 de [`ROADMAP_RPG.md`](ROADMAP_RPG.md) reutiliza este reproductor** (decisión en `ADVENTURE_LAYER.md` §3).
2. `eng/adv/director.hpp`: cutscene = pasos ordenados con grupos en paralelo (`LockInput`, `CameraPath`, `ActorWalk`, `PlayAnim`, `Say`, `Cue`, `WaitFrames`, `WaitFact`), sobre `eng::util::sequence` y timers del mini-SO; **abortable al cambiar de sala** y con tiempos límite en modo debug.
3. Demo `demos/features/adventure/amiga/003_adv_dialogue`: conversación de dos niveles con una opción desbloqueada por hecho, y una cutscene con paneo de cámara e input bloqueado que devuelve el control.

**Dependencias**: A1 (say/HUD de diálogo); `eng::os` timers (existe).

**Verificación**: tests host (evaluación del grafo con hechos, opciones inválidas, secuenciación/aborto del director) + demo con visión por secuencia.

## A3 — Stage Composer: capas, parallax, Copper Score y presupuesto

**Objetivo**: una sala declara capas y efectos; el compositor los materializa con las técnicas ya existentes y **respeta el presupuesto declarado**.

**Entregables**:

1. `eng/adv/stage.hpp`: `StageDesc` (capas tipadas `PfFar`/`PfNear`/`SprLayer`/`BobLayer`/`Panel`/`FxOverlay`/`HudStrip`, con contenido, `parallax_factor` fixed, modo de transparencia, modo de paleta y banda) y tabla de cues; puro (sin hardware).
2. `eng/adv/copper_score.hpp`: tablas de cues (`PaletteTrack` con pasos precalculados, `RegTrack`, `WaitMarker`) con disparadores (`OnEnter`, `OnScrollX`, `OnActorY`, `OnDialogue`, `OnBeat`, `OnScript`); expansión a movimientos del `copper::Plan`/parches `MoveSlot` sobre el doble buffer y contabilidad en palabras/línea (`RegionCost`, `eng::Copper::words_used`).
3. `eng/adv/composer.hpp`: `compose(stage, camera, actors, budget) → SceneComposition` — produce las capas `scene::LayerPlan` (estrategia `Single`/`Dpf`/`Bands` del planner existente), las `ScrollSpec` por capa (cámara × parallax), las asignaciones de representación (`choose_representation` con `RepresentationBudget`), las regiones HUD y los cues a disparar. Determinista y host-testable; **política de degradación declarada** (p. ej. actores secundarios sprite→BOB, capa lejana dinámica→estática, cue complejo→simple) con el orden del `ComposeConfig`.
4. Demos:
   - `004_adv_parallax`: sala DPF (`PfFar` a mitad de velocidad) + `PfNear` + `SprLayer` (fachada `api/effects/sprite_layer.hpp`, técnica `sprite-layer.md`), cámara moviéndose por el mundo.
   - `005_adv_copper_score`: atardecer de 30 s sobre los slots 1–7, rayo de 2 frames + `restore`, banda de agua por `BPLCON1`, un cambio de 4 colores disparado por `OnScrollX`.
   - `006_adv_ehb_band`: retrato/close-up EHB por franja con paleta propia (pausa el scroll del fondo al abrirse). *HAM*: la franja HAM queda **condicionada** al soporte vigente del modo; si no está validada por demo, se declara así en el `VALIDATION.md` y la sala usa EHB.

**Dependencias**: capas/scroll/DPF/spr-layer y sprites (existen, con demos 107/110/201/202/213/218); Copper (existe). El compositor es nuevo.

**Verificación**: tests host del compositor (elección de estrategia, mapeo de parallax, degradación determinista al recortar presupuesto, expansión de cues y su coste en palabras) + demos con visión, fps/cadencia y flicker; presupuesto por elemento (cue, cambio de capa activa) medido en las rutas que no corren siempre.

## A4 — Salas, transiciones y streaming

**Objetivo**: pasar de una sala a otra como un juego real: con carga, transición y sin romper el frame.

**Entregables**:

1. `eng/adv/rooms.hpp`: registro de `StageDesc` + `RoomPack` (`resident` / `room_pack` / `optional`) y `Rooms` (sala actual, `goto` con transición); `AssetCache` con presupuesto y **prefetch** de las salas adyacentes declaradas.
2. `eng/adv/transition.hpp`: `fade` (rampa de paleta por cue), `iris`/`wipe` (máscara BOB / columnas) y *handover* de cámara (posición final + a quién sigue); input bloqueado durante la transición.
3. **Save de aventura**: bloque propio del save (`rpg::save.hpp` por componente) con sala actual, posición y estado de guion/diálogo.
4. Demo `demos/features/adventure/amiga/007_adv_rooms`: dos salas unidas por una puerta, transición solapada con la lectura (ADF en WinUAE), música de puente si hay buffer, y **informe de presupuesto** por sala generado por la herramienta host (I4 de [`ROADMAP_ADVENTURE_IMPORT.md`](ROADMAP_ADVENTURE_IMPORT.md)).

**Dependencias**: [`ROADMAP_RESOURCES.md`](ROADMAP_RESOURCES.md) (carga asíncrona y presupuestos) y `eng::os` E/S (existe en parte). *Fallback declarado*: carga síncrona en la transición con música detenida, anotado en `VALIDATION.md`, si la E/S asíncrona aún no está lista.

**Verificación**: tests host (orden de carga del manifiesto, máquina de estados de transición, bloque de save) + demo con visión entre salas y **cadencia mantenida en el cambio** (medir la ruta de transición con perfil).

## A5 — Vertical slice: el muelle (verificación por demo)

**Objetivo**: cerrar la verificación de toda la capa con el criterio de éxito definido en el diseño (muelle con parallax, uso con ítem, atardecer por Copper, close-up y cambio de sala), adaptado y medido.

**Entregables**:

1. Juego en `games/500_adventure_slice` («un muelle»): parallax real (DPF + spr-layer), caminar y **usar un ítem** para resolver un puzzle, **atardecer por copper score** (no un fade genérico), **close-up EHB** al hablar, barra de verbos estable en su franja, **cambio de sala cargando desde «disco»** sin romper el frame, 3–4 salas, un diálogo con opciones y una cutscene. Contenido propio (sin assets de juegos comerciales).
2. HUD completo de aventura con `eng::hud` (barra de verbos, inventario, panel de diálogo) y franja de versión/depuración.
3. Música de sala + SFX + sting con `eng::audio` (`GameAudio`/`SampleBank`; streaming si está disponible).
4. `INTENT.md`/`VISION.md`/`VALIDATION.md` del juego con presupuesto A500 **declarado y validado por la herramienta de I4**, fps/cadencia a 50 Hz, flicker y visión por secuencias.

**Verificación**: pipeline completo `build → run → analyze` + visión Ollama + medidas. Al terminar, **quitar la marca NO VERIFICADA** de las cabeceras de `eng/adv/` y de `ADVENTURE_LAYER.md`.

## A6 — Pulido: perfil contextual, texto avanzado y guardado

**Objetivo**: el segundo perfil de interacción (moderno/one-click) y la capa de comodidad.

**Entregables**: perfil contextual como **segundo `VerbSet` + política de interacción** (mismo núcleo de acciones, sin reescribir reglas); menú radial ligero con `eng::hud`/`eng::ui`; texto avanzado (globos, wrapping, resaltado) sobre `eng::ui::text`/`Font8`; journal-lite de solo lectura y menú de guardar/cargar (E/S del mini-SO + `rpg::save`); demo `008_adv_contextual` y ampliación del slice.

**Verificación**: tests host (política de interacción: mismo verbo resuelto por ambos perfiles) + demo; el slice se revalida si cambia su render o su coste.

---

## Decisiones fijadas (adaptación de la consulta)

- **Sin VM/bytecode en el Amiga**: guiones C++ AOT + tablas ROM/UAF; el intérprete, si existe, es solo del previewer host (`ROADMAP_ADVENTURE_IMPORT.md` I3). La consulta convergió a esta opción; aquí es regla por el mandato de coste del engine.
- **Kernel compartido con RPG**: hechos, objetos, inventario, cerraduras, save y el modelo condiciones/acciones de quests se reutilizan; `eng::adv` no mantiene un segundo estado de mundo.
- **Sin jerarquía de «Context»**: `struct` de datos + funciones libres + tablas `constexpr` de punteros; nada de virtuales ni despacho dinámico en el camino caliente.
- **La capacidad la declara el contenido**: walkboxes, hotspots, nodos y cues tienen tope por plantilla; el validador host falla si una sala no cabe en el perfil A500 elegido.

## Dependencias externas y riesgos

- **HUD (H2)**: la barra de verbos interactiva y el inventario como slots dependen de `ROADMAP_HUD.md` H2/H3; el fallback por hito está declarado y se anota en la demo.
- **Recursos/streaming**: A4 completo depende de `ROADMAP_RESOURCES.md`; el fallback síncrono evita bloquear el hito pero **el vertical slice A5 exige la carga real** para cumplir el criterio.
- **EHB por franja**: el modo EHB está probado a pantalla completa (demos 201/202); su uso **por banda con paleta propia** se valida en la demo 006. **HAM** queda condicionado y se declara en el `VALIDATION.md` si no está validada.
- **Coste del compositor y las transiciones**: son rutas que no corren siempre y se miden como tales (presupuesto por elemento, regla §3 de `AGENTS.md`); un pico de frame en el cambio de sala invalida la demo hasta replantear.
- **Contenido**: assets y textos propios; la conversión de juegos comerciales vive en el pipeline host de importación y nunca añade material con derechos al repositorio.
- **Riesgo de sobre-diseño**: todo hito exige su demo; no se implementan sistemas «por si acaso» sin consumidor (regla §1.7 de `AGENTS.md`).

## Próximo paso verificable

A0 con su demo `001_adv_walk` (walkboxes + cámara + actor caminando sobre una sala cocinada): fija el vocabulario de la capa (puntos de mundo, sala, ruta) y es prerequisito de todo lo demás; su cierre se evidencia con tests host y visión por secuencia.
