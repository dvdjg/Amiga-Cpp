# 210 — Copper lanza blits, Técnica B y borde de scroll

Tres usos del Blitter, verificados en el demo y en `RunStatus` (`detail = (fine << 16) | 0x1FFF`, con el valor de scroll fino `fine` en los bits 16-23):

1. **Técnica A**: el **Copper programa el Blitter y escribe `BLTSIZE`** en la línea 304 (borde
   inferior) — blit **sincronizado al haz**. Un `CopperIntentKind::BlitterJob` +
   `Scheduler::emit_blitter_job` programa `BLTCON0/1`, `BLTAFWM/ALWM`, módulos, punteros y
   **`BLTSIZE` al final**; la **ventana segura** (`set_blitter_window`) lo limita al borde
   inferior. El demo verifica la copia (`src`→`dst`, 256 words) al frame siguiente.
2. **Técnica B**: un `graphics::BlitJob` (`CopyRect`, 1 word de ancho, `dst_mod = 2`) parchea los
   **data words** de 8 MOVEs consecutivos sin tocar los registros; la lista se lee de vuelta y se
   comprueba.
3. **Borde de scroll fino** (`eng::effects::FineScroll`): la pantalla (320 px, 1 plano) es un
   buffer anular de 21 words/fila. Cada frame avanza **`BPLCON1` 1 px** (scroll fino); al cruzar
   los 16 px, un `BlitJob` (`CopyRect` 20×256, `mods = 2`) desplaza la pantalla una columna y otro
   `BlitJob` (1×256, `dst_mod = row_bytes − 2`) escribe la **columna nueva** en el word 20. El
   helper `effects::FineScroll` (promovido desde la demo) da `bplcon1()`, `ddfstrt() = $30` y los
   dos `BlitJob`; el patrón procedural y la verificación del buffer son de la demo. El patrón del
   driver con *ring wrap* real es `graphics/drivers/tile_scroll.hpp`.

Los blits sueltos se envían con **`MinimalBackend::blitter_submit(const BlitJob&, wait)`** (un
job); `execute_frame_plan` encadena varios por el **mismo camino** (`submit_blit_job`).

## Serialización Copper↔CPU (automática)

El Blitter es **único**. El scroll de CPU lanza blits largos al principio del frame; si el blit del
Copper cae **mientras corren**, su `BLTSIZE` **aborta** el de CPU y el display se rompe. Por eso la
ventana no es una línea cableada: se calcula con
**`graphics::safe_blitter_window(0, current_raster_line(), border_line, last_line)`**, que toma como
suelo la **línea de raster real al terminar los blits de CPU** (`MinimalBackend::current_raster_line`)
y el borde inferior. Reproducido y aislado en el hilo (sin el blit de CPU, un blit de Copper en el
borde superior no molesta; con el de CPU en la misma franja, sí).

## Uso

```
   bash ./tools/build/build-demo.sh demos/amiga/210_copper_blitter --debug
   bash ./tools/run/run-demo.sh demos/amiga/210_copper_blitter --warp
   bash ./demos/amiga/210_copper_blitter/analyze-sequence.sh --warp
```

## Estado: verificado

`RunStatus.detail = (fine << 16) | 0x1FFF` (`fine` en bits 16-23; permite la captura
frame-exacta por valor de `fine` con `--sequence-fine-x`); Ollama
(`qwen3-vl:8b-instruct-q8_0`) confirma rayas diagonales blancas sobre azul, sin anomalías.

- **Host**: `tests/host/260_copper_blitter` (ventana segura) y `tests/host/261_fine_scroll`
  (cadencia de 1 px/frame del helper).
- **Emulador (1 px/frame)**: `analyze-sequence.sh` captura frames **consecutivos** con
  `--sequence-step-frames 14 --sequence-step-start-fine 2` (congelando la CPU en el *ready
  probe*, 1 frame entre capturas) y el `pixel-contract.json` verifica con
  `shifted_region_match` (dx = −1 px lógico = −2 px de imagen a 2×) que el contenido se
  desplaza exactamente 1 px por frame (peor error ≈ 0,07 %).

## Referencias

- `docs/reference/amiga/techniques/blitter-memcpy.md` §Concurrencia.
- `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md` (Técnicas A y B).
- `docs/reference/emulators/winuae/copper.md` (`CDANG`).
- `docs/build/BUILD_AND_RUN.md` §Convención de `detail` (`cameraX`/`--sequence-fine-x`).
- `engine/include/eng/graphics/{blit_job.hpp,raster_intent.hpp}` +
  `engine/include/eng/graphics/copper/scheduler.hpp` +
  `engine/include/eng/platform/amiga_minimal.hpp` (`blitter_submit`).
