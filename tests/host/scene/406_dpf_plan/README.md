# HOST-406 — estrategia `Dpf` (etapa 2 §7)

`eng/scene/dpf_plan.hpp` (`eng::scene::apply_dpf_plan`): desde un `ScenePlan` con estrategia `Dpf`
(BG + FG a banda completa) siembra la **geometría/paleta** comunes y los **roles** (FG delante en
PF2) de una `XlimitedSceneConfigT`. El contenido por field (mapa/banco) queda al juego (los formatos
de banco difieren entre motores — pendiente).

## Qué comprueba

- Plan Dpf → config sembrada (viewport/tiles/planos/display_height/paleta) + `dpf.enabled` +
  `foreground_is_pf2`.
- Plan que no es Dpf → `false` y **no** toca la config.
