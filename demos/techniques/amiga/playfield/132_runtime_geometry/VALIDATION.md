# VALIDATION — Demo 132 (`runtime_geometry`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (update no se ejecuta bajo el runner)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| run | `run-demo.sh … --side-channel-timeout-ms 150000` | **READY en ~9 s** (`detail=0x13200000`) | arranca bien |
| fps debug | `measure-fps 132_runtime_geometry A500_debug --json` | **0 fps · frames=0** (con READY) | el contador de frames no avanza |
| cadencia (por intervalo, 6 frames) | `run-demo.sh … --settle-ms 2000 --sequence-frames 6 --sequence-interval-ms 350` | pares: **0,0 ×5** | pantalla estática |

## Diagnóstico

- `update(app)` llama `mark_frame` + `m_route.advance(app.frame())`, pero el contador queda a 0 y
  la pantalla no cambia → **`update` no se está invocando** bajo el runner (el `render` sí corre:
  el READY con `frame>=4` lo prueba). El avance de la ruta vive en `update` → pantalla estática.
- La captura por paso no funciona (no hay `probe_when_ready` en el flujo del hook por frame de
  este bucle): usar intervalo mientras tanto.
- Mismo patrón en 204 (`app_strip_scroll`).

## Deuda registrada

132/204: `update` no se ejecuta bajo el runner — revisar el bucle del App (¿hook no enlazado?).
Mientras: cadencia/fps no medibles.
