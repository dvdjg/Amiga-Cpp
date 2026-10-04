# Catálogo de técnicas con sprites hardware (Amiga OCS/A500)

Inventario de las técnicas históricas construidas sobre los **8 canales de sprite DMA** del
chipset, con su coste, límites y encaje en el engine. Los **mecanismos** (formato de datos,
colores por par, attached, prioridad `BPLCON2`, colisión `CLXCON/CLXDAT`) están en
[sprite-layer.md](sprite-layer.md); el multiplexado horizontal, en
[sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md). Aquí se catalogan las
**técnicas** y se enlazan los **casos reales** de juego en
[sprite-tricks-games.md](sprite-tricks-games.md).

## Base del hardware

- **8 canales DMA**, cada uno de **16 px** de ancho (32/64 en AGA) y **altura arbitraria**.
- **3 colores + transparente** (2 bitplanes). **Attached por pares** (0+1, 2+3, 4+5, 6+7) →
  **15 colores + transparente** (4 bitplanes). Reduce de 8 a 4 objetos independientes.
- La paleta de sprites son los registros **`COLOR16`–`COLOR31`**; el índice 0 de cada par
  (`COLOR16`/`20`/`24`/`28`) es siempre **transparente**. Los canales par e impar de un par
  **comparten** sus `COLORxx`.
- El sprite se dibuja **on the fly** (sin save/restore del fondo, a diferencia de un BOB) y
  puede quedar **delante o detrás** de cada playfield según `BPLCON2`.
- X solo en **píxeles lores pares** (`HSTART` va ÷2); el píxel impar vive en el bit 0 de
  `SPRxCTL`. En hires el movimiento sigue siendo de 2 px.

## Registros que se tocan

| Registro | Para qué | Detalle |
|---|---|---|
| `SPRxPOS` | Posición del canal | byte alto = `VSTART[7:0]`; byte bajo = `HSTART[8:1]` (X/2, solo pares). Escribirlo **rearma** el canal (`vpos==vstart`) y provoca **redibujado** cuando el haz iguala la X. |
| `SPRxCTL` | Control | byte alto = `VSTOP[7:0]`; **bit 7 = ATTACH**; bit 2 = `VSTART[8]`; bit 1 = `VSTOP[8]`; **bit 0 = `HSTART[0]`** (px impar). Escribirlo durante la línea **DESARMA** el canal. |
| `SPRxDATA` / `SPRxDATB` | Datos (2 bitplanes) | Escribirlos **recarga y arma** el canal: la DATA pasa al registro de desplazamiento al coincidir `SPRxPOS`. Recargar por línea = **animación en caliente**. |
| `SPRxPTH`/`SPRxPTL` | Puntero a la estructura DMA | Cambiarlo cambia la imagen completa (animación de frames / set pre-shifteado). La estructura = cabecera `POS`+`CTL` + DATA por línea + final válido. |
| `COLOR16`–`COLOR31` | Paleta de sprites | Los canales de un **par comparten** sus `COLORxx`; el índice 0 del par es transparente. Cambiarlos por franja = **palette splitting**. |
| `BPLCON2` | Prioridad sprite↔playfield | `PF1P`/`PF2P`: delante o detrás de cada playfield. Por franja = **prioridad dinámica**. |
| `DMACON` (`SPREN`, bit 5) | DMA de sprites | **Un único bit** habilita los 8 canales. Un canal se apaga con `SPRxPT=0` o `VSTOP<=VSTART`. |
| `CLXCON` / `CLXDAT` | Colisión de hardware | `CLXCON` habilita pares/planos; `CLXDAT` (se autolimpia al leer) da el resultado. |

## Cómo se hace cada cosa

| Efecto | Operación |
|---|---|
| Colocar un objeto | `SPRxPOS` + `SPRxCTL` (y `SPRxPT` → estructura con la DATA). |
| **Animar por frame** | Cambiar `SPRxPT` (otra estructura/imagen) — o inyectar `SPRxDATA`/`SPRxDATB`. |
| **Scroll fino de 1 px** | Bit **H0** de `SPRxCTL` (par/impar) + **8/16 versiones pre-shifteadas** elegidas por `SPRxPT`. |
| **Scroll por posición** | Sumar el offset a la X en `SPRxPOS` (par) + H0 para el impar. |
| **Multiplexado vertical** | Rearmar `SPRxPT`/`POS`/`CTL` del canal en **otra Y** (gap ≥1 línea). |
| **Multiplexado horizontal** | Reescribir `SPRxPOS` (y a veces `SPRxDATA`/`SPRxDATB`) **dentro de la línea**. |
| **Attached (15 colores)** | Bit 7 de `SPRxCTL` del canal **impar** del par, misma POS en ambos. |
| Color por zona | `COLOR17`-`COLOR31` por franja (Copper) = palette splitting. |
| Prioridad por zona | `BPLCON2` por franja (Copper). |
| **Colisión** | `CLXCON` + lectura de `CLXDAT` (o cajas/máscaras por software si hay multiplexado). |

