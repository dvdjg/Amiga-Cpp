# 210 — Copper lanza blits (Técnica A)

Demuestra que el **Copper programa el Blitter y escribe `BLTSIZE`** en una línea del raster:
un blit **sincronizado al haz** sin coste de CPU por el arranque.

- `CopperIntentKind::BlitterJob` + `Scheduler::emit_blitter_job`: programa `BLTCON0/1`,
  `BLTAFWM/ALWM`, módulos, punteros y **`BLTSIZE` al final** (arranca el blit).
- **Ventana segura** (`Scheduler::set_blitter_window`): el job solo se materializa en el borde
  inferior (línea 304), serializado con los blits de CPU.
- **`COPCON`/`CDANG`**: el Copper solo puede escribir los registros del Blitter (<0x80) si
  `takeover_display` activa `CDANG` (`COPCON` bit 1); sin él, la primera escritura detiene el
  Copper (`custom.cpp:2835-2840`).

El demo copia 256 words (`src`→`dst`, `D = A`) en cada frame; al siguiente verifica la copia y
la publica en el overlay y en `RunStatus` (detalle `0x21F00` = OK).

Además valida la **Técnica B** (Blitter → copperlist) con `blitter_patch_copper_data`: el
Blitter escribe los **data words** de 8 MOVEs consecutivos (`BLTDMOD = 2`, stride 4 B) sin tocar
los registros; la lista se lee de vuelta y se comprueba. Con ambas OK el detalle es `0x21FFF`.

```
   bash ./tools/build/build-demo.sh demos/amiga/210_copper_blitter --debug
   bash ./tools/run/run-demo.sh demos/amiga/210_copper_blitter --warp
```

## Estado: verificado

Host: `tests/host/252_copper_blitter` (emisión + ventana segura). Demo: `copper blit: OK` y
`RunStatus.detail = 0x21F00`.

## Referencias

- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnica A).
- `engine/include/eng/graphics/raster_intent.hpp` (`BlitterJob`, `BlitterWindow`).
- `engine/include/eng/graphics/copper/scheduler.hpp` (`emit_blitter_job`, `set_blitter_window`).
