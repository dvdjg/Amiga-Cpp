# HOST-094 — color RGB444

Respalda `engine/include/eng/core/util/color.hpp`.

## Qué cubre

- Empaquetado/extracción RGB444 (`rgb444`, `rgb444_r/g/b`).
- **`lerp444`** (interpolación canal a canal con `num/den`, `muls.w`+`divs.w`; sin
  libcalls) y **`scale444`** (escala con saturación a 15).
- **`hsv_to_rgb444`** (hue en `[0,1536)`, sat/val `[0,255]`), con división por 255
  aproximada por `>>8` para no usar división libgcc.

## Uso previsto

Degradados de paleta, fundidos, parpadeos y efectos de color; reemplaza el `lerp444`
local que hoy vive en la demo 086.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/094_color
```
