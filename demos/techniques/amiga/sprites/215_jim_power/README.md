# 215_jim_power - **NO VERIFICADA**: fondo por sprites con DATA por línea (Jim Power)

> **Estado: NO VERIFICADA (bloqueo de pacing).** Lo implementado funciona a medias: el
> par *attached* de contraste y la **animación de DATA por línea** (parcheo de `DAT/DATB`
> por línea con fase vertical) están en su sitio, pero la **repetición POS** solo pinta una
> columna de las 6/16 previstas: el Copper adelanta al haz y reescribe el `SPRxPOS` de cada
> canal antes de que su columna termine de dibujarse. No usar como referencia validada.

## La técnica (objetivo)

Jim Power construye el fondo con **2 canales de sprite no attached** (6 y 7) de 4 colores:
el Copper, por línea, carga la **DATA** de cada canal (`SPR6/7DATA/DATB`) y luego
**reposiciona** cada canal a lo ancho para repetir el patrón de 32 px hasta cubrir la
pantalla. La DATA se anima al vuelo (no hay frames pre-shifteados). Referencias:
`docs/reference/amiga/techniques/sprite-tricks-games.md` §Jim Power y
`sprite-horizontal-multiplex.md`.

## Qué funciona (cerrado)

- **Animación de DATA por línea**: la tabla del patrón (32 filas × 2 mitades de 16 px, 4
  colores) se parchea por frame con `move_at` (~640 words/frame, sin regenerar la lista).
- **Estructuras DMA OFF** para los canales alimentados por Copper (`winuae/sprite-dma.md`)
  y `CTL` con el rango de la banda (comparador vivo).
- **Par *attached* de 15 colores** encima con `SpriteManager::arm_object` (técnica 214).
- `WAIT` de línea en `arm_hpos=0x40` (tras el fetch DMA, como 207); sin él la DATA del
  Copper la pisaba el fetch del canal.
- Presupuesto: con 4 planos (~160 slots/línea) caben ~24 MOVEs/línea de Copper.

## Síntoma abierto (pacing Copper↔haz)

Estado actual: DATA por línea servida por el **DMA** (estructura con cabecera, `CTL` de banda,
fila `pad` para absorber el primer fetch) + **ráfaga pura de POS** por línea (WAIT $20,
$40..$d8 paso +$8, sin WAITs intermedios). Resultado medido: **solo se pintan las últimas
columnas** (~2 de 20); el Copper adelanta al haz mucho más de un período, de modo que el
comparador solo ve las X finales de cada canal.

Experimentos registrados:
- Burst completo sin `WAIT` y con PT a estructura OFF (DATA por Copper): solo la última X.
- `WAIT` por período (receta de banda no-*attached* de la 208, `kCuGap=32`): pinta la primera
  columna y el Copper se cuelga en el segundo `WAIT` (llega cuando el haz ya pasó) → 1 columna.
- WAIT $20/$40 + 4 MOVEs dummy (simulando la recarga de DATA real): sin cambio apreciable.
- DATA por DMA (estructura) + ráfaga pura: ~2 columnas (las últimas).

Hipótesis abiertas (consulta enviada a Grok con estas medidas):
1. Coste real de un `MOVE` de Copper con **4 planos + DMA de sprites** activos (robo de bus)
   y, con él, el **head-start exacto** del `WAIT` para que el Copper quede 0–1 período por
   delante y no adelante a las columnas.
2. Por qué la copperlist real de Jim Power usa `hpos $1c/$20/$24` **variable por línea**:
   ¿compensa el robo de bus por línea o el slot de DMA de cada canal?
3. Si la variante correcta es DATA por **columna** (`POS+DATB+DATA`, como la 213) con 14
   columnas, asumiendo el coste de parchear la DATA de la ráfaga por frame.

Mientras tanto, el mecanismo validado equivalente (POS+DATA por columna, capa no
repetitiva) es el **Free Form de la demo 213** (`effects::SpriteLayer`).

Mientras tanto, el mecanismo validado equivalente (POS+DATA por columna, capa no
repetitiva) es el **Free Form de la demo 213** (`effects::SpriteLayer`).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/215_jim_power --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/215_jim_power
```

## Referencias

- `docs/reference/amiga/techniques/sprite-tricks-games.md` §Jim Power.
- `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` (pacing, ≥24 px por canal).
- `demos/techniques/amiga/sprites/208_risky_woods` (banda no-*attached* con `WAIT` por período).
- `demos/techniques/amiga/sprites/213_spr_layer` (Free Form validado).
- `docs/debugging/investigaciones/consulta-jim-power-data-line-pacing-en.md` (consulta).
