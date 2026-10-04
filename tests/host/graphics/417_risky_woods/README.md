# HOST-417 — `eng::effects::RiskyWoodsLayer`

Test host del **driver de fondo por sprites** de tipo *Risky Woods*
(`engine/include/eng/api/effects.hpp`): reposición de `SPRxPOS` con carrera contra el haz.

Cubre:

- `attach`: validación de geometría y de la estructura DMA.
- Por línea: **un `WAIT` por período** del patrón y los `channels` MOVEs de `SPRxPOS`.
- Los canales ciclan `channel_first..channel_first+channels-1` y las X crecen
  `column_width` px (16 px); la reutilización del mismo canal cae a `period` (96 px).
- La carrera **no reescribe `SPRxCTL`** (lo desarmaría).
- El **scroll** desplaza las X y hace entrar/salir períodos.
- `words_estimate` coincide con la huella real emitida (incluye el terminador).

Es freestanding (sin hardware): se compila con `g++` del host y corre como binario
nativo. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/417_risky_woods   # solo este
bash tools/run-host-tests.sh                                     # todos
```
