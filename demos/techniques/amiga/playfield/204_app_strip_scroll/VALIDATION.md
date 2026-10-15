# VALIDATION — Demo 204 (`app_strip_scroll`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (update no se ejecuta bajo el runner)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| run | `run-demo.sh …` | **READY** | arranca bien |
| fps debug | `measure-fps 204_app_strip_scroll A500_debug --json` | **0 fps · frames=0** (con READY) | el contador de frames no avanza |
| cadencia (por intervalo, 6 frames) | `run-demo.sh … --settle-ms 2000 --sequence-frames 6 --sequence-interval-ms 350` | pares: **0,0 ×5** | pantalla estática |

## Diagnóstico

- `update(app)` llama `mark_frame` y el avance del scroll, pero el contador queda a 0 y la
  pantalla no cambia → **`update` no se está invocando** bajo el runner (mismo patrón que 132;
  el `render` sí corre). La captura por paso no funciona (sin hook de probe en este bucle).

## Deuda registrada

132/204: `update` no se ejecuta bajo el runner — revisar el bucle del App (¿hook no enlazado?).
Mientras: cadencia/fps no medibles.
