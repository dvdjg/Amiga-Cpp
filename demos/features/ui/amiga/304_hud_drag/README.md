# 304 - hud_drag

Demo del **H3 de `ROADMAP_HUD.md`**: **inventario con arrastre** (drag & drop) por la máquina de puntero del modelo (`DragSession`): umbral (por debajo = click), fantasma del objeto siguiendo al puntero, drop válido que **mueve el objeto en el inventario RPG** (puente `eng/rpg/hud_bridge.hpp`) y cancelación fuera/sobre el origen. Iconos del inventario por el **atlas** del pipeline de host (H6) incbinado con `src/prebuild.sh`.

Ver `src/INTENT.md` (qué hace y su evolución temporal) y `VALIDATION.md` (medidas, evidencia de memoria y visión).

## Compilar y ejecutar

```
bash ./tools/build/build-demo.sh demos/features/ui/amiga/304_hud_drag --debug
bash ./tools/run/run-demo.sh demos/features/ui/amiga/304_hud_drag --keep-running
```

## Verificación

```
bash demos/features/ui/amiga/304_hud_drag/analyze-sequence.sh --warp
node tools/analyze/verify-hud-drag.mjs --image <screenshot.png>
```

El gate combina **memoria** (`fps10`, `drag_ok >= 2`, `drag_bad == 0`) y **visual** (presencia de texto/amarillo/rojo + estado `D:n SRC:nn DST:nn` cambiando en la secuencia por intervalos).

## API que ilustra

- `eng::hud`: `DragSession` (`drag_begin`/`drag_move`/`drag_release`/`drag_drop`/`drag_cancel`/`drag_reset`), rejilla + `hud_hit_test_region`, `HudInput` (click/selección) y `IconAtlas` (H6).
- `eng::rpg`: `ItemCatalog`/`ItemStore`/`ItemBag` + `hud_slots_from_bag` (el arrastre mueve objetos reales del inventario).
