# HOST-407 — layout de bandas (etapa 3 §7)

`eng/scene/band_plan.hpp` (`eng::scene::plan_bands`): valida las capas en banda de un `ScenePlan`
(orden de arriba abajo, sin solapes, dentro del display) y produce los tramos `{top, height, rol}`.
Caso típico: **split-screen** (dos jugadores). Puro.

## Qué comprueba

- Dos bandas que cubren el display → 2 tramos correctos.
- Solape → `BadOrder`; fuera del display → `OutOfRange`; capa a banda completa → `NotBands`;
  capacidad de salida insuficiente → `OutOfRange`.
