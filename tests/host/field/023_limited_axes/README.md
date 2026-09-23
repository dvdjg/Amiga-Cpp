# Test HOST-023: variantes de eje de XLimited/XYLimited

Fija la semántica de la **familia de scroll** sobre el `ScrollEngine` real
(`engine/include/eng/field/scroll_engine.hpp`):

- Eje X `Ring` (XLimited): anillo con banda entrante; escribe con `add_draw`.
- Eje X `Finite`: rango acotado `[0, mundo-viewport]`, sin anillo ni guardas; el
  puntero se mueve directamente y **no** escribe (el contenido ya está).
- `OneDirection`: no restaura `saveword` al invertir.
- Y corkscrew con X `Finite`: la fila entrante ocupa todo el ancho del bitmap y
  cae dentro del anillo (`< display_planelines`).

Escenario: shooter vertical de 400×10000 px, viewport 320×256, tile 16, X `Finite`
(recorrido 0..80) y Y anillo.

```bash
bash tools/run-host-tests.sh tests/host/023_limited_axes
```

Contexto: `docs/guides/roadmap/ROADMAP_UNIFICADO.md` y las demos
`110_ylimited_shooter` / `111_xlimited_sidescroller`.
