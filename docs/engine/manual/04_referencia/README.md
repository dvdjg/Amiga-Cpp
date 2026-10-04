# Referencia

**Toda** la superficie del engine, una sección por **módulo**. Para cada clase/plantilla/función:
propósito, firma, parámetros, errores, invariantes y `fichero:línea` al fuente.

## Módulos

| Módulo | Qué documenta |
|---|---|
| `api/` | La **fachada**: `App`/`Screen`/`Scene`/`Scroll`/`Sprites`/`IndexedDisplay`/`Assets`/`Device`/`Effects`/`Copper`. |
| [`core/`](core/README.md) | Base sin hardware: tipos, dominios, matemáticas, datos y utilidades. |
| [`core/types/`](core/types.md) | Tipos base, dominios y `Tag`, vistas (`Bytes`/`Words`/`ChipView`), `Address`, `Box`, `Span`, `Ref`/`NonNull`, `Block`. |
| [`core/math/`](core/math.md) | Escalar (`scalar_traits`, `Vec`/`Mat`), `Fixed`, `MiniFloat16`, `SinTable`, ruido, interpolación. |
| [`core/data/`](core/data.md) | `ct_array`, orden de bytes, `crc32`, ordenación, `utf8`, `rtc`, 3D (`mesh3d`/`polygon`). |
| [`core/util/`](core/util.md) | `expected`, `static_vector`, `pool`, `string_view`, `function_ref`, `scope_guard`, contenedores. |
| [`memory/`](memory/README.md) | `MemoryManager`, bancos (`MemBank`), arenas, `BlockPool`, `ChipStorage`, pilas. |
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
