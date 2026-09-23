# HOST-070: `copper::Plan`

Test host de `engine/include/eng/graphics/copper/plan.hpp`, el orquestador de copper a nivel
de escena: recolecta las intenciones de las capas, las **ordena por scanline** y las
materializa en la copperlist del buffer trasero de un `DoubleBuffer`.

## Qué comprueba

1. `begin`/`begin_frame` reservan el doble buffer y sitúan el emisor **detrás**.
2. `materialize` **ordena por scanline**: las intenciones se añaden desordenadas (100, 40, 70)
   y la lista sale con los WAITs en 40 → 70 → 100 (antes, `emit_copper_intents` exigía ese
   orden al llamador).
3. `end_frame` escribe **siempre en el bloque trasero** y voltea: la lista del frame anterior
   (que pasa a inactiva) conserva sus valores — invariante anti-tearing.
4. `takeover`/`commit` publican el bloque activo (swap de `COP1LC`).
5. El **overflow** de intenciones no publica una lista parcial (`end_frame` devuelve false).

Todo con un backend de pega y un `MemorySystem` sobre un buffer estático: sin hardware.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/graphics/070_copper_plan
```
