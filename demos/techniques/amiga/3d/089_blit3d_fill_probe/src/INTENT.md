# INTENT — 089_blit3d_fill_probe

## Qué hace

Sonda de diagnóstico del **relleno compuesto por bitplane**: dibuja una sola vez en `init` (sin update ni doble buffer, para que ninguna captura pille el blit a medias) un **rectángulo** (aristas rectas) y un **rombo** (aristas inclinadas) con el mismo camino del engine que la demo 088 (`fill_polygons_by_plane`), sobre un display de **un plano**: el plano de trabajo es la imagen visible.

## Qué pretende demostrar

- Que el camino contorno XOR ONEDOT + dedupe de aristas + area fill `FILL_XOR` descendente **rellena correctamente** polígonos convexos, incluidos los de aristas inclinadas (donde la 088 mostraba artefactos que resultaron ser de presentación/captura).
- Que el relleno por plano no tiene un bug de paridad en este caso: la sonda es estática y el resultado queda congelado en pantalla.

## Elementos en pantalla

- **Rectángulo** blanco a la izquierda (control) y **rombo** blanco a la derecha (aristas inclinadas); fondo negro.
- Texto `089 FILL PROBE` en la franja inferior.

## Evolución temporal

- Estática: el dibujo se hace en `init` y no se vuelve a tocar; solo queda la imagen para inspección/visión. **Estado: OK** (ambas figuras rellenas por completo).
