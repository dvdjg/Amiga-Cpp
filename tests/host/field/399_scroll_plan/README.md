# HOST-399 — `ScrollPlan` común (§7(e))

`eng/field/scroll_plan.hpp` (`eng::playfield::ScrollPlan`) es el **vocabulario declarativo común** de
una capa de scroll: geometría (viewport/tiles/planos), política (período del mapa toroidal + velocidad)
y contenido (asset de tiles). El camino de tiras lo consume con `StripScrollLayer::set_plan`; el
corcóscru, con `apply_scroll_plan(cfg, plan)` (siembra geometría + paleta).

## Qué comprueba

- `apply_scroll_plan` **siembra** viewport, tiles/planos, `display_height`, paleta y **parallax** (RoboCod) desde el plan.
- **No pisa** lo que el plan no declara: `display_height == 0` no sobrescribe el del motor; una
  paleta vacía no sobrescribe la del juego.
