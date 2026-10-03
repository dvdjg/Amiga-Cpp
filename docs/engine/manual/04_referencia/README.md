# Referencia

**Toda** la superficie del engine, una sección por **módulo**. Para cada clase/plantilla/función:
propósito, firma, parámetros, errores, invariantes y `fichero:línea` al fuente.

## Módulos

| Módulo | Qué documenta |
|---|---|
| `api/` | La **fachada**: `App`/`Screen`/`Scene`/`Scroll`/`Sprites`/`IndexedDisplay`/`Assets`/`Device`/`Effects`/`Copper`. |
| `core/types/` | Dominios y `Tag`, vistas (`Bytes`/`Words`), `Address`, `Box`, `Span`, handles. |
| `core/math/` | `Vec`/`Mat`/`Affine`, `Fixed`, `MiniFloat16`, `scalar_traits`, ruido, `SinTable`, interpolación. |
| `core/data/` | `ct_array`, orden de bytes, binario, checksums. |
| `core/util/` | `expected`, `static_vector`, `pool`, `string_view`, `function_ref`, `scope_guard`. |
| `memory/` | `MemoryManager`, arenas, `BlockPool`, presupuesto. |
| `graphics/planar/` | Bitmaps, vistas de plano, `Surface`, blits, rasterizado. |
| `graphics/copper/` | `copper`, `Plan`, `Scheduler`, doble buffer. |
| `graphics/blitter/` | Estado de Blitter, BOB, sprites, asignador/manager de sprites. |
| `graphics/composition/` | `Scene`, `compose`, etapas, límites, *copper chunky*. |
| `graphics/tilemap/` | Tilemap, scroll por tiles, tabla de atributos, decodificación planar. |
| `graphics/effects/` | Rotozoom, fine-scroll, paletas, C2P. |
| `graphics/drivers/` | Drivers gráficos por estrategia. |
| `field/` | Playfields, `Xlimited*` (corkscrew), `Strip*`, geometría de scroll runtime. |
| `scene/` | `World`, `ScenePlan`, actores, planes de DPF/bandas/raster. |
| `input/` | Estado de entrada. |
| `os/` | Mini-OS: mensajes, puertos, timers, ficheros, VFS, requests, tareas. |
| `res/` | Caché de assets, `DynLoader`, `.engz`, HUNK, ZX0, decode, load, presupuesto. |
| `audio/` | Mixer, modos de canal, música (Pt/P61), streaming. |
| `ui/` | Widgets, compositor, layout. |
| `hw/` | Inventario de hardware, presupuesto de bus. |
| `debug/` | `RunStatus`, periférico de depuración, sonda de memoria, telemetría. |
| `cpu/` | Utilidades/emulación `m68k`. |
| `platform/amiga/` | Backend Amiga, perfil de memoria, servicios. |
| `ai/` | Steering, navegación, planificación (GOAP), decisión, percepción. |
| `sim/` | Simulación y generación. |
| `board/` | IA de tablero: reglas, búsqueda, evaluación, conocimiento, almacenamiento, explicación. |
| `cards/` | IA de naipes: reglas, IA, evaluación, simulación. |
| `parallel/`, `task/`, `assets/`, `retro/` | Módulos pequeños de propósito único. |

> Cobertura completa (459 cabeceras, 23 módulos). El estado por módulo se marca en el
> [checklist del índice](../README.md#5-cobertura-los-23-módulos--checklist-de-referencia).

Volver al [índice del manual](../README.md).
