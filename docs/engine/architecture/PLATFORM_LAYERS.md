# Capas de plataforma y contrato de backend

Modelo canónico de **separación por plataformas** del engine. Responde a tres preguntas: qué es común a cualquier máquina, qué es común a toda la familia Amiga y qué es específico de una máquina (A500 frente a A1200); y cómo se acopla una plataforma nueva (Atari ST, Megadrive) sin tocar el dominio. El plan de organización general está en [`../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md`](../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md).

## 1. Los tres anillos

```
┌─ Anillo 0: DOMINIO (agnóstico de máquina) ─────────────────────────────┐
│ eng/core, field, scene, graphics/{composition,effects,drivers,tilemap}, │
│ ai, sim, board, cards, ui, os, input, audio (mixer/planos), res,        │
│ parallel, task, debug, hw                                              │
│                                                                        │
│  Habla de INTENCIONES portables, no de registros:                       │
│    Visual / CopperIntent / SpriteIntent / Effect   (raster_intent.hpp)  │
│    RasterCaps / RasterPolicy / Rasterizer           (field/raster.hpp)  │
│  No conoce DMA, Copper, Paula ni CIA.                                   │
├─ Anillo 1: VOCABULARIO DE CHIPSET (por familia) ───────────────────────┤
│ eng/cpu/m68k          CPU 68000 (la comparten Amiga, Atari ST, Megadrive)│
│ eng/platform/amiga    registros custom, Copper, Blitter, Paula, CIA,    │
│                         decodificación de entrada (joystick/CD32/teclado)│
│ eng/platform/atarist  (futuro) Shifter, YM2149, MFP 68901, IKBD         │
│ eng/platform/megadrive (futuro) VDP, YM2612, Z80 de audio               │
├─ Anillo 2: BACKEND (implementación por objetivo) ──────────────────────┤
│ engine/src/platform/amiga/       backend Amiga (OCS/ECS/AGA)            │
│ engine/src/platform/atarist/     (futuro)                               │
│ engine/src/platform/megadrive/   (futuro)                               │
└────────────────────────────────────────────────────────────────────────┘
```

Regla de dependencia: cada anillo **solo conoce a los de arriba** (el dominio nunca conoce el vocabulario de chipset ni el backend). La frontera la valida `tools/check/platform-boundaries.mjs`: las cabeceras de dominio no pueden incluir `eng/platform/<familia>` ni referenciar `$dff`/registros custom. `eng/cpu/m68k` queda permitido (es de CPU, no de plataforma).

## 2. Qué es común y qué es específico en Amiga

| Nivel | Contenido | Cómo se modela |
|---|---|---|
| **Común a todo Amiga** (OCS/ECS/AGA) | Mapa de registros custom `$DFF000`, Copper, Blitter, Paula (4 canales), CIA-A/B, modelo Chip/Slow/Fast, ROM kernel, 68000 | `eng/platform/amiga/` + `engine/src/platform/amiga/` |
| **OCS (A500)** | Bus de chip de 16 bits, sin `FMODE`, 512 KB Chip + 512 KB trapdoor típicos, sin Fast RAM de base | `HardwareProfile a500_1mb_slow` |
| **AGA (A1200/A4000/CD32)** | Fetch de 32/64 bits (`FMODE`), más bitplanes y paleta, Fast RAM, 68020, Akiko (CD32) | macro `K_AGA` + `raster_caps()` |
| **Timing** | PAL 50 Hz / NTSC 60 Hz | `HardwareProfile::pal` |

Conclusión: **A1200 no es un backend distinto; es el mismo backend Amiga con otro perfil y otro target de compilación.** El backend se parametriza con un perfil de máquina (RAM, timing) y con la macro de target (`K_AGA`), y expone sus capacidades por `raster_caps()`. La diferencia Amiga/Atari/Megadrive sí es un backend distinto.

## 3. El contrato de backend

