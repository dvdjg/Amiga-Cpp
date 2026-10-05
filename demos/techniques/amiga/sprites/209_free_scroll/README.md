# 209_free_scroll — scroll de fondo **no repetitivo** (imagen de 320 px) con sprites

Demuestra una **imagen de fondo de 320 px (20 tiles de 16 px) NO repetitiva** mostrada íntegra con
los 8 canales de sprite, que se **rearman por línea** (`SPRxPOS` **y** `SPRxPT`) para cubrir el
ancho con tiles **distintos** (no un patrón que se repite), con **scroll de 1 px/frame**. Dos
variantes a comprobar: **4 colores** (8 sprites sueltos) y **16 colores** (4 pares *attached*).

Es el **Free Form real**: a diferencia de la [208_risky_woods](../208_risky_woods/README.md) (que
**reutiliza la DATA** de un patrón fijo y solo mueve `SPRxPOS`), aquí cada columna es un trozo
distinto → el Copper cambia también el puntero `SPRxPT` (o la `SPRxDATA`).

## Coste (presupuesto de Copper: cada `MOVE`/`WAIT` = 8 px lo-res)

**Clave:** un canal de sprite muestra en cada línea su **misma `DAT`/`DATB`** (16 px). Reposicionar
solo la `SPRxPOS` **repite** ese contenido. Para que cada columna sea **distinta** hay que reescribir
también el dato — la `SPRxDATA`+`SPRxDATB` (2 `MOVE`) **o** el `SPRxPT` (2 `MOVE`) — además de la
`POS` (1 `MOVE`) → **3 `MOVE` por columna**. Con 8 canales y 20 columnas hacen falta **3 pasadas**.

| Variante | `MOVE` por columna | 20 columnas | ¿cabe en la línea (~56 instr.)? |
|---|---|---|---|
| **4 colores** | `POS` + `DAT` + `DATB` = 3 | **60 `MOVE` ≈ 480 px** | **NO** |
| **16 colores** | 2·`POS` + 4·(`DAT`/`DATB`) = 6 | **120 `MOVE` ≈ 960 px** | **NO** |

Conclusión esperada (a verificar): una imagen de **320 px no repetitiva NO cabe solo con Copper**,
ni en 4 colores. Es el motivo por el que el **Free Form real usa el Blitter** (regenera la DATA de
una sola estructura por línea) y no solo Copper. La [208_risky_woods](../208_risky_woods/README.md)
y la [207_sprite_layer](../207_sprite_layer/README.md) son baratas precisamente porque **reutilizan
la DATA** (patrón fijo); en cuanto el fondo es libre, hace falta el Blitter.

## Plan (a "ver a dónde llegamos")

1. **Cota honesta**: medir cuántas columnas **distintas** caben solo con Copper antes de saturar la
   línea (≈18 columnas a 3 `MOVE` = 288 px → ~288 px de 320; el resto repetido o por Blitter).
2. **Free Form con Blitter**: el Blitter recompone la DATA de un único juego de estructuras por
   línea (regenerando la fila de 320 px) mientras el Copper solo reposiciona la `POS` (1 `MOVE`/col).
3. Comparar 4 colores (8 canales) vs 16 colores (4 pares *attached*, DATA de 4 bits).

## Datos

- **20 tiles de 16 px × 80 líneas** (una estructura DMA cada uno: `[POS, CTL, DAT0, DATB0, …, 0, 0]`).
- Scroll: **16 sets pre-shifteados** (1 px) × 20 tiles = 320 estructuras (≈105 KB en Chip RAM);
  el scroll cambia de set (parche de las palabras `SPRxPT` por frame, coste ~0 CPU).

## Etapas

| Etapa | Qué | Estado |
|---|---|---|
| **E0** | 8 tiles **distintos**, estático (validar "no repetitivo" vs 208). | — |
| **E1** | 20 tiles (3 pasadas) → **320 px** a 4 colores. | — |
| **E2** | scroll de 1 px/frame por sets pre-shifteados + parche de `SPRxPT`. | — |
| **E3** | variante **16 colores** (4 pares *attached*) — cota de ancho. | — |

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/209_free_scroll --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/209_free_scroll --keep-running
```

## Referencias

- [Catálogo de técnicas de sprites](../../../../docs/reference/amiga/techniques/sprite-techniques-catalog.md)
  (técnica 6, *Free Form Sprite Layer*).
- [207_sprite_layer](../207_sprite_layer/README.md) (`effects::SpriteLayer`, patrón repetido).
- `docs/reference/emulators/winuae/sprite-dma.md` (estructura DMA y columna fantasma).
