# `interaction.hpp` — núcleo de interacción

`engine/include/eng/ui/interaction.hpp` es un **header hoja** (solo tipos y `constexpr`; sin árbol de widgets, sin *backing*, sin compositor) con las primitivas de interacción **comunes a toda interfaz**: las usan los widgets de aplicación (`eng::ui`) y el HUD de juego (`eng::hud`, que lo incluye — el HUD se construye **sobre** la GUI, nunca al contrario). Quien lo incluye paga solo lo que usa.

```
modelo/geometria (widgets o modelo del HUD)
     │  listas de entradas (rects con capa; rejillas con pitch)
     ▼
  hit_test            punto -> entrada (rect por capa; si no, celda por formula)
     │
     ├── UiContext          (raton: click, hover, foco de arbol)
     └── FocusRing          (anillo por geometria + Tab + activacion)
             ▲
             ├── eng::ui     (cuando la app navega por geometria)
             └── eng::hud    (HudFocus: adaptador al modelo del HUD)
```

## Formas de *hit* (por concepto)

Cada capa define **sus** entradas; el núcleo solo lee los campos que necesita:

- `HitRectLike`: `box` (`eng::Box`), `id` lógico y `layer` (mayor = delante). El HUD añade nada más; la GUI pasa sus `Widget` adaptados.
- `HitGridLike`: `area`, `cols`, `rows`, `pitch_x`, `pitch_y` y `first_id`. El HUD usa `HudGridHit` (con su `GridHandle` adicional, que el núcleo ignora).

## `hit_test`

Resuelve un punto contra las listas: **rectángulos por capa** (mayor delante; empate = el último empujado, el orden de pintado) y, si ninguno toca, **rejillas por fórmula** (`(p − origen) / pitch`, división entera: el *pitch* no tiene que ser múltiplo de 16) en orden de empuje. Devuelve `HitResult` con el **índice de la entrada** (el llamador lo mapea a su `GridHandle`/widget), el `slot` de la celda y el `id` lógico (`first_id + slot`). La multiplicación ocurre **en el evento**, nunca por frame ni por píxel.

La **misma** función sirve al ratón y al foco: no hay dos mapas que puedan desincronizarse.

## `FocusRing`

Anillo de foco **genérico**, navegable por **geometría** sobre las mismas listas:

- `focus_first` / `focus_rect` / `focus_grid_cell`: entrada inicial y sincronización con el *hover*.
- `move(dx, dy)`: dentro de una rejilla mueve la celda (si cabe); en el borde —o entre entradas— salta al **más cercano en esa dirección** (centro contra centro; el eje del movimiento manda).
- `next(forward)`: recorre el anillo en orden de empuje (circular); lo consume `Tab`/`Shift+Tab`.
- La **activación** la resuelve el llamador con `target()`: el HUD selecciona la celda (`grid_select`) o devuelve el `id` al juego; la GUI entrega el evento al widget con foco.

El anillo no impone layout ni árbol: participa cualquier elemento con geometría declarada.

## Consumidores

- **HUD** (`eng::hud`): `hud_hit_test` delega en `hit_test` y mapea índice → `GridHandle`; `HudFocus` adapta `FocusRing` al modelo (sincroniza `grid_move_focus_to`/`grid_select`). Ver `HUD_SYSTEM.md` §7.
- **GUI** (`eng::ui`): el *hover* lo mantiene `UiContext` (`WfHover` con `MouseMove`; `PointerCancel` lo limpia); el foco de la GUI es de árbol (`Tab` por `WfAcceptsFocus`), y el anillo geométrico queda disponible para apps que naveguen por posición.

Verificación: **HOST-225** (hover del contexto), **HOST-469** (hit-test y anillo desde el HUD).