Un backend es un tipo que satisface el **contrato** que consume `eng::Engine` y los drivers. No es una clase base ni una jerarquía: el engine lo usa por *duck typing* con `requires` (C++23), de modo que un backend con menos capacidades simplemente omite los servicios que no ofrece (p. ej. un backend host sin Blitter ni IRQ de VBlank).

Contratos vigentes:

- `eng::GameModule` / `eng::GameIdle` (`engine/engine.hpp`): lo que implementa el juego.
- `eng::DisplayDriver` / `eng::GraphicsDriver` (`graphics/driver.hpp`): ciclo de instalación del display.
- Servicios de backend usados por `Engine` (opcionales, detectados con `requires`): `boot`, `wait_vblank`, `current_raster_line`, `set_vblank_service`/`clear_vblank_service`, `set_blit_service`/`clear_blit_service`, `set_blitter_service`, `set_audio_service`, `memory`, `raster_caps`, `install_raster`.
- Capacidades: `field::RasterCaps` (¿hay Blitter? ¿bus de 16/32/64 bits?) permite que el dominio elija ruta sin saber la máquina.

`eng/platform/backend.hpp` reúne el vocabulario del contrato para que «soportar Atari ST» sea *satisfacer el contrato*, no copiar el backend Amiga.

## 4. Cómo se acopla una plataforma nueva

1. **Reutilizar el dominio tal cual**: `eng/core`, `eng/cpu/m68k`, `eng/retro`, `eng/field`, `eng/scene`, `eng/ai`, `eng/sim`, `eng/board`, `eng/cards`, `eng/ui`, `eng/os`, `eng/parallel`, `eng/res`, `eng/audio` (mixer/planos). El audio de juego habla de `AudioPlan`, no de Paula.
2. **Crear el vocabulario de chipset** en `eng/platform/<familia>/` (registros, formatos de display, decodificación de entrada).
3. **Crear el backend** en `engine/src/platform/<familia>/` que satisfaga el contrato y traduzca las intenciones portables (`Visual`/`CopperIntent`/`SpriteIntent`/`Effect`) a su hardware. Si la máquina no tiene Copper, esas intenciones se resuelven con otra técnica (raster, sprites de hardware, CPU).
4. **Declarar capacidades** (`RasterCaps` y equivalentes) para que el dominio no asuma Blitter ni Copper.
5. **Perfil de máquina** vía `eng::hw` (no duplicar `HardwareProfile`).
6. **Verificar** con `tests/<plataforma>/l0_bare_metal/` y con los tests host del vocabulario de chipset (`tests/host/platform/<familia>/`).

### Ejemplos

- **Atari ST**: comparte el 68000 (`eng/cpu/m68k`). Shifter (vídeo, sin Copper ni Blitter en ST; Blitter en Mega ST/STE), YM2149 (sonido), MFP 68901 (IRQ/timers, sustituye a CIA), IKBD (entrada). El backend declara `raster_caps().blitter = false` y resuelve las intenciones por CPU.
- **Megadrive**: 68000 + Z80 (audio) + VDP (tile-based, sin Copper/Blitter al estilo Amiga). El backend traduce las intenciones a sprites/planos del VDP.

## 5. Estado de la consolidación

- `eng/platform/amiga/` agrupa el vocabulario Amiga (`backend.hpp`, `paula.hpp`, `input_poll.hpp`, `blob.hpp`, `gfx3d.hpp`, `lib3d.hpp`, `object3d.hpp`, `object3d_poly.hpp`, `polygon_fill.hpp`).
- Las rutas antiguas (`eng/platform/amiga/backend.hpp`, `eng/platform/amiga/paula.hpp`, `eng/platform/amiga/input_poll.hpp`) se conservan como cabeceras-paraguas de compatibilidad mientras se migran los consumidores.
- El backend canónico es `eng::amiga::AmigaBackend` (alias `MinimalBackend` por compatibilidad).
