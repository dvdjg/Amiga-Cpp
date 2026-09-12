# Demo 081 — Tareas de fondo cooperativas

Demuestra `eng::task::BackgroundQueue` (`engine/include/eng/task/background.hpp`, test
HOST-017) en el **modo interrupt-driven**: la **IRQ de VBlank** corre el **juego**
(update/render, con *deadline* de 1 frame) y el **bucle principal** ejecuta el trabajo
de **fondo** cooperativo, que la IRQ preempta.

## Qué se ve

- **Fondo (azul, COLOR00)**: lo pulsa el **juego** (en la IRQ de VBlank) por CPU cada frame
  (la copperlist no toca COLOR00) → prueba viva de que el latido del juego va a 50 fps.
- **Barra blanca (COLOR01)**: la rellena la **tarea de fondo** fila a fila en una banda
  central, desde el bucle principal. Su longitud es el progreso (`progress().permille`,
  también en `runStatus.detail`). Al completarse se reinicia (ciclo).
- **Línea amarilla (COLOR02)**: la dibuja el **juego** (en la IRQ) por Blitter
  (`blitter_clear` + `blitter_line`). Sus esperas de Blitter (`wait_blitter`) son otro punto
  donde se drena el fondo (`MinimalBackend::set_blitter_service`).

## Invariantes / diseño

- Display 4 planos 320×256 con el driver `HamScene` (`row_repeat = 1`).
- **Modo interrupt-driven** (`Engine::run_frames`, el **por defecto**): el tick del juego
  (update+render) corre en la IRQ de VBlank (`support/vbl_irq.s` + `set_vblank_service`); el
  bucle principal hace `while (frames < N) background.run_slice(...)`. La IRQ tiene prioridad
  dura (preempta al fondo); cuando no hay más juego que hacer, vuelve (`RTE`) y el fondo sigue.
- La tarea **se adapta al barrido del CRT**: si `vpos > 220`, procesa la mitad por rebanada.
  El cupo por frame (`max_slices_per_frame`) acota cuánto fondo se hace por frame.

## Validación

```
bash ./tools/build/build-demo.sh demos/amiga/081_background_tasks --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/081_background_tasks --sequence-frames 6 --sequence-interval-ms 600
bash ./tools/analyze/analyze-demo.sh demos/amiga/081_background_tasks
```

Evidencia: la fracción blanca (la barra) varía entre frames de la secuencia (progresa y se
reinicia) y el color de fondo cambia (el juego sigue pulsando desde la IRQ); la captura
tiene 4 colores (fondo, barra, **línea amarilla** por Blitter, borde). `runStatus.detail`
lleva `permille` (bits 16+) y el frame. Analizador propio (`analyze-screenshot.sh`).

Ver `docs/engine/architecture/BACKGROUND_TASKS.md`.
