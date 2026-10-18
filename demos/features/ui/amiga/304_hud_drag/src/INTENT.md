# INTENT - 304_hud_drag

## Qué hace

Tutorial del **arrastre (drag & drop)** del HUD (H3): la franja inferior muestra un inventario 6x2 con objetos reales del kernel `eng::rpg` (puente `hud_slots_from_bag`, H5) y la máquina de puntero del modelo (`DragSession`, `HUD_SYSTEM.md` §7.2) recorre sus cuatro fases:

```text
Pressed --umbral(6 px)--> Dragging --drop en celda valida--> Dropped
   |                         |                                  |
   +--up (click)--> Idle     +--cancel (fuera/sobre el origen)--> Cancelled
```

El **fantasma** del objeto arrastrado (su icono del atlas, resaltado) sigue al puntero mientras `Dragging`; la celda origen se marca con marco rojo y la celda bajo el puntero con amarillo. Soltar en una celda válida **mueve el objeto en el inventario RPG** (la bolsa se reordena y las vistas se reconstruyen con el puente); soltar fuera o sobre el origen **cancela**.

## Qué pretende demostrar

- La **máquina de puntero completa** con umbral (por debajo = click, sin arrastre) y sus dos terminales (`Dropped`/`Cancelled`) con `drag_reset`.
- Que el **fantasma** es presentación del juego (el modelo solo expone `DragSession` con posición y objeto) y que el **drop** se resuelve contra el mismo *hit-test* por región que el click.
- El ciclo RPG completo en un arrastre: `ItemBag::remove` + reinserción + `hud_slots_from_bag` (la rejilla refleja el inventario nuevo).

## Elementos en pantalla

- **Fondo estructurado** del campo (bandas de color estáticas) y **franja** con la rejilla del inventario (iconos del atlas H6: cuadrado/cruz/círculo/rombo; barras de cantidad).
- **Marcas de arrastre**: marco rojo = celda origen de la sesión; marco amarillo = celda destino bajo el puntero; **fantasma** amarillo siguiendo al ratón.
- **Línea de estado** `D:n SRC:nn DST:nn` (fase y slots; validable sin decodificar píxeles) y franja de depuración (nombre + versión + frames + fps).

## Evolución temporal

1. **Init**: franja + inventario RPG + atlas incbinado; estado inicial pintado.
2. **Guion determinista** (tras ~2 s sin entrada real): cada ~4 s (ciclo de 200 frames) arrastra el primer objeto con contenido — inicia sesión en su celda, avanza en diagonal superando el umbral (fantasma visible), y **alterna** un drop válido en una celda opuesta (moviéndolo en el RPG) con una cancelación; después `drag_reset`.
3. **Entrada real**: el ratón conduce la misma máquina (down en celda con objeto = sesión; mover = fantasma; up = click/drop/cancel); cualquier entrada reinicia el guion.
4. **Telemetría**: `fps10` cada segundo; `drag_ok`/`drag_bad` por cada ciclo terminado.
