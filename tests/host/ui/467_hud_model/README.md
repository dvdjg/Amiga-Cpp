# HOST-467: modelo del HUD (`eng::hud`) — handles, capacidad, *dirty*, contadores, arrastre y regiones

Test host de la fase **H0** del HUD: `engine/include/eng/hud/model.hpp`, `region.hpp` y `theme.hpp`. Ver `HUD_SYSTEM.md` §4–§5 y `ROADMAP_HUD.md` (H0).

## Qué comprueba

1. **Capacidad y desbordamiento**: regiones (`MaxRegions`), widgets (`MaxWidgets`), slots enlazados (`MaxSlots`), operaciones (`MaxDrawOps`) y *hit-test* (`MaxHitRects`) devuelven handle inválido o `false` al pasarse, sin tocar memoria fuera de rango.
2. ***Dirty* solo al cambiar**: `set_meter`/`set_counter` con el mismo valor devuelven `false` y no suben `dirty_revision`; un cambio visible sube la revisión una vez; `clear_dirty` no altera valores.
3. **Contadores sin división**: base 10, relleno de ceros, base 16 en mayúsculas, negativos, `s32` mínimo y base 2, con el texto formateado en el buffer del contador.
4. **Rejilla**: selección dentro de rango y foco por filas/columnas con recorte en los bordes (sin división).
5. **`DragSession`**: transiciones `Idle → Pressed → Dragging → Dropped/Cancelled`, umbral `kHudDragStartPx`, *click* puro, rechazo de transiciones inválidas y `reset`.
6. **Regiones**: banda válida, corte fuera del display, planos imposibles, `Overlay` con `Backing` sin bytes declarados y `add_region` que rechaza especificaciones inválidas.

## Salida de referencia

```
HOST-467 hud_model: OK
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/467_hud_model
```
