# WinUAE — sprite DMA (estructura con cabecera)

Cómo trata WinUAE la estructura de sprite en modo DMA, con lo observado en su fuente
(`../WinUAE-DBG/`).

## Estructura en Chip RAM

En modo DMA de sprite (`SPREN` + `SPRxPT`), la estructura es:

```
word 0 : SPRxPOS   (mismo formato que el registro)
word 1 : SPRxCTL
word 2 : DAT línea 0
word 3 : DATB línea 0
…
word   : 0, 0      (terminador)
```

`SPRxPT` apunta al **inicio** (la cabecera), no al primer `DAT`: Agnus recarga `POS`/`CTL` de
ahí al armar el sprite. Un buffer con solo `DAT/DATB`+terminador se interpreta como cabecera →
posición basura y sprite mal colocado.

## Lo observado en el fuente

- `custom.cpp:1189-1193`: `sprite_sprctlmask` — los bits de `SPRxCTL` que el emulador rastrea
  como «especiales» por chipset: `bit 0` (OCS), `+bit 4` (ECS), `+bits 3,4` (AGA). *La
  decodificación exacta de `VSTART[8]`/`VSTOP[8]` en el emulador queda por confirmar; el layout
  que usa el engine sigue el AHRM cap. 4 y lo valida la demo.*
- La colisión (`drawing.cpp`) y los registros (`custom.cpp`) tratan el sprite ya armado; el
  arranque de la estructura lo hace el DMA.

## Sprites alimentados por Copper: avance del DMA y columna fantasma

Un canal de sprite **siempre** avanza su `SPRxPT` mientras el DMA de sprites está activo, aunque
el Copper reescriba `SPRxPOS`/`SPRxDATA` por línea. El DMA solo lee de `SPRxPT`:

- `custom.cpp:10055-10120` (`generate_sprites`): si `dmaen(DMA_SPRITE)` y el canal está en
  `dmacycle`, cada línea hace 1-2 fetch desde `s->pt`; `s->pt` **avanza** (`custom.cpp:12019-12023`,
  `s->pt = r->pv`). Si `dmastate==0` el valor leído se interpreta como **cabecera**
  (`SPRxPOS`/`SPRxCTL_DMA`, `custom.cpp:12012-12018`); si `dmastate==1`, como `DATA`/`DATB`.
- `custom.cpp:4018-4083` (`sprstartstop`): `dmastate` se pone a 1 cuando `vpos == vstart` y a 0
  cuando `vpos == vstop`. Por eso el Copper, al escribir `SPRxCTL` con `VSTART = línea`,
  **rearma el DMA** de ese canal cada línea, que sigue leyendo de `SPRxPT`.

Consecuencia: un canal que el Copper alimenta pero cuyo `SPRxPT` apunta a una estructura corta
(«nula») hace que el DMA avance por memoria. Al terminar la banda, el fetch con `dmastate==0`
lee como cabecera la DATA de la estructura siguiente (p. ej. `0xAAAA`), lo que produce
`VSTART=0xAA`/`VSTOP` mayor y deja el sprite **armado hasta el final del frame**: la **columna
fantasma** fuera de la banda de la demo `207_sprite_layer`.

Dos condiciones evitan el fantasma:

1. Cada canal (DMA y Copper) apunta a una **estructura válida** con cabecera y terminador, de
   modo que el avance del DMA quede acotado.
2. El `WAIT` del rearmado Copper cae **después del fetch DMA** (`DDFSTRT`) y antes de la primera
   columna: así la `DATA` que escribe el Copper no la pisa el fetch del DMA.

## Validación

- `demos/techniques/amiga/sprites/206_sprite_collision`: sprite en estructura con cabecera → la **colisión
  registra** (bit 1 de `CLXDAT`); sin cabecera, no.
- `demos/techniques/amiga/sprites/207_sprite_layer`: una estructura DMA por canal (DMA y Copper) + `WAIT` de
  rearmado en `arm_hpos` → render correcto y **sin** columna fantasma.

## Referencias

- AHRM 3.ª, cap. 4 (Sprite DMA) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md) §4.
- Ejemplo `spr_layer/Sprite_Layer/` (Jeroen Knoester).
