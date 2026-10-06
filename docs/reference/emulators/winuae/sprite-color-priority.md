# WinUAE — color de sprite: prioridad de par, ATTACH y reuso

Lo **observado** en `../WinUAE-DBG/drawing.cpp` al depurar la demo `208_risky_woods`. Contrastado con el AHRM 3.ª.

## Prioridad: el número de canal es la prioridad

- `drawing.cpp:4226-4237`: el color se toma de la **pareja con mayor prioridad**, que es la de **número más bajo** con algún bit puesto. `OFFS` es el índice de pareja; `2 * OFFS` es a la vez el bit de la pareja en `v` y el **offset de color**.
- `drawing.cpp:4240-4266`: `col = v; … col += offs * 2;` con `col += 16` → pareja 0 → `COLOR16-19`, pareja 1 → `COLOR20-23`, etc. (4 registros por pareja).
- `drawing.cpp:4223` y `4270`: si el valor `v == 0` la función devuelve `0` → el píxel es **transparente** (se ve el playfield).

**Implicación:** para que un elemento se dibuje **delante** hay que darle el **número de canal más bajo**. Un sprite de número mayor queda **detrás** aunque su copperlist se escriba después. En la 208 los dos objetos usan canales 0/1 (delante) y el fondo de la franja B los canales 2..7 (detrás).

## Playfield vs sprites (`BPLCON2`): el valor `0x0000` oculta los sprites

- `drawing.cpp:3086-3089` (`expand_bplcon2`) construye `plf_sprite_mask` a partir de `PF1P`/`PF2P` (`bplcon2 & 7` y `(bplcon2 >> 3) & 7`): cada playfield ocupa **una posición de la cadena de prioridad** entre los grupos de sprites (`SP01 SP23 SP45 SP67`). El valor `000` coloca al playfield **delante de todos** los sprites.
- `drawing.cpp:4220` (`plfmask = (plf_sprite_mask >> maskshift) >> maskshift; v &= ~plfmask;`): el píxel de sprite solo gana donde su grupo tiene más prioridad que el playfield.
- AHRM 3.ª cap. 7, Table 7-2: `PF1P=000` → `PF1 SP01 SP23 SP45 SP67`; el ejemplo canónico del propio manual es `MOVE.W #$0024,BPLCON2` (*«Sprites have priority over playfields»*, AHRM `:3292`) — `$24` = `PF1P=PF2P=100`, los playfields detrás de los cuatro grupos. **En single-playfield manda `PF2P` (bits 5-3)**, no `PF1P` (nota de Table 7-2; confirmado con Grok en `docs/debugging/investigaciones/consulta-bplcon2-single-playfield-priority-en.md`). `BPLCON2` no tiene reset documentado: hay que escribirlo siempre.

**Implicación (defecto corregido en el engine):** `copper::Scheduler::emit_planes_display` escribía `BPLCON2=0x0000`, lo que ponía **el playfield delante de todos los sprites**; los sprites solo se veían sobre los píxeles de color 0 del bitmap (borde/zonas transparentes). Corregido a `0x0024` con comentario y cita; regresión de 207/208/054/101 en verde. En demos que quieran sprites **detrás** del playfield (p. ej. 207) el driver escribe su propio `BPLCON2` después.

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

**Presupuesto de ancho de banda del Copper (regla de diseño):** cada `MOVE`/`WAIT` del Copper cuesta **2 ciclos de bus = 8 px lo-res**. Reposicionar un par *attached* = **2 MOVE** (16 px); 4 pares = **8 MOVE** (64 px). Si además se pone **un `WAIT` por periodo** (8 px), el coste por periodo es **72 px**: **si el periodo del patrón es 64 px, el Copper se retrasa +8 px cada periodo** y a partir del ~4.º el `MOVE` del impar llega tarde → el par cae a 4 colores (banda C de la 208). **Solución:** **quitar los `WAIT` intermedios** en los tramos *attached* (un solo `WAIT` al inicio de la línea + la ráfaga de `SPRxPOS`); así `8 MOVE = 64 px = periodo` y el Copper corre pareado con el haz → 15 colores a todo el ancho. Los tramos **no-*attached*** sí necesitan el `WAIT` por periodo (sin él, el `POS` de un canal pisa al anterior).

**Guarda entre bandas (evitar la franja sólida de la 1.ª columna):** al cambiar de banda, si el Copper reescribe el `SPRxPT`/cabecera en la misma línea en la que los sprites de la banda anterior alcanzan su `VSTOP`, el DMA lee parte de la estructura anterior y parte de la nueva → una **columna vertical sólida** con la paleta de la banda previa. Se evita dejando **una línea de guarda** (`top+1` respecto a la banda anterior): Agnus recarga el `SPRxPT` en una línea neutra. Ver `docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md` §2.9.

## Validación

- `demos/techniques/amiga/sprites/208_risky_woods` E4: prioridad (objetos 0/1 delante), ATTACH (franja C) y reuso del par.
- `demos/techniques/amiga/sprites/214_attached_object`: el par *attached* (canales 0/1) y seis chispas (2..7) **delante del playfield** con el `BPLCON2=0x0024` del scheduler; captura única y secuencia validadas por visión + MD5.

## Referencias

- AHRM 3.ª, cap. 4 (sprite DMA, ATTACH, prioridad) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md).
- Código: `../WinUAE-DBG/drawing.cpp` (`denise_render_sprites`, `sprwrite`, `sprite_offs`).
