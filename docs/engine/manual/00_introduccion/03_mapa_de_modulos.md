# Mapa de módulos

El engine son **23 módulos** bajo `engine/include/eng/`. Cada uno agrupa una responsabilidad; el
**orden** refleja dependencias (un módulo solo usa los que están por debajo, salvo la fachada).

```
  ┌──────────────────────────────────────────────────────────────────────────────┐
  │  api/          fachada pública (game, screen, scene, scroll, sprites,          │
  │                framebuffer, display, assets, copper, device, effects, wrender) │
  └───────────────▲──────────────────────────────────────────────┬───────────────┘
                  │ (usa todo lo de abajo)                       │
  ┌───────────────┴───────────────┐   ┌───────────────────────────┴──────────────┐
  │  scene/   World · ScenePlan   │   │  input/ · ui/ · debug/ · assets/ · retro/  │
  │           actores · planes    │   │  hw/ · cpu/ · parallel/ · task/            │
  └───────────────▲───────────────┘   └───────────────────────────▲──────────────┘
  ┌───────────────┴───────────────┐   ┌───────────────────────────┴──────────────┐
  │  field/   playfield · scroll  │   │  audio/  (mixer, música, streaming)        │
  │           Xlimited · Strip    │   │  os/     (mini-SO: msg, IO, timers, tasks) │
  └───────────────▲───────────────┘   │  res/    (assets, VFS, .engz, DynLoader)   │
  ┌───────────────┴───────────────┐   └───────────────────────────▲──────────────┘
  │  graphics/  planar · copper   │                               │
  │             blitter · compos. │   ┌───────────────────────────┴──────────────┐
  │             tilemap · effects │   │  ai/ (steering, nav, GOAP, decisión)        │
  └───────────────▲───────────────┘   │  sim/  board/  cards/  (IA y simulación)    │
  ┌───────────────┴───────────────┐   └───────────────────────────▲──────────────┘
  │  memory/   MemoryManager      │                               │
  │            arenas · BlockPool │                               │
  └───────────────▲───────────────┘                               │
  ┌───────────────┴───────────────────────────────────────────────┴──────────────┐
  │  core/   types (Byte/Word/Tag/Address/Box) · math (Vec/Mat/Fixed/MiniFloat)   │
  │          data (ct_array, byte_order) · util (expected, static_vector, pool)   │
  └──────────────────────────────────────────────────────────────────────────────┘
                  │
  ┌───────────────┴──────────────────────────────────────────────────────────────┐
  │  platform/   amiga/ (AmigaBackend, perfil de memoria) · (otros backends)      │
  └──────────────────────────────────────────────────────────────────────────────┘
```

## Responsabilidad de cada módulo

| Módulo | Responsabilidad | Cab. |
|---|---|---|
| `core/` | Tipos y utilidades **sin hardware**: `types`, `math` (genérica sobre escalar), `data`, `util`. | 95 |
| `memory/` | `MemoryManager`, arenas Chip/Slow/Fast, `BlockPool`, presupuesto. | 6 |
| `graphics/` | Pixel/planos, Copper, Blitter, composición de escena, tilemap, efectos, C2P. | 53 |
| `field/` | Playfields y **scroll** (tiras, corkscrew XLimited, geometría runtime). | 41 |
| `scene/` | `World`, plan de escena, actores, planes de DPF/bandas/raster. | 18 |
| `api/` | **Fachada pública** (lo único que un juego necesita incluir). | 13 |
| `audio/` | Mixer, modos de canal, música (Pt/P61), streaming. | 29 |
| `os/` | **Mini-SO**: puertos/mensajes, timers, E/S, VFS, tareas. | 19 |
| `res/` | Recursos: caché de assets, `.engz`, HUNK, ZX0, `DynLoader`. | 14 |
| `ui/` | Widgets, compositor, layout. | 24 |
| `ai/` | Steering, navegación, planificación (GOAP), decisión, percepción. | 15 |
| `sim/` | Simulación y generación. | 45 |
| `board/` | IA de **tablero** (reglas, búsqueda, evaluación, conocimiento). | 37 |
| `cards/` | IA de **naipes** (reglas, IA, evaluación, simulación). | 14 |
| `hw/` | Inventario de hardware, presupuesto de bus. | 2 |
| `debug/` | `RunStatus`, periférico de depuración, sonda de memoria, telemetría. | 5 |
| `cpu/` | Utilidades/emulación `m68k`. | 3 |
| `platform/` | Backend Amiga (`AmigaBackend`) y perfil de memoria. | 14 |
| `input/`, `parallel/`, `task/`, `assets/`, `retro/` | Módulos pequeños de propósito único. | 1–7 |

> Cab. = nº de cabeceras a la fecha (total **459**). La referencia del manual cubre **cada** módulo;
> ver el [checklist de cobertura](../README.md#5-cobertura-los-23-módulos--checklist-de-referencia).

Volver al [índice de introducción](README.md) · [índice del manual](../README.md).
