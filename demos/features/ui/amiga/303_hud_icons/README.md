# Demo 303 — `hud_icons`: inventario con selección y foco (`eng::hud`, H2)

Demo de la fase **H2** del [`ROADMAP_HUD.md`](../../../../../docs/guides/roadmap/ROADMAP_HUD.md): una **rejilla de inventario 6×2** en una franja propia de 48 líneas a 2 planos (blanco/amarillo/rojo) sobre un campo de juego estático. El modelo `eng::hud` mantiene la rejilla (`IconGrid`) y **`HudInput`** consume los eventos de **ratón y joystick** para mover **selección** y **foco**; el presentador `HudStrip` pinta los deltas por CPU.

## Qué enseña

- **Entrada por el contrato de eventos**: el juego sondea el hardware (`eng::amiga::poll_mouse`/`poll_input`, patrón de la demo 215) y sintetiza `eng::ui::UiEvent`; `HudInput` los consume y devuelve un `Result` (consumido, *click*, selección/foco cambiados). El HUD no lee registros.
- **Consumo explícito por región**: la política sale de `HudRegionSpec::capture_input` (`HudInput::Policy::from_region`); con la franja capturando, el ratón de la banda no llega al juego, y fuera de la banda el juego recibe los eventos.
- **Hit-test barato**: rectángulos por **capa** (mayor delante; empate = último empujado) y rejillas por **fórmula** (`(p − origen)/pitch`, división entera: el *pitch* no tiene que ser múltiplo de 16); el índice de slot y el id lógico se calculan **al recibir el evento**, nunca por frame.
- **Selección ≠ foco**: el ratón y el *click* mueven la selección; el joystick/pad mueve el foco por filas/columnas (con bordes) y `Fire`/`Accept` selecciona la celda con foco. Los dos estados se pintan distinto y viven en el modelo (`grid_select`/`grid_move_focus`, con *dirty*).
- **Repintado por deltas**: solo se repintan las celdas cuyo estado (selección/foco/hover) cambia; la rejilla estática se pinta una vez.
- **Guion de demostración**: sin entrada real, un recorrido determinista por el perímetro (un paso cada 20 frames, un disparo cada 60) mantiene la demo viva y da una dinámica reproducible a la validación.
- **HUD de depuración** (AGENTS §0): línea `HUD ICONS vX.Y.BBBB F NNNNN FPS NN.N` de longitud fija por palabras, más el estado `SEL nn FOC nn`.

## Búsqueda previa

Antes de escribir la demo se comprobó qué existe ya en el engine para no duplicar:

- **Entrada del HUD**: `eng::hud::HudInput` + `hud_hit_test` (`engine/include/eng/hud/input.hpp`, verificado por HOST-469) en vez de una máquina de puntero propia; el contrato de eventos es `eng::ui::UiEvent` (`event.hpp`).
- **Sondeo de entrada**: `eng::amiga::poll_mouse`/`poll_input`/`MousePollState` (`engine/include/eng/platform/amiga/input_poll.hpp`; mismo patrón que la demo 215) con los tipos de dominio `eng::input::MouseState`/`PadState`/`InputAggregator` (`eng/input/input.hpp`).
- **Modelo y presentación**: `eng::hud` (`Hud`, `HudStrip`, `IconGrid`, `HudSlotView`, `HudDrawOp` con texto por valor) en `engine/include/eng/hud/`; la franja se pinta con `eng::playfield::Surface` sobre `CanvasPlayfield` (sin bucle por píxel).
- **Composición de bandas**: `eng::scene::Band`/`band_of_planes`/`band_from_view`/`RasterLayout` + `App::present_layout` (la vía de la demo 131), no `copper::Scheduler` a mano.
- **Fachada de juego**: `eng::GameDisplay` + `eng::amiga::run_demo` + `App::screen()`/`App::input()`; la línea de depuración reutiliza `Surface::draw_text_words` y `eng::debug::HudFpsWindow`.

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/features/ui/amiga/303_hud_icons --debug
bash ./tools/run/run-demo.sh demos/features/ui/amiga/303_hud_icons --keep-running
bash ./tools/analyze/analyze-demo.sh demos/features/ui/amiga/303_hud_icons
```

Verificación y medidas: [`VALIDATION.md`](VALIDATION.md) · Intención: [`src/INTENT.md`](src/INTENT.md) · Visión (prompt canónico): [`src/VISION.md`](src/VISION.md).

## Notas de implementación

- El estado del juego vive en **BSS** (convención del repo; `run_demo` ya deja `AmigaBackend`/`DemoHost`/`App` en la pila del lanzador).
- **Orden de pintado por banda** (`HUD_SYSTEM.md` §9.3): `draw_world()` + `present()` primero (aquí el campo es estático, sin jobs) y la franja después por CPU.
- **Los rellenos de la franja van por CPU** (`HudStrip::bind` fija `AccelMode::Cpu`): son rectángulos pequeños (celdas 16×16, marcos de 1 px) y el arranque del Blitter los encarecería (`ATRIBUCION_DE_CICLOS.md` antipatrón 3).
- **El joystick se consume por flanco** (una pulsación = un paso de foco): la repetición es responsabilidad del servicio de entrada, no del widget (`HUD_SYSTEM.md` §7.3).
- El **hover** lo lleva la demo (la celda bajo el ratón), no el modelo: el `Result` del input da el slot resuelto y la demo repinta solo el delta.
