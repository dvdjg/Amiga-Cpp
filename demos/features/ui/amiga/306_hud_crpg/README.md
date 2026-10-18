# 306 - hud_crpg

Demo del **H5 de `ROADMAP_HUD.md`**: pantalla de **CRPG/aventura gráfica** en la franja inferior, con **barra de verbos** (LOOK/TAKE/USE/OPEN/TALK), **inventario RPG** alimentado por el puente `eng::rpg::hud_slots_from_bag` (`ItemBag`/`ItemStore`/`Equipment` -> vistas `HudSlotView`) y **tooltip con TTL**; entrada por ratón/joystick (`HudInput`) **y** por el anillo geométrico `HudFocus` (el guion determinista mueve foco y activa), con los mismos efectos en el modelo.

Ver `src/INTENT.md` (qué hace y su evolución temporal) y `VALIDATION.md` (medidas, evidencia de memoria y visión).

## Compilar y ejecutar

```
bash ./tools/build/build-demo.sh demos/features/ui/amiga/306_hud_crpg --debug
bash ./tools/run/run-demo.sh demos/features/ui/amiga/306_hud_crpg --keep-running
```

## Verificación

```
bash demos/features/ui/amiga/306_hud_crpg/analyze-sequence.sh --warp
node tools/analyze/verify-hud-crpg.mjs --image <screenshot.png>
```

El verificador comprueba, sobre la captura del runner (ventana a 2x), que la franja tiene texto (etiquetas de verbos + línea de depuración), amarillo (verbo activo + barras de cantidad), rojo (marco del foco) y que, en secuencia, la **línea de estado `V:n S:nn`** y el contenido de la franja **cambian** entre frames (el guion avanza el foco/verbo/selección). El gate `analyze-sequence.sh` añade la **evidencia de memoria** (`fps10` y `aframe` por contadores) porque la captura por paso de este entorno puede congelar el vídeo (DT-023/035).

## API que ilustra

- `eng::hud`: `add_verb_bar`/`verb_select`/`verb_move_focus` (H5), `add_tooltip`/`tooltip_show`/`tooltip_tick` (H5), `add_grid`/`bind_grid`/`push_grid_hit`/`push_hit` (H2), `HudInput` (ratón/joystick) y `HudFocus` (anillo geométrico).
- `eng::rpg`: `ItemCatalog`/`ItemStore`/`ItemBag`/`Equipment` y el puente `eng::rpg::hud_slots_from_bag` (`hud_bridge.hpp`).
- `eng::ui::FocusRing` (núcleo de interacción compartido): el anillo del HUD es un adaptador de este núcleo (`eng/hud/focus.hpp`).
