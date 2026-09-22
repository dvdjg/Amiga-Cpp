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
3. **Borde de scroll fino**: la pantalla (320 px, 1 plano) es un buffer anular de 21 words/fila.
   Cada frame avanza **`BPLCON1` 1 px** (scroll fino); al cruzar los 16 px, un `BlitJob`
   (`CopyRect` 20×256, `mods = 2`) desplaza la pantalla una columna y otro `BlitJob` (1×256,
   `dst_mod = row_bytes − 2`) escribe la **columna nueva** en el word 20. El buffer se verifica
   contra el patrón procedural. `DDFSTRT = $30` fetcha la word extra que exige el fine scroll
   (patrón del driver `graphics/drivers/tile_scroll.hpp`).

Los blits sueltos se envían con **`MinimalBackend::blitter_submit(const BlitJob&, wait)`** (un
job); `execute_frame_plan` encadena varios por el **mismo camino** (`submit_blit_job`).

## Serialización Copper↔CPU (automática)

El Blitter es **único**. El scroll de CPU lanza blits largos (`kCpuBlitWords ≈ 5 300` words) al
principio del frame; si el blit del Copper cae **mientras corren**, su `BLTSIZE` **aborta** el de
CPU y el display se rompe. Por eso la ventana no es una línea cableada: se calcula con
**`graphics::safe_blitter_window(cpu_blit_words, cpu_start_line, border_line, last_line)`**, que
coloca el `BLTSIZE` del Copper **después** del fin estimado del blit de CPU (`blitter_lines`), con
el borde inferior como suelo. Reproducido y aislado en el hilo (sin el blit de CPU, un blit de
Copper en el borde superior no molesta; con el de CPU en la misma franja, sí).

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
