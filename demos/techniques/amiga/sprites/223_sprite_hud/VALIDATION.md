# VALIDATION — Demo 223 (`sprite_hud`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: VERIFICADA.**

Intención: HUD (nombre + versión + fps) por sprites con armado único en la franja inferior,
coste por frame CERO (copperlist estática; fps ≤1/s).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| build | `build-demo.sh … --debug` | OK | compila con la API endurecida (armado con PT tipado) |
| run | `run-demo.sh …` | READY ✓ | arranca |
| fps | `measure-fps 223_sprite_hud A500_debug --json` | **49,87 fps · 142.244 ciclos/frame · 1,003 fields** | presupuesto cumplido (50 Hz) |
| contenido | `out/tmp/white223.mjs` (blancos de COLOR17) | **336 px en 16 filas de la franja inferior** | el HUD aparece |
| visión (Ollama) | `out/run/vision.mjs` sobre la captura | "223 HUD / v1.0.2292 / FPS nn"; nítidos; franja inferior ✓ | render correcto |
| host | `run-host-tests.sh` | 411 OK | HOST-418 cubre armado único y rearmado por línea |
| lista Copper | volcado del emulador (`COP1LC`) | armados `POS/CTL/PT` con stride 4 (`0x120+ch*4`) y HSTART absoluto | emisión correcta |

## Diagnóstico del render (hallazgos, con fuente)

El render estuvo abierto hasta localizar **tres defectos encadenados** (citas del fork
`../WinUAE-DBG/`):

1. **Stride de `SPRxPT`**: el armado emitía el bloque de punteros con stride 8
   (`0x120+ch*8`) en vez de 4 (`SPR0PTH=0x120`, `SPR1PTH=0x124`; `custom.cpp:7462-7464`):
   los canales 1..3 escribían el PT de OTRO canal y el canal 4 escribía `SPR0POS`/`SPR0CTL`
   (corrompía el canal 0). Corregido en `SpriteLineLayer` y `emit_sprite_horizontal_rearm`
   (los tests HOST-105/418 tenían el golden erróneo).
2. **X del sprite**: `hpos0` es HSTART **absoluto** (pantalla = HSTART − 0x81,
   `drawing.cpp:2706`); la demo pasaba x de pantalla (32) → el sprite quedaba en x = −97.
   Corregido (`kHpos0 = 0x81 + 32`).
3. **Banda en `vpos > 255`**: el `WAIT` simple del Copper solo alcanza la fase 0-255
   (`custom.cpp:4427`/`9255-9256`); el armado usa `wait_line_safe` → `wait_line_pal` (par de
   overflow `0xffdf/0xfffe`, `copper.hpp:225-236`), pero la **reposición** POS-only del
   rearmado por línea recibe `vline: u8` y no expresa ≥ 256. La demo pasó a **armado único**
   (`per_line_rearm=false`, canales disjuntos 3+3+2) con `VSTART = DIWSTRT 0x2C + fila`; el
   **secuenciador auto-arma** el canal al llegar a `VSTART` (`custom.cpp:10088-10090`).

Además: los registros de sprite son **write-only para la CPU** (la lectura devuelve el
latch de escritura, `custom.cpp:7119-7169`), así que no se pueden sondear por CPU — la
observación fiable es el volcado interno del emulador (`winuae_custom_registers`), los
watchpoints con `src=copper` y la captura.

## Prompts de visión y respuestas crudas

- Prompt (`out/tmp/prompt223final.txt`): transcribe el texto blanco inferior; nitidez;
  posición. Respuesta: "(1) 223 HUD / v1.0.2292 / FPS nn; (2) Nítidos; (3) Sí".
- Prompt de banda (`out/tmp/prompt223band.txt`, captura previa al fix): "No hay texto
  blanco en ninguna zona" — coherente con el sprite fuera de pantalla (x = −97).
