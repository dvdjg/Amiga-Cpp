# Demo 056: input aggregator — entrada unificada (joystick + fuego + teclado)

Demuestra el paso 6 de `ENGINE_DESIGN.md` §5: el backend rellena un
`eng::input::InputAggregator` cada frame y la lógica de juego lo consume sin
tocar hardware. Las direcciones se leen de `JOY0DAT`/`JOY1DAT` (contador de
Denise, código de Gray por eje) y el fuego de `CIAAPRA` (`input_poll.hpp`); el
teclado es el sintético `g_automation_keycode` (o el gancho `g_tech_new` que
`--automation-key` inyecta por memoria).

Qué muestra: una cruz que se mueve con el joystick (puerto 0) sobre un degradado
arcoíris por Copper. Mantener FIRE la pone roja; una tecla 1..9 selecciona su
color de forma determinista.

## Verificación

- Test host `tests/host/006_input_decode`: valida la decodificación pura de
  `JOYxDAT` en direcciones (mismo mapeo que ACE/Sevgi/AHRM).
- La demo en reposo (sin joystick) dibuja la cruz blanca y centrada, lo que
  confirma la lectura del estado "sin entrada".

```bash
CXX="<g++ nativo>" tools/run-host-tests.sh tests/host/006_input_decode
tools/build/build-demo.sh demos/amiga/056_input_aggregator --clean
tools/run/run-demo.sh       demos/amiga/056_input_aggregator
```

Nota: la inyección de una dirección concreta de joystick en WinUAE requiere
configurar el input del emulador; el runner solo inyecta `g_tech_new` (color).
