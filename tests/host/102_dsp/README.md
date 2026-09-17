# HOST-102 — primitivas de audio

Respalda `engine/include/eng/core/util/dsp.hpp`.

## Qué cubre

- **`Adsr`**: envolvente lineal con tasas por muestra (attack/decay/release) y nivel de
  sostenido; `note_on`/`note_off`/`tick`/`active` (sin división).
- **`OnePole`**: filtro de un polo `y += alpha·(x−y)`.
- **`DelayLine<S,N>`**: línea de retardo sobre `RingBuffer` (`push`/`read`/`process`).
- **`soft_clip`** y osciladores `osc_saw`/`osc_square`/`osc_triangle`/`osc_sine`.
- Pruebas con `double` y `MiniFloat16`.

## Uso previsto

Sintetizar/modular SFX (envolventes, filtros, eco, osciladores) sin `float` obligatorio
ni heap; las tasas se fijan a la frecuencia del mixer.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/102_dsp
```
