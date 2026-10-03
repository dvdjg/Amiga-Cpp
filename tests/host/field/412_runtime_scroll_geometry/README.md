# HOST-412 — geometría del anillo en runtime (paso 4 §7)

`eng/field/runtime_scroll_geometry.hpp` (`eng::playfield::runtime_scroll_geometry`): calcula la
geometría del anillo de tiras con parámetros de **runtime** (viewport/tiles/planos/anillo/mapa) —el
mismo conjunto de campos que `StripScrollGeometry<…>` (NTTP)— con las mismas **invariantes** como
*checks*. Es el primer paso hacia que el planner **elija la geometría en runtime** (p. ej. un editor
que carga un formato no conocido a priori).

## Qué comprueba

- **Equivalencia**: para varias geometrías (202/204/205/32 px/split) todos los campos de la versión
  runtime coinciden **exactamente** con los del NTTP.
- **Invariantes**: tile ≠ 16/32 → `BadTile`; viewport no múltiplo → `BadViewport`; guarda < 2 →
  `BadGuard`.

## Alcance / pendiente

Este es el **cálculo** de la geometría en runtime. Para que el `App` **construya el motor** con ella
(el motor NTTP sigue siendo el rápido), falta que `StripScrollComposer`/`StripScrollController`
consuman la geometría como **parámetro** (hoy usan `Geom::campo` compile-time) — el siguiente paso
del §7.
