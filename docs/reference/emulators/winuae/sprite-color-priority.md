# WinUAE — color de sprite: prioridad de par, ATTACH y reuso

Lo **observado** en `../WinUAE-DBG/drawing.cpp` al depurar la demo `208_risky_woods`. Contrastado con el AHRM 3.ª.

## Prioridad: el número de canal es la prioridad

- `drawing.cpp:4226-4237`: el color se toma de la **pareja con mayor prioridad**, que es la de **número más bajo** con algún bit puesto. `OFFS` es el índice de pareja; `2 * OFFS` es a la vez el bit de la pareja en `v` y el **offset de color**.
- `drawing.cpp:4240-4266`: `col = v; … col += offs * 2;` con `col += 16` → pareja 0 → `COLOR16-19`, pareja 1 → `COLOR20-23`, etc. (4 registros por pareja).
- `drawing.cpp:4223` y `4270`: si el valor `v == 0` la función devuelve `0` → el píxel es **transparente** (se ve el playfield).

**Implicación:** para que un elemento se dibuje **delante** hay que darle el **número de canal más bajo**. Un sprite de número mayor queda **detrás** aunque su copperlist se escriba después. En la 208 los dos objetos usan canales 0/1 (delante) y el fondo de la franja B los canales 2..7 (detrás).

## ATTACH: los bits altos los aporta el canal impar

- `drawing.cpp:2722`: al escribir `POS`/`CTL`, `dspr[n & ~1].attached = (dspr[n | 1].ctl & 0x80) != 0;` → el flag de *attach* se guarda en el **canal par**, tomado del **bit 7 del `SPRxCTL` del impar**.
- Consumo: `drawing.cpp:4239`: `if (dspr[offs].attached) { col = v; … col += 16; }` → la pareja usa **4 bits** (hasta 15 colores) combinando ambos canales.
- En modo no-*attached* (`drawing.cpp:4245-4266`) el valor se arma con los 2 bits del canal con bits y se selecciona el par o impar.

**Implicación:** el canal impar aporta los bits 2,3; su `SPRxCTL bit 7` es lo que activa el modo. En no-*attached* solo hay 3 colores asignables (valor 0 = transparente).

## Reuso (reposicionar el mismo canal dentro de la línea)

- `drawing.cpp:2656-2664`: si el sprite **ya estaba armado** y se escribe una `SPRxPOS` cuya X coincide con el contador del haz (`s->xpos_lores == denise_hcounter`), se copia el shifter (`dataas = dataa; …`) y se re-arma (`spr_arms(s, 1)`). Es la vía del **multiplexado por línea** (reutilizar un canal varias veces por línea).

**Implicación (observada en la 208):** al **reusar un par *attached*** a lo ancho, las posiciones repetidas **pierden los bits altos**: solo la **armadura inicial** entrega los 15 colores y las repeticiones caen a 4 (2 bits) porque el canal impar que aporta los bits 2,3 no se re-arma. Reposicionar **también** el canal impar **empeora** el resultado (más re-armes → degradación mayor). Para repetir un patrón a 15 colores hay que **volver a armar el par entero** con un `WAIT` en cada posición, o reducir el número de repeticiones.

> *Punto pendiente de confirmar en el fuente exacto del reuso de la pareja *attached* (interacción `spr_arms`/`spr_nearest` del canal impar); contrastar con el AHRM cap. 4.*

## Validación

- `demos/techniques/amiga/sprites/208_risky_woods` E4: prioridad (objetos 0/1 delante), ATTACH (franja C) y reuso del par.

## Referencias

- AHRM 3.ª, cap. 4 (sprite DMA, ATTACH, prioridad) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md).
- Código: `../WinUAE-DBG/drawing.cpp` (`denise_render_sprites`, `sprwrite`, `sprite_offs`).
