# HOST-107: planificación GOAP

Test host de `engine/include/eng/ai/planning/goap.hpp`: el planificador **GOAP**
(Goal-Oriented Action Planning) con hechos booleanos, acciones (precondiciones,
efectos, coste) y objetivo. Modelo y referencias en
[`GAME_AI_LIBRARY.md`](../../../docs/engine/architecture/GAME_AI_LIBRARY.md).

## Qué comprueba

Sobre tres dominios clásicos de IA de juego, valida que el plan exista, que su
**coste** sea el mínimo esperado, que su **longitud** coincida, que cada acción sea
**aplicable en orden** y que el **estado final** cumpla el objetivo (reproduciendo el
plan paso a paso):

1. **Torres de Hanoi** (3 discos, 3 postes, 18 acciones generadas en `constexpr`):
   la solución óptima son `2³ − 1 = 7` movimientos.
2. **Receta de un pastel** (8 acciones con costes): compras + batir + hornear + decorar,
   coste total 20.
3. **Misión de un soldado** (13 acciones): obstáculo (alambre), utensilio (alicates),
   llave y puerta, máquina (generador y puerta eléctrica), arma y munición; coste 26.

Casos límite del contrato: objetivo **ya cumplido** (plan vacío, coste 0), objetivo
**sin solución** (`found()` falso) y `forbid` (una acción solo se aplica con un hecho
a 0; con el hecho presente el planner toma la vía alternativa).

## Salida de referencia

```
  hanoi        plan=7 acciones  coste=7  nodos=17
  pastel       plan=8 acciones  coste=20  nodos=35
  soldado      plan=13 acciones  coste=26  nodos=81
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/107_goap
```
