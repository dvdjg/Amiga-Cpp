# AHRM — erratas y notas aprendidas del emulador

Aclaraciones y correcciones de la copia local del AHRM, obtenidas **leyendo el fuente del
emulador** (mecanismo descrito en `AGENTS.md` §1.11). Cada nota cita **de dónde se obtuvo**
(emulador + `fichero:línea`). El texto del manual no se modifica: esto lo **completa**.

## 1. `SPRxCTL`: el bit `ATTACH` es el **bit 7**

El AHRM es **correcto** (cap. 4: *«the ATTACH bit, bit 7 in sprite control word 2»*), pero el
`amiga-bootcamp/08_graphics/sprites.md` y `01_hardware/ocs_a500/sprites.md` lo listan en el
**bit 0** — **erróneo**. Layout correcto de `SPRxCTL`:

```
  bits 15-8 : VSTOP[7:0]
  bit  7    : ATTACH
  bits 6-3  : sin usar (0)
  bit  2    : VSTART[8]
  bit  1    : VSTOP[8]
  bit  0    : HSTART[0]
```

**Origen**: AHRM 3.ª (texto) + contraste con el engine. Bug corregido en
`eng/graphics/sprite_manager.hpp`, `copper/scheduler.hpp` y `SpriteLayer`.

## 2. `CLXCON`/`CLXDAT`: el «siempre detectado» es **por grupo par/impar**

El AHRM dice *«If all bitplanes are excluded (disabled), then a bitplane collision will always
be detected»*. Es **incompleto**: la comparación se hace **por grupo** (planos **pares** con
`enable & 0xAA` y **impares** con `enable & 0x55`), no globalmente.

Con solo planos de un grupo habilitados (p. ej. `ENBP1`, impar), la comparación del grupo vacío
`(apixel & 0) == 0` es **siempre cierta** → el bit de **planos pares vs sprite** (`CLXDAT`
bits 5-8) se enciende **siempre**, aunque no haya solape.

→ Para un plano **impar** (`BPL1`) mirar **`CLXDAT` bit 1**; para uno **par** (`BPL2`), **bit 5**.

**Origen**: WinUAE-DBG `drawing.cpp:4196-4211` (`denise_render_sprites2`) y `3255-3263`
(`expand_colmask`). Ficha completa: `docs/reference/emulators/winuae/collision.md`.

## 3. `collision_level`: preferencia de WinUAE que desactiva la colisión

`currprefs.collision_level`: `0` = sin colisión (`CLXDAT` siempre 0); `≥1` sprite↔sprite; `>1`
sprite↔bitplane; `≥3` bitplane↔bitplane.

**Origen**: WinUAE-DBG `drawing.cpp:4192`; preferencia en `cfgfile.cpp`.

## 4. Sprite DMA: la estructura en Chip RAM lleva **cabecera POS+CTL**

En modo DMA (`SPREN` + `SPRxPT`), Agnus recarga `POS`/`CTL` **desde la estructura en memoria**,
que es `[POS, CTL, DAT0, DATB0, …, 0, 0]`; `SPRxPT` apunta a su **inicio** (la cabecera), no al
primer `DAT`. Un buffer con solo `DAT/DATB`+terminador se interpreta como cabecera (basura).

**Origen**: contraste con el ejemplo `spr_layer/Sprite_Layer/` (Jeroen Knoester) y validación en
la demo `206_sprite_collision`/`207_sprite_layer`.

## 5. Sprite DMA: el puntero avanza siempre (columna fantasma si el Copper alimenta el canal)

Aunque el Copper reescriba `SPRxPOS`/`SPRxDATA`/`SPRxCTL` por línea, el DMA de sprites **sigue
avanzando** el `SPRxPT` del canal mientras esté activo, y cuando `dmastate==0` interpreta lo
leído como **cabecera** `POS`/`CTL`. Un canal alimentado por Copper con `SPRxPT` a una
estructura corta («nula») acaba leyendo la DATA de la estructura siguiente como cabecera → el
sprite recibe un `VSTART`/`VSTOP` basura y queda **armado hasta el fin del frame** (columna
fantasma). Corregir con **una estructura DMA válida por canal** y colocando el `WAIT` de
rearmado **después del fetch DMA** (`DDFSTRT`) y antes de la primera columna.

**Origen**: WinUAE-DBG `custom.cpp:10055-10120` (`generate_sprites`), `custom.cpp:12012-12023`
(fetch e incremento de `s->pt`), `custom.cpp:4018-4083` (`sprstartstop`/`SPRxCTL`). Detalle:
`docs/reference/emulators/winuae/sprite-dma.md`; validado en `demos/amiga/207_sprite_layer` (sin
fantasma y con la DATA del Copper visible).

## 6. Disquete: el control va por CIA-B **PRB** (`$BFD100`), no PRA

El AHRM es **correcto** (Table 8-5: `CIABPRB $BFD100` = «eight output bits for disk selection,
control and stepping»), pero `amiga-bootcamp/01_hardware/common/floppy_hardware.md` lo atribuye a
CIA-B **PRA** (`$BFD000`) y `.../common/cia_chips.md` dice que PRA son las salidas de disco. Es
**erróneo**. Layout de `CIABPRB` (activos a 0 salvo `SIDE`/`DIR`):

```
  bit 7  /MTR     motor (0 = on)
  bits 6-3 /SEL3../SEL0   selección (/SEL0 = 0 -> DF0)
  bit 2  SIDE     cara (0 = superior)
  bit 1  DIR      dirección (0 = hacia el centro; pista 0 está fuera)
  bit 0  /STEP    pulso (alto -> bajo -> alto)
```

Estado en **CIA-A PRA** (`$BFE001`): `/RDY`(5), `/TK0`(4), `/WPRO`(3), `/CHNG`(2).

**Origen**: AHRM Table 8-5 + contraste con WinUAE-DBG (`cia.cpp:2431` decodifica el registro con
`(addr & 0xf00) >> 8`, ignorando A0; `identify.cpp:83,98` dan `CIAB PRA` en `$BFD000` y `CIAA PRA`
en `$BFE001`). Implementado en `eng/os/floppy.hpp` + `amiga_floppy.cpp`; la DMA cruda se
valida en la demo `214_floppy_raw`.

Matiz sobre `SIDE` (bit 2): WinUAE lo **invierte** al traducir la cara física a índice de cara
(`side = 1 - ((data >> 2) & 1)`, `disk.cpp:3482-3491`). Así, la **primera cara del ADF** se lee con
`SIDE = 1`, no con `SIDE = 0`; el backend del engine escribe el bit invertido para que el parámetro
`side` sea el índice de cara del ADF (0 = primera cara).
