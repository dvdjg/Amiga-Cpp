# Manual del engine — documentación oficial

Este es el **manual oficial** del engine C++23 para Amiga (y demás plataformas). Es la puerta de
entrada **orientada a aprender y a consultar**: tutoriales guiados de las distintas formas de usarlo,
los **niveles de profundidad/abstracción**, la **referencia completa** de cada módulo (incluidas clases
y plantillas de datos, matemáticas, simulación e IA) y la **arquitectura** con esquemas.

> Documentos de **diseño/decisión** (el *porqué* histórico, roadmaps, bitácoras) viven en
> `docs/engine/architecture/`, `docs/guides/roadmap/` y `docs/debugging/`. **Este manual no los
> duplica**: los **cita** y enlaza (regla de una sola verdad por hecho). Ver §7.

## 1. Cómo leerlo (rutas por lector)

| Si vienes a… | Ruta recomendada |
|---|---|
| **Hacer tu primer juego** | §`01_tutoriales` de arriba abajo → §`02_niveles/01_nivel_a_fachada` |
| **Buscar una clase/función** | §`04_referencia/<módulo>` (una sección por módulo) |
| **Entender cómo funciona por dentro** | §`05_arquitectura` (ASCII) + `docs/engine/architecture/` |
| **Integrar otro runtime** (p. ej. emulador) | `docs/engine/NES_CONSUMER_GUIDE.md` (caso de consumidor externo) |
| **Operar build/run/depuración** | `docs/build/BUILD_AND_RUN.md` + `docs/debugging/system/debug-winuae-v2-guide.md` |

## 2. Roles de cada sección (Diátaxis)

- **`01_tutoriales/`** — *aprender haciendo*: progresivo, de cero a juego, **código funcional** que
  compila (con enlace a la demo real que lo valida).
- **`02_niveles/`** — *las formas de usarlo*: la fachada (A), el dispositivo (B) y el metal (C), y
  **cuándo bajar** de nivel (árbol de decisión).
- **`03_guias/`** — *cómo hacer X*: recetas por tarea (animar el scroll, colisiones, música…).
- **`04_referencia/`** — *información*: **toda** la superficie del engine, por módulo (clases,
  plantillas, funciones, campos, errores, invariantes), con `fichero:línea` al fuente.
- **`05_arquitectura/`** — *entender*: el modelo de cada subsistema con **esquemas ASCII**.
- **`06_ejemplos/`** — *código funcional* completo, cada uno apuntando a su demo del repo.
- **`07_apendices/`** — glosario, mapa símbolo→página, citas, FAQ, troubleshooting.

## 3. Árbol de directorios

