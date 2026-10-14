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

## Ventana de fetch por línea y pacing del Copper (demo 215)

El DMA de sprites de una línea **solo se programa mientras el haz no ha llegado al fetch de
bitplanes**: en `generate_sprites` el fetch solo ocurre si `hp <= plfstrt_sprite`
(`../WinUAE-DBG/custom.cpp:10108-10112`) y `plfstrt_sprite` se fija al **arranque del
secuenciador de bitplanes** (BPRUN) de esa línea (`custom.cpp:9888-9889`; se limpia a
`0x100` al terminar, `custom.cpp:9916/9930/9971`). Con 4 planos el fetch de bitplanes
arranca pronto (`DDFSTRT=$38` en las demos), así que **la ventana útil para que una
escritura de `SPRxDATA`/`DATB` afecte a la fila de esa misma línea es corta**: si el
Copper escribe después de que el fetch de la línea haya pasado, la DATA nueva se usa en la
**siguiente** línea. Es la raíz del pacing de la técnica Jim Power (demo 215): el Copper
debe escribir cada columna **justo antes** de su ventana, y por eso la copperlist original
usa `hpos` variable por línea.

Coste del Copper (AHRM 3.ª, cap. 5, «Copper»; `docs/reference/ahrm/…cat.md:500` y `:514`):

- el Copper es un procesador de **dos ciclos** que pide el bus **solo en los ciclos de
  memoria impares** (los pares son de audio, disco, refresh, sprites y, en low-res, del
  display);
- **MOVE/SKIP**: 2 ciclos de memoria y 2 words → **4 tiempos de ciclo de memoria por
  instrucción** ≈ **8 píxeles low-res por MOVE** (el «un word cada 8 píxeles» del hilo);
- **WAIT**: 3 ciclos de memoria y 6 tiempos + 1 ciclo de despertar.

Cota de fondo con 4 planos: la CPU puede compartir ciclos alternos si el display tiene
**4 o menos** planos; con **más de 4**, el fetch de bitplanes empieza a robar ciclos a la
CPU (AHRM `:5767`).

**Implicación para el engine:** el driver de bending (F4.2) y el patrón Jim Power escriben
en la **misma familia de ventana**; las medidas de la demo 220 (64 líneas, 764 words)
caben porque solo hay un sprite por canal y el fetch de cada fila ocurre al principio de
línea. Para reescribir DATA a media línea (215) hay que ajustar el `hpos` del `WAIT` por
línea al hueco real (variable, como el original), no un `hpos` fijo.

## Validación

- `demos/techniques/amiga/sprites/206_sprite_collision`: sprite en estructura con cabecera → la **colisión
  registra** (bit 1 de `CLXDAT`); sin cabecera, no.
- `demos/techniques/amiga/sprites/207_sprite_layer`: una estructura DMA por canal (DMA y Copper) + `WAIT` de
  rearmado en `arm_hpos` → render correcto y **sin** columna fantasma.

## Referencias

- AHRM 3.ª, cap. 4 (Sprite DMA) + [ERRATA_Y_NOTAS.md](../../ahrm/ERRATA_Y_NOTAS.md) §4.
- Ejemplo `spr_layer/Sprite_Layer/` (Jeroen Knoester).
