# HOST-472 - `hud_crpg` (HUD de aventura/CRPG, H5)

Test host de la fase **H5** del HUD (`docs/guides/roadmap/ROADMAP_HUD.md`): componentes de `eng/hud` y el puente `eng/rpg/hud_bridge.hpp`.

## Qué cubre

- `VerbBar`: alta desde etiquetas, verbo activo, foco de navegación con recorte y *dirty* al cambiar.
- `Tooltip`/`Toast`: visibilidad con TTL, avance por `tooltip_tick` y *dirty* al ocultarse.
- `Panel` + `panel_cell`: layout por columnas y rechazo controlado si no cabe.
- `eng::rpg::hud_slots_from_bag`: inventario/equipo → `HudSlotView` (id lógico, icono por tipo, cantidad, vacío/equipado).

Freestanding: sin hardware, sin heap; solo `printf` del host para el informe.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/472_hud_crpg
```