```
docs/engine/manual/
├── README.md                        # este índice / plan maestro
├── 00_introduccion/
│   ├── 01_que_es_el_engine.md
│   ├── 02_filosofia_y_reglas.md            # zero-cost, "la app pide / el engine dispone", sin heap/RTTI
│   ├── 03_mapa_de_modulos.md               # ASCII: los 23 módulos + dependencias
│   ├── 04_arquitectura_de_capas.md         # ASCII: core → memory/graphics → field/scene → api → platform
│   └── 05_glosario.md
├── 01_tutoriales/                          # guiados, progresivos; código funcional
│   ├── 01_hola_mundo.md                    # backend + App + start/run
│   ├── 02_bucle_vblank_y_frame.md          # init/update/render, VBlank, frame()
│   ├── 03_dibujo_con_screen.md             # Screen: clear/fill/frame/line/text
│   ├── 04_escena_y_display.md              # GameDisplay, Scene, composición, present
│   ├── 05_entrada_del_jugador.md           # app.input(), pads
│   ├── 06_audio.md                         # SFX + música
│   ├── 07_objetos_y_sprites.md             # Sprite / SpriteScene / actores
│   ├── 08_tiles_y_scroll.md                # ScrollPlan, TileScroll, corkscrew
│   ├── 09_recursos_y_ficheros.md           # Vfs, assets, carga síncrona/asíncrona
│   ├── 10_mundo_y_planificador.md          # World, ScenePlan, estrategias
│   ├── 11_framebuffer_indexado.md          # IndexedDisplay / C2P (efectos por píxel)
│   ├── 12_mini_os_y_tareas.md              # mensajes, timers, tareas de fondo
│   └── 13_publicar_el_juego.md             # build/run/depuración/validación (enlaza docs/build)
├── 02_niveles/
│   ├── 01_nivel_a_fachada.md               # App/Screen/SpriteScene/ScrollLayer/IndexedDisplay
│   ├── 02_nivel_b_dispositivo.md           # Device/Scene/FramePlan/copper::Scheduler
│   ├── 03_nivel_c_metal.md                 # hw/cpu/paralela/backend/registros
│   └── 04_escape_cuando_bajar.md           # árbol de decisión ASCII
├── 03_guias/                               # how-to por tarea (una por tarea)
│   ├── animar_scroll_y_parallax.md
│   ├── colisiones_y_deteccion.md
│   ├── espanoles_y_efectos_de_copper.md
│   ├── interfaz_de_usuario.md
│   └── … (crece por tarea)
├── 04_referencia/                          # UNA SECCIÓN POR MÓDULO — cubre TODO el engine
│   ├── api/                                # fachada: game, screen, scene, scroll, sprites,
│   │                                       #   framebuffer, display, assets, copper, device, effects, world_render
│   ├── core/
│   │   ├── types/                          # dominios, tags, addr, box, span, ptr, typed, domains…
│   │   ├── math/                           # linalg, geometry, fixed, minifloat, scalar_traits,
│   │   │                                   #   scalar_math, noise, expr, sinetable, interp…
│   │   ├── data/                           # ct_array, byte_order, binary, checksum…
│   │   └── util/                           # expected, static_vector, pool, string_view,
│   │                                       #   function_ref, scope_guard, hash…
│   ├── memory/                             # memory_manager, arena, block_pool, budget
│   ├── graphics/
│   │   ├── planar/                         # bitmap, plane_view, surface, blit, raster…
│   │   ├── copper/                         # copper, plan, scheduler, double_buffer, static_plan
│   │   ├── blitter/                        # blitter_state, bob, sprite, sprite_allocator/manager
│   │   ├── composition/                    # scene, compose, stages, limits, copper_chunky
│   │   ├── tilemap/                        # tile_map, tile_scroll, attribute_table, tile_planar…
│   │   ├── effects/                        # rotozoom, fine_scroll, palette_*, c2p…
│   │   └── drivers/                        # drivers gráficos por estrategia
│   ├── field/                              # playfield, xlimited_*, strip_*, scroll_*, runtime_scroll_geometry
│   ├── scene/                              # world, plan, actor_*, dpf_plan, band_plan, raster_plan…
│   ├── input/
│   ├── os/                                 # message, port, time, file, vfs, request, task
│   ├── res/                                # asset_cache, dynloader, engz, hunk, zx0, decode, load, budget
│   ├── audio/                              # mixer, mode, music, p61, pt, stream, paula…
│   ├── ui/                                 # widgets, compositor, layout, text…
│   ├── hw/                                 # info (inventario de hardware), bus_budget
│   ├── debug/                              # run_status, peripheral, mem_probe, telemetry
│   ├── cpu/                                # m68k (utilidades/emulación)
│   ├── parallel/ , task/ , assets/ , retro/   # módulos pequeños
│   ├── platform/amiga/                     # backend, memory_profile, servicios
│   ├── ai/                                 # steering, navigation, planning, decision, perception
│   ├── sim/                                # simulación (gen, ecosistema)
│   ├── board/                              # IA de tablero (rules, search, eval, knowledge, storage, explain)
│   └── cards/                              # IA de naipes (rules, ai, eval, sim)
├── 05_arquitectura/                        # explicación profunda + MUCHOS esquemas ASCII
│   ├── modelo_de_memoria.md
│   ├── sistema_de_tipos.md                 # Bytes/Words/Block<Tag>/Address, frontier raw()
│   ├── frame_path.md                       # del update al VBlank: blits, copper, present
│   ├── display_y_dma.md
│   ├── copper_y_scheduler.md
│   ├── c2p_y_framebuffer_indexado.md
│   ├── planificador_de_escena.md
│   ├── sistema_de_objetos.md
│   ├── mini_os_de_mensajes.md
│   ├── audio_sintesis_y_streaming.md
│   └── … (una por subsistema)
├── 06_ejemplos/                            # código funcional, enlazado a demos reales
│   └── (por tema; cada ejemplo cita su demo en `demos/`)
└── 07_apendices/
    ├── mapa_simbolo_a_pagina.md            # fichero:símbolo del engine → página del manual
    ├── citas_y_fuentes.md                  # AHRM, emulador, docs/reference
    ├── faq.md
    └── troubleshooting.md
```

## 4. Convenciones (obligatorias)

