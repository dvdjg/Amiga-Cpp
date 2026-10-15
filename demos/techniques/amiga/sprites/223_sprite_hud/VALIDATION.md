# VALIDATION — Demo 223 (`sprite_hud`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: ABIERTA — no renderiza aún (visual pendiente)**.

Intención: HUD (nombre + versión + fps) por sprites rearmados por línea con coste por frame
CERO (copperlist estática; fps ≤1/s).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| build | `build-demo.sh … --debug` | OK | compila con la API endurecida (armado con PT tipado) |
| run | `run-demo.sh …` | READY ✓ | arranca |
| fps | `measure-fps 223_sprite_hud A500_debug --json` | **49,27 fps · 1,01 fields** | presupuesto cumplido |
| contenido (captura completa) | `out/tmp/white223.mjs` (blancos de COLOR17) | **0 px** | los segmentos NO aparecen |
| lista de Copper (volcado del emulador) | `COP1LC` dump | armados `POS/CTL/PT` correctos (PT → tabla HUD) + reposiciones + DMACON ✓ | la emisión es la del patrón validado |

## Diagnóstico abierto

- La emisión coincide con el patrón de la 053 (`emit_config`: POS+CTL+**PT**→DATA) y el
  volcado de la lista es correcto; aun así no se pinta nada. Probadas las dos convenciones
  del par `{DAT, DATB}` (color 1 por DAT y por DATB): 0 px de color de sprite en ambas.
- Siguiente paso: traza de eventos DMA del emulador (`WinUAE-DBG`, `debug_dma`/
  `record_dma_event`) para ver el estado del canal (dmastate/dmacycle/`s->pt`) en las
  líneas 232-255; y revisar el orden armado↔fetch de la MISMA línea
  (`custom.cpp:10055-10120`).

## Deuda registrada

223: render del HUD por sprites — diagnóstico con la traza DMA. La técnica viva del
rearmado sigue en 053/207/217.
