# El camino de un frame

Un frame del engine dura **un VBlank PAL** (~20 ms a 50 Hz). El backend marca el latido; el `App`
corre, una vez por latido, `update` (lógica) y `render` (dibujo + publicación).

## Vista de alto nivel (el `App`)

```
   ┌─ ARRANQUE (una vez) ────────────────────────────────────────────────┐
   │  main(): backend.configure_memory(...)                               │
   │  App app {backend, game, memoria};                                   │
   │  app.set_display(display);  app.start()   // compone y toma el display│
   │  game.init(app)                           // tus recursos (una vez)   │
   └──────────────────────────────────────────────────────────────────────┘
                                   │
        ┌──────────────────────────┴──────────────────────────────────┐
        │                    POR CADA VBLANK (50 Hz)                   │
        │                                                              │
        │  VBlank ──► update(app) ──► render(app) ──► present()         │
        │   (IRQ)      lógica del     dibujo al       ejecuta blits +   │
        │              frame          plan del frame  publica Copper    │
        └──────────────────────────────────────────────────────────────┘
```

## Por dentro (lo que hace el `App` en `update`/`render`)

```
  update(app)                                  render(app)
  ├─ (escena activa o Game).update(app)         ├─ begin_async_frame()
  │                                             ├─ materializa el mundo (World → plan)
  └─ pump_scroll_layers()                       ├─ (escena activa o Game).render(app)
     └─ cada capa registrada: frame(backend)     │    └─ app.screen()... ; app.present()
        (avanza scroll, blits, Copper)           └─ …
```

El engine pasa `init`/`update`/`render` a tu tipo `Game` (o a la **escena** superior, si hay pila de
escenas). El **contexto de dibujo** es `app.screen()`; el **plan del frame** (blits de Blitter) se
ejecuta en `present()`, que además publica la copperlist en VBlank (`commit`).

## Del píxel al DMA (lo que ve el hardware)

```
  app.screen().fill(...)      ──►  FramePlan (jobs de Blitter/CPU)
  screen.sprite(...)          ──►  BobTarget + blit
  app.emit_bobs_banded(...)   ──►  blits por banda
        │
        v   present()
  ┌─────────────────────────────────────────────────────────────┐
  │ 1. Blitter ejecuta los jobs (copias/rellenos/BOBs)          │
  │ 2. Se parchean los BPLxPT de la copperlist (doble buffer)   │
  │ 3. commit() publica la copperlist nueva en VBlank (COP1LC)  │
  └─────────────────────────────────────────────────────────────┘
        │
        v
  Agnus lee los bitplanes por DMA; Denise dibuja; el Copper escribe
  registros por línea (paleta, scroll, modo).  ->  imagen en pantalla
```

## Puntos clave

- **`render` es el punto de publicación, no de simulación**: instalar una copperlist fuera de VBlank
  parte el frame. El engine te da el punto correcto (el `commit` tras `wait_vblank`).
- **`update` va después del VBlank**: es donde va la lógica del frame (el «NMI» de muchos ports).
- **Doble buffer**: con `Buffers ≥ 2`, dibujas el *trasero* mientras el display lee el *delantero*; el
  swap ocurre en VBlank → sin tearing.
- **Solape CPU↔Blitter**: `app.set_async_present(true)` + `App::blitter_memcpy_async` para encadenar
  trabajo sin bloquear.

Detalle reproducible: `tests/host/ui/240_app_scenes` (pila de escenas) y `demos/features/engine/amiga/`
(ejemplos vivos). Fuente: `engine/include/eng/api/game.hpp` (`Adapter`, `run`).

Volver a [Arquitectura](README.md) · [índice del manual](../README.md).
