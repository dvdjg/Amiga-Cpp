# HOST-015 — Simulación del fuego de `fire-rgb`

Valida en host la **simulación de fuego** del porte 1:1 de `effects/fire-rgb`
(demo 080) y la tabla de color asociada.

## Qué cubre

- **Modelo de fuego**: la media de los 4 vecinos de abajo
  (`fire[y][x] = (E + B + D + C) >> 2`) produce un buffer "abajo caliente"
  (la fila inferior es la semilla aleatoria, el calor sube).
- **`dualtab` en C++23 `constexpr`**: la tabla de color/calor generada en
  compilación (`demos/amiga/080_fire_rgb/src/data/dualtab.hpp`) coincide **byte a
  byte** con la del original (`data/gen-dualtab.py`), 0 diferencias.

El bucle caliente en el Amiga vive en `support/fire_loop.s` (ASM); este test valida
la matemática equivalente en host, de forma determinista y rápida.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/015_fire_sim
```
