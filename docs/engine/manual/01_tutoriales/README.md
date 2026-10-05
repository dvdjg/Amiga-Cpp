# Tutoriales guiados

Camino **de cero a juego**, en orden. Cada tutorial es **autónomo**, trae **código funcional**
compilable y enlaza la **demo real** del repo que lo valida (regla del manual §4).

| # | Tutorial | Aprende |
|---|---|---|
| 01 | 01_hola_mundo.md | Backend Amiga, `App`, `start()`/`run()`, `g_eng_run_status`. |
| 02 | 02_bucle_vblank_y_frame.md | `init`/`update`/`render`, `app.frame()`, el latido de VBlank. |
| 03 | 03_dibujo_con_screen.md | `clear`/`fill`/`frame`/`line`/`text`, `present()`. |
| 04 | 04_escena_y_display.md | `GameDisplay`, composición, `takeover`, doble buffer. |
| 05 | 05_entrada_del_jugador.md | `app.input()` (`pad0`/`pad1`, ratón, teclado). |
| 06 | 06_audio.md | SFX (`SfxMixer`) y música (`AudioSystem`). |
| 07 | 07_objetos_y_sprites.md | `Sprite`, `SpriteScene`, `ActorDesc`, degradación a BOB. |
| 08 | 08_tiles_y_scroll.md | `ScrollPlan`, `TileScroll`, corkscrew 8-way. |
| 09 | 09_recursos_y_ficheros.md | `Vfs`, `Assets`, carga síncrona/asíncrona. |
| 10 | 10_mundo_y_planificador.md | `World`, `ScenePlan`, estrategias (`Single`/`Dpf`/`Bands`). |
| 11 | 11_framebuffer_indexado.md | `IndexedDisplay` / C2P (efectos por píxel). |
| 12 | 12_mini_os_y_tareas.md | Mensajes, timers, tareas de fondo. |
| 13 | 13_publicar_el_juego.md | Build/run/depuración/validación (enlaza `docs/build/`). |

Volver al [índice del manual](../README.md).
