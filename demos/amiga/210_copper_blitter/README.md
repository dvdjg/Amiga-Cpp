# 210 — Copper lanza blits, Técnica B y borde de scroll

Tres usos del Blitter, verificados en el demo y en `RunStatus` (`detail = 0x21FFF`):

1. **Técnica A**: el **Copper programa el Blitter y escribe `BLTSIZE`** en la línea 304 (borde
   inferior) — blit **sincronizado al haz**. Un `CopperIntentKind::BlitterJob` +
   `Scheduler::emit_blitter_job` programa `BLTCON0/1`, `BLTAFWM/ALWM`, módulos, punteros y
   **`BLTSIZE` al final**; la **ventana segura** (`set_blitter_window`) lo limita al borde
   inferior. El demo verifica la copia (`src`→`dst`, 256 words) al frame siguiente.
2. **Técnica B**: un `graphics::BlitJob` (`CopyRect`, 1 word de ancho, `dst_mod = 2`) parchea los
   **data words** de 8 MOVEs consecutivos sin tocar los registros; la lista se lee de vuelta y se
   comprueba.
3. **Borde de scroll**: la pantalla (320 px, 1 plano) se desplaza una columna a la izquierda con
   un `BlitJob` (`CopyRect` 19×256, `mods = 2`) y la **columna nueva** entra por la derecha con
   otro `BlitJob` (1×256, `dst_mod = row_bytes − 2`); el buffer se verifica contra el patrón.

Los blits sueltos se envían con **`MinimalBackend::blitter_submit(const BlitJob&, wait)`** (un
job); `execute_frame_plan` encadena varios por el **mismo camino** (`submit_blit_job`).

## Serialización Copper↔CPU (clave)

El Blitter es **único**. El scroll de CPU lanza blits largos (≈5 100 words ≈ 140 líneas) al
principio del frame; si el blit del Copper cae **mientras corren**, su `BLTSIZE` **aborta** el de
CPU y el display se rompe. Por eso el blit del Copper va en el **borde inferior (línea 304)**,
con la ventana segura fuera del tramo del blit de CPU. Reproducido y aislado: con el scroll de
CPU desactivado, un blit de Copper en el borde superior no rompe nada; con el scroll activo y el
blit de Copper en la misma franja, sí.

## Uso

```
   bash ./tools/build/build-demo.sh demos/amiga/210_copper_blitter --debug
   bash ./tools/run/run-demo.sh demos/amiga/210_copper_blitter --warp
```

## Estado: verificado

`RunStatus.detail = 0x21FFF`; Ollama (`qwen3-vl:8b-instruct-q8_0`) confirma rayas diagonales
blancas sobre azul, sin anomalías. Host: `tests/host/260_copper_blitter`.

## Referencias

- `docs/reference/amiga/techniques/blitter-memcpy.md` §Concurrencia.
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnicas A y B).
- `docs/reference/emulators/winuae/copper.md` (`CDANG`).
- `engine/include/eng/graphics/{blit_job.hpp,raster_intent.hpp}` +
  `engine/include/eng/graphics/copper/scheduler.hpp` +
  `engine/include/eng/platform/amiga_minimal.hpp` (`blitter_submit`).
