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

- `drawing.cpp:4940-4968` (`matchsprites2`): por cada sprite **armado**, cuando la X coincide con el contador del haz (`cnt == (sp->xpos & ~3)`) se copia el *shifter* (`dataas = dataa; databs = datab;`, línea 4950) y se arma (`spr_arms`, línea 4960). Es la vía del **multiplexado por línea** (reutilizar un canal varias veces por línea): reescribir `SPRxPOS` cambia `xpos` y, cuando el haz llega a la nueva X, el sprite se re-arma.
- `drawing.cpp:4373-4384` (`denise_render_sprites`): el color de cada píxel se compone de los `pix` (2 bits) de **todos** los canales; por eso un par *attached* necesita **los dos** *shifters* corriendo (`drawing.cpp:4239-4244` combina los 4 bits).

**Implicación (observada en la 208 y confirmada en el fuente):** al **reusar un par *attached*** a lo ancho reescribiendo solo la `SPRxPOS` del **canal par**, el **impar no se re-arma** (su `xpos` no cambió → su match no dispara en la nueva X) y sus bits 2,3 no llegan al píxel: la **armadura inicial** (donde el Copper sí escribió ambos `SPRxPOS`) entrega los 15 colores, pero las repeticiones caen a **4** (2 bits del par). Para conservar los 15 colores hay que **reescribir también la `SPRxPOS` del impar** en cada posición (para que su *shifter* re-arme) **y dar un head-start suficiente** al `WAIT` (con head-start corto el `POS` del impar no llega a tiempo → vuelven a perderse los bits; ver `kCuGap` en la 208 y `docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md` §2.9–2.10).

**Confirmado en hardware OCS real (Grok):** es **posible** multiplexar un par *attached* a varias X por línea conservando los 15 colores. Secuencia por posición: un `WAIT` (con antelación ≥ 16 px lo-res) y luego `SPR0POS` **y** `SPR1POS` con la **misma** X, **sin** tocar `SPRxCTL` (el `WAIT` cerca del objetivo deja a `SPR1POS` sin escribir a tiempo → solo el canal par, 4 colores). La pérdida de los bits altos es **comportamiento físico de Denise** (comparadores horizontales independientes por canal; el shifter del impar emite `00` y el multiplexor de color del par cae a COLOR16-19), no un artefacto de WinUAE. Detalle de la consulta: [`consulta-ocs-attached-sprite-multiplexing-en.md`](../../debugging/investigaciones/consulta-ocs-attached-sprite-multiplexing-en.md).

## Validación

- `demos/techniques/amiga/sprites/208_risky_woods` E4: prioridad (objetos 0/1 delante), ATTACH (franja C) y reuso del par.

## Referencias

- AHRM 3.ª, cap. 4 (sprite DMA, ATTACH, prioridad) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md).
- Código: `../WinUAE-DBG/drawing.cpp` (`denise_render_sprites`, `sprwrite`, `sprite_offs`).
