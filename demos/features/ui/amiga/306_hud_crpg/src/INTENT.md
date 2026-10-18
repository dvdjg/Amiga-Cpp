# INTENT - 306_hud_crpg

## Qué hace

Pantalla de **CRPG / aventura gráfica** sobre la franja inferior (líneas 208..255 de la pantalla PAL), con el campo de juego estático detrás (la franja no resta al campo: solo cambia su paleta). Tres elementos del HUD H5 conviven en la franja:

- **Barra de verbos** (`LOOK TAKE USE OPEN TALK`, 5 verbos de 40 px en (112,3)): el verbo **activo** se pinta con fondo amarillo y el texto en negro; los demás, fondo negro y texto blanco. Los botones y su geometría viven en el **modelo** (`add_verb_bar`): el pintado, el *hit-test* y el anillo de foco comparten la misma caja.
- **Rejilla de inventario** (6x2 celdas de 16x16 en (8,3)): alimentada por el **puente RPG** (`hud_slots_from_bag`): `ItemBag`/`ItemStore`/`Equipment` del kernel `eng::rpg` se convierten en vistas `HudSlotView` (icono por tipo, cantidad y marca de equipado) sin que el HUD conozca el RPG. La tira del objeto usa un color por tipo; con cantidad > 1 se pinta una barra amarilla de 2 px por unidad en el borde inferior.
- **Tooltip** con TTL (`tooltip_show`/`tooltip_tick`, 150 frames): al cambiar verbo o celda se muestra `VERB:<nombre> ITEM:<n>` en (112,27); al expirar, la línea vuelve al fondo.

Además: línea de estado `V:n S:nn` en (112,17) (validable sin decodificar píxeles) y la franja de depuración obligatoria (nombre + versión + frames + fps) en y=40.

## Elementos en pantalla

## Evolución temporal

1. **Fase de reposo**: al arrancar, el verbo LOOK está activo y la celda 0 (llave, equipada) seleccionada; el tooltip muestra el estado inicial durante ~3 s.
2. **Guion (attract)**: tras ~2 s sin entrada real, el **anillo de foco** (`HudFocus`) recorre con flechas el perímetro de la rejilla (5 derecha, abajo, 5 izquierda, arriba) cada 16 frames; al llegar al borde derecho el salto geométrico pasa a los **verbos** (centro contra centro). Cada 48 frames **activa** con Fire: en una celda selecciona (`grid_select`), en un verbo cambia el verbo activo (`verb_select`). El marco rojo de foco y la inversión amarilla del verbo/celda cambian de posición: el estado `V:n S:nn` avanza.
3. **Entrada real** (ratón o joystick): mover el ratón actualiza el **hover** (marco blanco en la celda); el clic selecciona la celda o cambia el verbo (mismos efectos que el anillo); el joystick mueve el foco de la rejilla y Fire selecciona. Cualquier entrada real reinicia el guion.

## Qué pretende demostrar

- La **coexistencia de las dos vías de entrada** del HUD (HUD_SYSTEM §5.8): `HudInput` (ratón/joystick) y `HudFocus` (anillo geométrico sobre el núcleo `eng/ui/interaction.hpp`) producen los mismos efectos en el modelo; el juego no distingue el dispositivo.
- El **puente RPG -> HUD** (H5): el inventario es contenido real del kernel `eng::rpg` (tipos, pilas, equipo) y el HUD solo recibe vistas.
- El **presupuesto por deltas**: solo se repintan las celdas/verbos cuyo estado cambió y la línea del tooltip al expirar; el resto del frame no toca la franja.
