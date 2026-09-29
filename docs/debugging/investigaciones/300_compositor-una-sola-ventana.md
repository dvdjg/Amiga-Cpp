# Demo 300 (compositor GUI): solo se compone una ventana en hardware

Bitácora del hallazgo. El estado vigente del compositor está en [`docs/engine/architecture/GUI_LIBRARY.md`](../../engine/architecture/GUI_LIBRARY.md) §14 y el plan en [`ROADMAP_GUI.md`](../../guides/roadmap/ROADMAP_GUI.md).

## Síntoma

La demo `demos/features/ui/amiga/300_gui_compositor` arranca, alcanza READY y captura. La **captura de un solo frame** (`screenshot.png`) mostraba solo la ventana B, lo que sugirió que faltaban A y C. Al capturar una **secuencia larga** (16–20 frames) aparecen las **tres ventanas** correctamente.

## Reproducción

```bash
export AMIGA_BIN_PATH='…/vscode-amiga-debug/bin/win32'
export WINUAE_GDB_PORT=2355 WINUAE_SIDE_CHANNEL_PORT=2421
bash tools/build/build-demo.sh demos/features/ui/amiga/300_gui_compositor --clean
bash tools/run/run-demo.sh demos/features/ui/amiga/300_gui_compositor --warp \
  --sequence-frames 20 --sequence-interval-ms 60
```

## Estado y descartes

- **La captura de un solo frame no es representativa.** El `screenshot.png` se toma en un instante temprano; con secuencia se ven A, B y C. No hay defecto de composición: el escritorio y las zonas estáticas no cambian entre frames.
- **No lo introduce el cambio de blit con *shift*.** Idéntico comportamiento con el engine previo (verificado con `git stash`).
- **Verificación determinista:** `out/tmp/framediff.cjs <seq>` (diff píxel a píxel entre frames consecutivos). Con la animación original (pasos de 16 px y `f % n`) mostraba **frames idénticos consecutivos** (congelaciones de ~120 ms) y saltos grandes; el "parpadeo" percibido era del **movimiento**, no un glitch.
- **Visión (Ollama):** el `flicker-check` marca zonas de alta oscilación, pero estas corresponden a las ventanas **en movimiento**; no distingue movimiento de glitch. Una consulta con pregunta crítica (`ollama-desc.mjs`) **alucinó** coordenadas fuera de la imagen. Para parpadeo, la referencia fiable es el frame-diff determinista.

## Corrección aplicada

`animate()` de la demo 300 pasa a mover cada ventana **1 px por frame** con rebote triangular (sin saltos de 16 px y sin tramos congelados). El compositor lo sostiene con la ruta Blitter (destino alineado o *shift* de origen). El frame-diff posterior muestra cambio en casi todos los frames.

## Lección

Para juzgar parpadeo/animación: (1) capturar **secuencia**, nunca un frame suelto; (2) usar **frame-diff determinista** para separar movimiento de glitch; (3) la visión artificial es apoyo, no veredicto (puede alucinar). Ver [DEMO_VISUAL_DEBUG.md](../../guides/methodology/DEMO_VISUAL_DEBUG.md) y [PROTOCOLO_ETAPAS_GRAFICOS.md](../../guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md).
