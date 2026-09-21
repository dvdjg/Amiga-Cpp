# HOST-260 — Copper lanza blits (Técnica A)

Valida `CopperIntentKind::BlitterJob` + `Scheduler::emit_blitter_job`/`set_blitter_window`
(`eng/graphics/raster_intent.hpp`, `eng/graphics/copper/scheduler.hpp`):

- El intent programa `BLTCON0/1`, `BLTAFWM/ALWM`, módulos y punteros, y escribe **`BLTSIZE` al
  final** (arranca el blit sincronizado al haz).
- La **ventana segura**: el job solo se materializa si su línea cae dentro; fuera se cuenta como
  `unhandled_intents` y no escribe registros (serialización con los blits de CPU).

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/260_copper_blitter
```

Roadmap: `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnica A).
