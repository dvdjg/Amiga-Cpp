# INTENT - 305_hud_overlay

## Qué hace

Tutorial del **overlay de juego** (H4 de `ROADMAP_HUD.md`) sobre un campo **en movimiento**, con las dos políticas de restauración de `HUD_SYSTEM.md` §6.4 y **skins** (H6):

- **Campo**: fondo planar 640×256 pre-renderizado (c2p) que **scrollea** 2 px/frame por blit de ventana con shift fino (solo las 216 líneas superiores); un actor recorre el campo en lissajous. La franja inferior es **estática**.
- **Overlay A (GameRedraws)** en (208,16) 96×36: panel de estado (medidor + contador + texto de degradación). El **blit del fondo ES la restauración**: el juego repinta el área antes de que el HUD pinte encima, sin *backing*.
- **Overlay B (Backing)** en (16,216) 96×24: panel de "vidas". El juego **no redibuja** esa zona (estática); el HUD guarda su fondo en un buffer Chip propio (`budget.max_backing_bytes`) y lo **restaura con un blit** antes de cada repintado.
- **Skins (H6)**: cada ~200 frames cambian el **estilo del medidor** (`Linear`/`Segmented`/`Pips`/`Raster`) y el **estilo del contador** (`Numeric`/`Bar`/`Pips`); el modelo no cambia de forma ni de valor.
- **Degradación por presupuesto** (`HUD_SYSTEM.md` §6.6): el demo mide **solo la sección HUD** por frame (reloj de ciclos) y, si supera `kBudgetCycles`, degrada (contador a 1/2 de tasa y medidor sin pulso); al ir sobrado, vuelve. El estado y el número de cambios van por contadores.

## Qué pretende demostrar

- Que el HUD puede pintarse **fuera de una franja**: las **operaciones del modelo** (`HudDrawOp`) se aplican con `apply_hud_ops` sobre una `Surface` de la escena (el mismo contrato de la franja, sin franja), en coordenadas **absolutas** con el rect de la Surface como clip.
- Las **dos políticas de restauración**: `GameRedraws` (el juego repinta) y `Backing` (el HUD guarda/restaura), conviviendo en la misma pantalla.
- La **escalera de degradación** medida de verdad: el presupuesto se aplica sobre el coste HUD real y oscila (normal ↔ degradado) según el trabajo por frame.
- Los **skins**: mismo modelo, distinta materialización (medidor y contador), cambiados en caliente.

## Elementos en pantalla

- Campo scrolleando (bandas con relieve, río, sol) + actor de disco (lissajous, x en pasos de 16).
- Panel A (esquina superior derecha): medidor según estilo + contador según estilo + `NORMAL`/`DEGRADADO`.
- Panel B (franja estática inferior): barra de "vidas" (1..5 pips) que cicla.
- Franja de depuración obligatoria (AGENTS §0): nombre + versión + frames + fps sobre el campo (y=240).

## Evolución temporal

1. **Init**: fondo c2p + actor + franja estática + regiones A/B + *backing* del panel B construido + primera pintura.
2. **Frames**: el fondo scrollea y el actor se mueve; el HUD repinta por deltas (contador/medidor); el panel B restaura su *backing* y repinta; cada ~200 frames cambian los estilos; el presupuesto degrada/recupera.
3. **Telemetría**: `fps10`, `ov_ok`/`ov_bad` (ops aplicadas), `bk_ok`/`bk_bad` (restauraciones), `blit_bad` (blits del juego), `deg_changes` (cambios de estado de degradación).