1. **Español** con ortografía correcta (tildes, eñes, puntuación). Párrafos en **una línea lógica**
   (word wrap), sin saltos a mitad de frase (regla de `STRUCTURE.md` §10.2).
2. **Esquemas ASCII** siempre que aclaren capas, flujos, buffers, geometrías o zonas de hardware
   (`+---+`, flechas `-->`, cajas). Un diagrama antes que un párrafo largo.
3. **Ejemplo de código funcional**: cada tutorial/guía trae un bloque **completo y compilable**
   (o un fragmento marcado como tal) y **enlaza la demo real** (`demos/…/NNN/`) que lo valida.
4. **Referencias al código fuente**: toda afirmación de referencia cita `ruta:línea` del engine
   (p. ej. `engine/include/eng/api/game.hpp:505`), no «según el engine».
5. **Una sola verdad**: un hecho se describe **una vez**; los demás sitios **enlazan**. La
   *explicación* va en `05_arquitectura/`, la *referencia* en `04_referencia/`, el *diseño/decisión*
   en `docs/engine/architecture/`.
6. **Citas de fuentes**: hardware/registros → `docs/reference/ahrm/` y las fichas de emulador
   (`docs/reference/emulators/`), no se reescriben.

## 5. Cobertura (los 23 módulos) — checklist de referencia

`04_referencia/` debe cubrir **todo** el engine. Estado de la cobertura (a completar por pasadas):

- [ ] `api` (13) · [ ] `core/types` · [ ] `core/math` · [ ] `core/data` · [ ] `core/util`
- [ ] `memory` · [ ] `graphics/planar` · [ ] `graphics/copper` · [ ] `graphics/blitter`
- [ ] `graphics/composition` · [ ] `graphics/tilemap` · [ ] `graphics/effects` · [ ] `graphics/drivers`
- [ ] `field` · [ ] `scene` · [ ] `input` · [ ] `os` · [ ] `res` · [ ] `audio` · [ ] `ui`
- [ ] `hw` · [ ] `debug` · [ ] `cpu` · [ ] `parallel` · [ ] `task` · [ ] `assets` · [ ] `retro`
- [ ] `platform/amiga` · [ ] `ai` (steering/navigation/planning/decision/perception)
- [ ] `sim` · [ ] `board` · [ ] `cards`

> **Alcance**: 459 cabeceras en 23 módulos (conteo a la fecha). El manual es **exhaustivo en
> intención**: ninguna clase ni plantilla del engine queda sin entrada en `04_referencia/`.

## 6. La escalera de aprendizaje (resumen)

```
nivel A (fachada)          App · Screen · SpriteScene · ScrollLayer · IndexedDisplay · World
   |                                   (lo normal: un juego se escribe AQUÍ)
   v
nivel B (dispositivo)      Device · Scene · FramePlan · copper::Scheduler · RasterLayout
   |                                   (cuando A no llega: efectos, composición a mano)
   v
nivel C (metal)            hw · cpu · parallel · backend · registros · DMA
                                       (lo último: el engine deja de existir)
```

Regla de oro: **sube al nivel más alto que resuelva la intención**; baja solo con motivo (documentado
en `02_niveles/04_escape_cuando_bajar.md`).

## 7. Relación con la documentación existente (no duplicar)

| Doc existente | Rol | Cómo lo usa el manual |
|---|---|---|
| `docs/engine/architecture/*.md` | **Diseño y decisión** (el *porqué*, contratos, roadmaps) | `05_arquitectura/` y `04_referencia/` lo **citan** y enlazan |
| `docs/engine/NES_CONSUMER.md` / `NES_CONSUMER_GUIDE.md` | Caso de **consumidor externo** | Enlazado como ejemplo de integración |
| `docs/guides/roadmap/*`, `docs/guides/methodology/*` | Planes vigentes y método | No entra al manual; enlazado |
| `docs/reference/amiga/*`, `docs/reference/ahrm/*` | **Referencia de hardware** | Citado en `07_apendices/citas_y_fuentes.md` |
| `docs/build/*`, `docs/debugging/*` | Operación (build/run/debug) | `01_tutoriales/13` y `07_apendices` enlazan |

## 8. Estado y contribución

El manual se construye **por pasadas**: primero `00_introduccion/` y `01_tutoriales/` (el camino del
principiante), luego `04_referencia/` módulo a módulo, y `05_arquitectura/` a medida que cada
subsistema se consolida. Cada documento nuevo respeta §4 y se indexa aquí.
