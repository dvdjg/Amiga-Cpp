# Demo 063: benchmark del fuego — C++ vs asm

Compara el rendimiento de dos implementaciones del MISMO algoritmo de fuego
(promedio de 4 vecinos de abajo, buffer `u16[80×64]`):

- `fire_cpp()`: bucle C++ (el compilador genera cálculo de offsets `y*W` y acceso
  indexado con `__mulsi3`).
- `fire_asm()`: rutina en `support/fire_asm.s` (punteros en registros y
  post-incremento `(a5)+`, sin recalcular offsets), port del `MainLoop` de
  `fire-rgb` de demoscene-repo-orig.

Mide los **ciclos de CPU** de 32 iteraciones de cada versión con el periférico de
depuración (`DebugPeripheral::cycle_counter`, base `0xB70000`) y publica el
resultado en contadores:

```
slot 0 = ciclos de fire_cpp (32 iteraciones)
slot 1 = ciclos de fire_asm (32 iteraciones)
```

## Cómo leer el resultado

Con la demo corriendo (WinUAE-DBG), consultar:

```
debugperiph counters
```

O por canal lateral (`winuae_debugperiph counters`). La ratio `slot0/slot1` es la
aceleración del asm frente al C++.

## Nota de estado (harness)

La demo llega a `Ready` en `g_eng_run_status`, pero el runner (`run-demo.js`)
resuelve la dirección del run-status por el índice fijo `sections[2]`, que con el
layout de esta demo (con `fire_asm.s` enlazado) cae en `.bss` en vez de en el
símbolo real (que queda en `sections[3]`). Por eso el runner no detecta el READY
por timeout, aunque el programa sí publica `state=3`. Es un problema de resolución
del harness (debería buscar por magic, no por índice), no del asm. El benchmark se
lee por contadores, no por run-status.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/063_fire_cpp_vs_asm --clean
tools/run/run-demo.sh       demos/amiga/063_fire_cpp_vs_asm --warp
```
