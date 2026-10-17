# HOST-465 - `HudFpsWindow` (fps del último segundo en el HUD)

Test host de la ventana de fps del HUD (`eng/debug/prof.hpp`): el campo de fps del último segundo que muestran las demos 079/116 (y las siguientes) sale de este helper puro:

- la ventana se cierra cuando el reloj de ciclos (`0xB7E928`, 7,093790 MHz) acumula **≥ 1 s** desde la última actualización;
- emite `fps10` = fps × 10 (**un decimal**: 18,7 → 187; los enteros no distinguen 18,1 de 18,9);
- armado diferido: sin reloj (host) queda inactivo y el primer reloj válido solo arma;
- tope de 50 Hz: una ventana imposible se recorta para que `frames × kClockHz × 10` no desborde u32;
- wraparound de 32 bits del contador: la resta u32 da el delta correcto.

Pieza freestanding: los ciclos se **inyectan** (sin hardware, sin heap, sin STL). El coste por frame en la demo es una resta y una comparación; la división se paga solo al cerrar la ventana (~1 vez/segundo).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/debug/465_hud_fps_window   # solo este
bash tools/run-host-tests.sh                                      # todos
```
