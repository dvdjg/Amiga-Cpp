# 210 — Copper lanza blits, Técnica B y borde de scroll

Tres usos del Blitter, todos verificados en el propio demo y en `RunStatus` (`detail = 0x21FFF`):

1. **Técnica A**: el **Copper programa el Blitter y escribe `BLTSIZE`** en una línea del raster
   (blit **sincronizado al haz** sin coste de CPU por el arranque). Un
   `CopperIntentKind::BlitterJob` + `Scheduler::emit_blitter_job` programa `BLTCON0/1`,
   `BLTAFWM/ALWM`, módulos, punteros y **`BLTSIZE` al final**. La **ventana segura**
   (`set_blitter_window`) lo limita al borde inferior (línea 304), serializado con los blits de
   CPU.
2. **Técnica B**: un `graphics::BlitJob` (`CopyRect`, 1 word de ancho, `destination_modulo_bytes
   = 2`) parchea los **data words** de 8 MOVEs consecutivos con el Blitter, sin tocar los
   registros; la lista se lee de vuelta y se comprueba.
3. **Borde de scroll**: la pantalla es un buffer anular de 21 words/fila (320 px visibles + 1
   columna). Cada frame un `BlitJob` (`CopyRect` 20×256, `mods = 2`) desplaza la pantalla una
   columna a la izquierda y otro (`CopyRect` 1×256, `dst_mod = row_bytes-2`) escribe la **columna
   nueva** a la derecha; el buffer resultante se verifica contra el patrón procedural.

Los blits sueltos se envían con **`blitter_submit(const graphics::BlitJob&, wait)`** (un job);
`execute_frame_plan` encadena varios por el **mismo camino** (`submit_blit_job`). Así no hay
firmas propias con `u16*`+`s16` ni tipos nuevos: el descriptor `BlitJob` ya modela
`words_per_row`/`height`/`source_modulo_bytes`/`destination_modulo_bytes`/`minterm`/planos.

`COPCON`/`CDANG`: el Copper solo puede escribir los registros del Blitter (<0x80) si
`takeover_display` activa `CDANG` (`COPCON` bit 1); sin él, la primera escritura **detiene** el
Copper (`custom.cpp:2835-2840`). Ver `docs/reference/emulators/winuae/copper.md`.

```
   bash ./tools/build/build-demo.sh demos/amiga/210_copper_blitter --debug
   bash ./tools/run/run-demo.sh demos/amiga/210_copper_blitter --warp
```

## Estado: verificado

Overlay con `copper blit (Tecnica A): OK`, `copperlist patch (Tecnica B): OK` y
`scroll edge (shift + columna): OK`; `RunStatus.detail = 0x21FFF`. Host:
`tests/host/260_copper_blitter` (emisión + ventana segura).

## Referencias

- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnicas A y B).
- `docs/reference/emulators/winuae/copper.md` (`CDANG`).
- `engine/include/eng/graphics/raster_intent.hpp` (`BlitterJob`, `BlitterWindow`).
- `engine/include/eng/graphics/copper/scheduler.hpp` (`emit_blitter_job`, `set_blitter_window`).
- `engine/include/eng/graphics/blit_job.hpp` (`BlitJob`) +
  `engine/include/eng/platform/amiga_minimal.hpp` (`blitter_submit`).
