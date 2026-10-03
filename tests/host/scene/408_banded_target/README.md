# HOST-408 — dibujo split-aware (etapa 4 §7)

`eng/scene/banded_target.hpp` (`eng::scene::for_each_band_part`): reparte un rectángulo de pantalla
entre las bandas y llama al callback con cada trozo recortado (`clip_to_band`). Primitivo con el que
un `fill`/BOB/CPU se dirigen a la banda correcta. Puro.

## Qué comprueba

- Rect dentro de una banda → 1 llamada con el rect íntegro.
- Rect que cruza la línea de split → 2 llamadas (trozo superior 100..127, inferior 128..159).
- Rect fuera de todas las bandas → 0 llamadas.
