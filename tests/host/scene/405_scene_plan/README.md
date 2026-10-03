# HOST-405 — vocabulario de escena + estrategia (etapa 1 §7)

`eng/scene/plan.hpp`: `ScenePlan`/`LayerPlan` (`Role` + `Placement` + `ScrollPlan`) y
`choose_strategy`, que elige la **estrategia de composición** acotada (`Single`/`Dpf`/`Bands`) o
rechaza (`Unsupported`). Puro, sin motores.

## Qué comprueba

- `Empty` sin capas; `Single` con 1 capa a banda completa.
- `Dpf` con 2 capas a banda completa de roles complementarios (BG+FG); `Unsupported` con roles
  iguales u `Overlay`.
- `Bands` con ≥2 bandas apiladas; `Unsupported` con 1 banda o mezcla banda+full.
