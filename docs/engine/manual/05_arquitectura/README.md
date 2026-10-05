# Arquitectura (explicación)

El **cómo funciona por dentro**, por subsistema, con **esquemas ASCII**. Es la capa de *entendimiento*;
los *contratos de diseño* viven en `docs/engine/architecture/` (se citan).

| Documento | Subsistema |
|---|---|
| modelo_de_memoria.md | Pools Chip/Slow/Fast, arenas, `Block<Tag>`, presupuesto y propiedad. |
| sistema_de_tipos.md | Vistas con `Tag`, unidades fuertes, `Address`, frontera `raw()`. |
| frame_path.md | Del `update` al VBlank: blits, copper, `present` (ASCII del frame). |
| display_y_dma.md | Bitplanes, fetch, `BPLMOD`, `DIW`/`DDF`, doble buffer. |
| copper_y_scheduler.md | `copper::Plan`/`Scheduler`, parcheo, zonas de modo. |
| c2p_y_framebuffer_indexado.md | chunky→planar, `IndexedDisplay`, coste. |
| planificador_de_escena.md | `ScenePlan` + estrategias + `RasterLayout`. |
| sistema_de_objetos.md | Actores, sprite HW vs BOB, prioridad. |
| mini_os_de_mensajes.md | Puerto, VBlank, timers, E/S asíncrona, tareas. |
| audio_sintesis_y_streaming.md | Paula, mixer, música, streaming. |

Volver al [índice del manual](../README.md).