## Inventario de técnicas

| # | Técnica | Qué es | Coste / clave | Ejemplos |
|---|---|---|---|---|
| 1 | **Sprites básicos de objeto** | Jugador, enemigos, balas, power-ups, HUD | Cero CPU; sin save/restore | R-Type, Saint Dragon (casi todos los shmups) |
| 2 | **Attached (15 colores)** | Dos canales del mismo par unidos por el bit `ATTACH` | Reduce 8→4 objetos; mismo coste DMA | Personaje/boss grande (multi-juego) |
| 3 | **Multiplexado vertical** | Rearmar el canal más abajo (otra Y) en la misma lista DMA | ≥1 línea vacía entre reusos; decenas de "VSprites" | Saint Dragon (4 canales → 15 balas) |
| 4 | **Multiplexado horizontal (Copper)** | El Copper reposiciona `SPRxPOS` dentro de la misma línea (y a veces recarga DATA) | Carrera contra el haz; ≥24 px entre usos del mismo canal | Fondos repetidos, ensanchar >128 px |
| 5 | **Fondo repetitivo *Risky Woods*** | 8 sprites (4 pares attached) = patrón de 64 px, reposicionado cada 16/32 px a lo ancho | Muy barato si el scroll va por punteros pre-shifteados | Risky Woods |
| 6 | **Free Form Sprite Layer** | Redesplegar los 8 canales (POS **y** DATA) para un fondo **libre**, sin patrón | Alto (4 copperlists, Blitter); 3-4 colores | [spr-layer](https://www.powerprograms.nl/amiga/spr-layer.html) (Roondar) |
| 7 | **Combinación con BOBs** | Sprites para jugador/balas/HUD/parallax; BOBs (Blitter) para enemigos/plataformas/multicolor | El DMA de sprites compite con el Blitter | Risky Woods, Jim Power (enemigos por Blitter) |
| 8 | **Colisión** | Hardware `CLXDAT`/`CLXCON` (sprite↔sprite, sprite↔bitplane) | No pixel-perfect con multiplexado; sin posición | La mayoría usaba cajas/máscaras por software |
| 9 | **Offset por fila (bending)** | El Copper cambia `SPRxPOS` cada línea (o cada pocas) con una tabla | Tabla de seno; parcheo barato por frame | Ondas, distorsión, parallax por franjas; Leander |
| 10 | **Palette splitting** | El Copper cambia `COLOR17`-`COLOR31` por franja de altura | Mismo sprite con paletas distintas arriba/abajo | Cielo/montaña/suelo; Shadow of the Beast |
| 11 | **Avanzadas** | Prioridad dinámica (`BPLCON2` por franja), modo manual (POS/DATA por CPU/Copper), *sprite chasing* extremo, capas de parallax, HUD de sprites | Ver cada una | Shadow of the Beast, Parasol Stars, Leander |

## Efectos especiales

### Animación del bitmap del sprite (estilo Jim Power / Brian the Lion)

El Copper escribe **`SPRxDATA`/`SPRxDATB`** mientras el haz dibuja el sprite: el canal saca
la nueva palabra de píxeles **a partir de ese momento** de la línea. Combinado con cambios de
`SPRxPOS`, el mismo canal muestra **patrones distintos** a lo largo de la línea (sin
pre-shiftear miles de frames). En **4 colores** el Copper puede escribir un word cada ~8 px.
Detalle y copperlist real en [sprite-tricks-games.md](sprite-tricks-games.md) §Jim Power.

### Sprite bending (wave / distorsión)

Cambiar `SPRxPOS` **por línea** con una **tabla de seno** precalculada (256 o 512 entradas,
amplitud ±8/16/32). Cada frame solo se avanza un puntero de fase. La lista puede ser estática
y parchearse por frame con el Blitter o la CPU en el VBlank. Técnicas derivadas: bending por
sprite (fase/amplitud propia), bending + animación de DATA, doble onda (dos senos sumados).

### Sprite chasing horizontal extremo

Mover el mismo sprite varias veces en la misma línea (solo POS, a veces DATA) para crear
franjas o montañas a pantalla completa (**Leander**). Es la generalización de la técnica 4/5.

## Organización de memoria del Free Form (referencia de las implementaciones reales)

```text
SpriteDataA / SpriteDataB      ; 8 canales x (2 words POS/CTL + altura*2 DATA + 2 terminador)
                               ; 224 lineas -> ~904 B/sprite; x2 buffers ~14-15 KB
CopperList_Visible_A/_B/_Work_A/_Work_B   ; 4 copperlists (doble buffer x 2: datos + posiciones)
                               ; cabecera (colores/BPLCON/punteros/BPL setup) ~500-1000 B
                               ; + por linea: 1 WAIT + ~40-42 MOVE (~85 words ~170 B)
                               ; 224 lineas -> ~38 KB/lista; x4 -> ~152 KB
```

| Elemento | Tamaño (304×224) |
|---|---:|
| 8 sprites × 2 buffers | 14–15 KB (por sprite `(224*4)+8 = 904` B; 8 = 7 232 B; doble buffer = 14 464 B) |
| 4 copperlists | ~152 KB (por lista `(224*42)*4 + 512 = 38 144` B; ×4 = 152 576 B) |
| **Total efecto** | **167 040 B ≈ 163 KB** (176 líneas: ahorra ~34,5 KB → ~128,5 KB) |

Con 176 líneas el efecto cabe en 512 KB de chip, pero se recomienda **≥1 MB**.

## Scroll y actualización diferida (pestaña *Scrolling*)

Dos enfoques:

- **A. Scroll por cambio de datos**: regenerar la DATA shifteada cada vez que el fondo se
  mueve 1 px (simple pero caro si se hace todo en un frame).
- **B. Posición + datos diferidos (el del Free Form de Roondar)**: los 8 sprites DMA se
  actualizan normalmente; el resto de instancias viven **solo en la Copperlist**, con
  **4 Copperlists** (Visible A/B + Work A/B).

Clave del scroll por posición: cada sprite tiene dos partes para la X — **`SPRxCTL`** dice si
es **par o impar** y **`SPRxPOS`** da la X en **incrementos de 2 px**. Como el fondo se mueve
**1 px cada 2 frames**, entre dos updates de `SPRxPOS` pasan 4 frames:

```text
          SPRxCTL   SPRxPOS
Frame 0      0         8
Frame 1      0         8
Frame 2      1         8
Frame 3      1         8
Frame 4      0         7
...
```

Reparto del trabajo (se construye el par Work mientras se muestra el Visible):

| Fase | Frames | Qué se actualiza |
|---|---|---|
| Posiciones | 0-3 | X (`SPRxPOS` / bit bajo de `SPRxCTL`) de la lista siguiente: **19 sprites** (288/16 = 18 + 1) → 4,75 columnas = **1 064 words** por frame |
| Datos | 4-31 | DATA de las columnas (posiciones + 11 sprites no cubiertos por DMA): **22×2 + 16 = 60 columnas** en 32 frames → **1 875 columnas = 420 words** por frame |
| Swap | 32 | Se intercambian los pares de Copperlists; el update de posiciones empieza 4 frames antes |

El Blitter rellena en **modo clear** (posición fija) y con blits de columnas de tiles; la lista
se organiza **por columnas** (16 px) para que un solo blit cubra una columna entera de altura.

Estrategia general: mientras se muestra un par Visible, el Blitter + CPU construyen el par
Work; swap cada 32 frames (o al avanzar 16 px). Como el fondo se mueve despacio, hay margen.

## Estrategias eficientes para actualizar la copperlist

1. **Doble/cuádruple buffering**: nunca modificar la lista en ejecución.
2. **Blitter para rellenar**: *clear* con el valor de `SPRxPOS`; blits de columnas de tiles.
3. **Estructura por columnas** (16 px) en vez de por líneas: un blit actualiza una columna.
4. **Separar POS y DATA**: las posiciones se actualizan mucho más a menudo y son baratas.
5. **Precalcular variantes shifteadas** (8/16) para scroll de 1-2 px.
6. **Limitar la altura** del efecto; el coste es lineal con el número de líneas.

## Costes medidos (Roondar / *Free Form Sprite Layer*)

El coste del efecto es **directamente proporcional al número de líneas** que cubre.

**Por línea del efecto (medido por Roondar):**
- Copper: **1 `WAIT` + 41 `MOVE` = 85 ciclos DMA**/línea.
- Blitter (scroll, repartido): 19 words cada 4 frames (posiciones) + 22 words cada 32 frames
  (datos de los 11 sprites no cubiertos por DMA) ≈ **6 words/frame = 12 ciclos DMA/frame**.
- **Total ≈ 97 ciclos DMA por línea** del efecto.

**Coste total por efecto (304×224, 4 planos + panel):**

| Efecto | Coste (ciclos DMA) |
|---|---:|
| Sprite layer estándar (224 líneas) | 8 736 |
| Risky Woods (224 líneas) | 16 800 |
| Risky Woods (176 líneas) | 13 200 |
| Free Form estático (224 líneas) | 19 040 |
| Free Form con scroll (224 líneas) | 21 728 |
| Free Form con scroll (176 líneas) | 17 072 |

**Presupuesto restante para BOBs (32×32, ~2304 ciclos cada uno, 90 % de eficiencia):**

| Pantalla | BOBs/frame |
|---|---:|
| Sin efecto | 19 |
| Sprite layer estándar | 14 |
| Risky Woods (224 líneas) | 11 |
| **Risky Woods real** (304×176 + panel 288×48×5) | **13** |
| Free Form estático | 10 |
| Free Form con scroll | 9 |
| Free Form con scroll (176 líneas) | 12 |

Nota: los números asumen un programa **básico**. La lógica real de juego **los baja** (difícil
de cuantificar, depende del overhead). La actualización de DATA para el scroll es "sorprendentemente
barata" porque se **reparte en 32 frames** (el fondo se mueve 1 px cada 2 frames).

## Cálculo del presupuesto de bus (pestaña *Calculations*)

Fórmulas para dimensionar un efecto sobre el bus del A500 (los números de arriba salen de aquí).

**Bus disponible:** `226 × 312 = 70 512` ciclos DMA por frame PAL. **Refresh + audio** consumen
`2 304`, quedando **68 208** para el resto.

**Coste DMA por elemento:**
- **Bitplanes:** 1 ciclo DMA por word mostrado. `coste = (ancho/16) × alto × profundidad`.
  - 304×224×4 = `(304/16)·224·4 = 17 024`; 304×176×4 = 13 376; panel 288×16×3 = 864 (16 líneas) / 2 592 (48) / 4 320 (48×5 planos).
- **Sprites:** 2 ciclos DMA **por canal y línea** → `16`/línea para los 8 (2 816 @176, 3 584 @224).
- **Copper:** 1 `WAIT` = 3 ciclos; 1 `MOVE` = 2. Estándar = 1 wait + 18 moves = 39/línea; Risky
  Woods = 1 wait + 36 moves = 75/línea; Free Form = 1 wait + 41 moves = 85/línea.
- **Blitter (BOB 32×32 intercalado, 4 planos, con restore):** 2 ciclos/word copiado y 4/word
  cookie-cut → `3 words × 32 líneas × 4 planos × 6 = 2 304` ciclos/bob. Eficiencia ~90 %.

**Presupuesto de ejemplo (304×224×4 + panel 288×16×3):** el bitmap+panel consume 17 888; sin
sprites → libres **50 320** (`50 320/2 304 ≈ 22 × 0,9 ≈ 19` BOBs, el "sin efecto" de la tabla);
con los 8 sprites DMA (+3 584) = 21 472 → libres 46 736. Las demás filas de la tabla de BOBs
restan el coste del efecto del presupuesto restante.

## Limitaciones a tener en cuenta en el engine

- Solo **8 canales reales** → el multiplexado es **obligatorio** si hay más de 8 objetos.
- Al menos **1 línea** vacía entre reusos verticales del mismo canal.
- El tiempo DMA del Copper **compite** con el Blitter y con los bitplanes.
- Los sprites son **low-res** (movimiento de 2 px incluso en hires).
- Con scroll horizontal o **>4 bitplanes** se **pierden canales** de sprite (el fetch ancho
  come los slots de los canales 6-7 y a veces 4-5).

## Encaje en el engine

- **Reparto de canales por franja** (la pieza que permite mezclar técnicas y dejar canales
  libres para objetos): `graphics/sprite_band.hpp` + `SpriteAllocator` con ledger;
  diseño en [SPRITE_BANDS.md](../../engine/architecture/SPRITE_BANDS.md), tests HOST-416/417.
- **Driver de fondo por reposición**: `effects::RiskyWoodsLayer` (`api/effects.hpp`).
- **Pendiente**: fondo Free Form (datos distintos por columna), animación de DATA del sprite
  (Jim Power), bending por tabla de seno, y el scroll por cambio de punteros pre-shifteados.

## Referencias

- Amiga Hardware Reference Manual 3.ª: cap. 4 (Sprite), cap. 7 (`BPLCON2`, `CLXCON/CLXDAT`).
- Casos reales: [sprite-tricks-games.md](sprite-tricks-games.md) y los artículos de
  codetapper.com/amiga/sprite-tricks/.
- Fuente del emulador para el comportamiento fino: `../WinUAE-DBG/custom.cpp`,
  `drawing.cpp` (ver [diagnóstico 208](../../debugging/investigaciones/risky-woods-208-sprite-scroll.md)).
