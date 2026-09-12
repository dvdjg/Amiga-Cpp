# Demo 081 — Tareas de fondo cooperativas

Demuestra `eng::task::BackgroundQueue` (`engine/include/eng/task/background.hpp`,
test HOST-017): un **proceso pesado** avanza en los huecos del frame **sin bloquear**
el bucle principal.

## Qué se ve

- **Fondo (azul, COLOR00)**: lo pulsa el **bucle principal** por CPU cada frame (la
  copperlist no toca COLOR00) → prueba viva de que el juego sigue a 50 fps.
- **Barra blanca (COLOR01)**: la rellena la **tarea de fondo** fila a fila en una banda
  central. Su longitud es el progreso (`progress().permille`, también en
  `runStatus.detail`). Al completarse se reinicia (ciclo).
- **Línea amarilla (COLOR02)**: la dibuja el **bucle principal por Blitter**
  (`blitter_clear` + `blitter_line`) en una banda inferior. Sus esperas de Blitter
  (`wait_blitter`) son otro punto donde el engine drena el fondo
  (`MinimalBackend::set_blitter_service`).

## Invariantes / diseño

- Display 4 planos 320×256 con el driver `HamScene` (`row_repeat = 1`).
- La tarea se **adapta al barrido del CRT**: si `vpos > 220`, procesa la mitad por
  rebanada; el cupo por frame lo fija `BackgroundQueue::max_slices_per_frame` (aquí 1).
- La prioridad es del bucle principal: el fondo solo avanza en VBlank y en las esperas
  de Blitter, nunca durante `update`/`render`.

## Validación

```
bash ./tools/build/build-demo.sh demos/amiga/081_background_tasks --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/081_background_tasks --sequence-frames 6 --sequence-interval-ms 600
bash ./tools/analyze/analyze-demo.sh demos/amiga/081_background_tasks
```

Evidencia: la fracción blanca (la barra) varía entre frames de la secuencia (progresa
y se reinicia) y el color del fondo cambia (el bucle principal sigue pulsando);
`runStatus.detail` lleva `permille` (bits 16+) y el frame. Analizador propio
(`analyze-screenshot.sh`: exige blanco y fondo oscuro).
