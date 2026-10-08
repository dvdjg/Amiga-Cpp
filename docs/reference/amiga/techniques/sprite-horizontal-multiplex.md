# Multiplexado horizontal de sprites (fondos tipo Risky Woods)

- **Referencias:** [Free Form Sprite Layer (powerprograms.nl)](https://www.powerprograms.nl/amiga/spr-layer.html) (Jeroen Knoester, 2018) y la serie [Sprite Tricks (codetapper.com)](http://codetapper.com/amiga/sprite-tricks/) sobre el método de *Risky Woods*.
- **Idea:** el Copper reposiciona y recarga la DATA de un canal de sprite **horizontalmente dentro de la misma línea**, no solo entre líneas. Así un canal dibuja varios tramos de 16 px a lo ancho, y los 8 canales se reparten la pantalla entera como una capa de "sprites" continua (fondo o primer plano ancho con paleta propia).
- **Diferencia con el multiplexado vertical:** el vertical (`SpriteRearm`) reaprovecha un canal en **otra Y** (más de 8 objetos en pantalla, separados ≥1 línea). El horizontal reaprovecha un canal en **otra X** (objeto de más de 64 px o varios tramos en la misma línea), reescribiendo `SPRxPOS`/`SPRxCTL` y la data.

## Mecanismo

En modo DMA, el canal lee la DATA de cada línea durante el H-Blank (2 DMA por línea y canal). El Copper **puede reescribir `SPRxPOS` en cualquier momento** (AHRM cap. 4, "Manual Mode": *"If you write to the SPRxPOS register, you can manually move the sprite horizontally at any time, even during normal sprite usage"*), y escribir a `SPRxDATA`/`SPRxDATB` recarga la imagen y **arma** el sprite para el siguiente comparador horizontal. Combinando ambos, el canal aparece otra vez más a la derecha con otra imagen.

```
            una línea de barrido (lores)
  ┌──────────────────────────────────────────────────────────────┐
  │ canal 0:  [16px]        [16px]        [16px]        [16px]    │  ← rearmado horizontal
  │           ↑SPRxPOS/CTL  ↑SPRxPOS/CTL  ↑SPRxPOS/CTL  ...       │     cada ≥24 px
  └──────────────────────────────────────────────────────────────┘
```

- **Reposición mínima:** ≥ **24 px** entre el uso original de un canal y su copia, para que al Copper le dé tiempo a escribir los registros de posición/data (la carrera Copper vs haz).
- **Ancho máximo de un canal:** 16 px (o 64 px con `attach` = 4 canales unidos a 15 colores). Sin rearmado, un canal solo cubre esos 16/64 px por línea.

## Variantes

| Variante | Descripción | Coste Copper/línea |
|---|---|---|
| **Sprite layer "estándar"** | 8 canales contiguos (o attach 2 a 2), sin rearmado: cubre 128 px | `1 wait + 18 moves` = 39 DMA ciclos |
| **Risky Woods** | 2 canales con `SPRxPOS` actualizado **cada 16 px**: patrón de **64 px repetitivo a 15 colores** | `1 wait + 36 moves` = 75 DMA ciclos |
| **Free Form** | redespliegue de los **8 canales** (posición **y** DATA) según avanza el haz: fondo **libre, sin patrón repetido**, 3 colores/capa | `1 wait + 19 moves` (repos) `+ 11×2 moves` (data) = 85 DMA ciclos |
| **Free Form + scroll** | lo anterior + actualización repartida en varios frames (4 copperlists) | 85 (Copper) + 12 (Blitter) = 97 DMA ciclos |

Datos de la fuente (resolución 304×224, 4 planos + panel 288×16×3):

| Efecto | Coste total | BOBs 32×32 restantes |
|---|---:|---:|
| Sin efecto | 0 | 19 |
| Sprite layer estándar | 8 736 | 14 |
| **Risky Woods** (224 líneas) | 16 800 | 11 |
| **Risky Woods** (juego real, 176 líneas) | 13 200 | 13 |
| **Free Form** estático | 19 040 | 10 |
| **Free Form** con scroll | 21 728 | 9 |

- **Coste Copper:** 1 WAIT = 3 DMA ciclos; 1 MOVE = 2 DMA ciclos; cada canal sprite = 2 DMA ciclos por línea (16 en total para los 8).
- **Scroll (técnica de la fuente):** no desplazar la imagen, sino **mover la X** del sprite (`SPRxPOS` en pasos de 2 px + el bit de paridad/impar en `SPRxCTL`) y repartir la actualización de la DATA de los 8 canales en **4 copperlists** a lo largo de varios frames (se actualizan ~1,875 columnas de 16 px por frame). La posición hace falta 4 frames antes que la data.
- **Memoria (Free Form con scroll, 224 líneas):** 4 copperlists (~152 KB) + sprites doble-buffer (~14 KB) ≈ **163 KB**; recomendable ≥1 MB. La variante estática usa **1 sola copperlist**.

## Cómo se monta el *Free Form* (claves verificadas)

Las **dos claves** de la copperlist (fuente: `spr_layer/Data/copperlists.asm`, Jeroen Knoester):

1. **Por posición, SOLO `SPRxPOS` + `SPRxDATB` + `SPRxDATA`** — **no** se escribe `SPRxCTL`
   (escribirlo **desactiva** el comparador; `SPRxDATA` es quien **arma** el sprite en la nueva X).
   Escribiendo el `CTL` por posición el rearmado **no dibuja**.
2. **Al final de cada línea** hay que **reposicionar los 8 canales a la izquierda, en orden inverso**
   (`SPRxPOS` solo), para que el renglón siguiente los vuelva a dibujar por DMA en su sitio.

Estructura por línea (referencia, 212 px de alto, canales 0–7 por DMA + columnas extra por Copper):

```
  WAIT (inicio de línea)
  8× [SPRxPOS, SPRxDATB, SPRxDATA]            ; columnas 0..7 (reusa canales 0..7)
  N× [SPRxPOS, SPRxDATB, SPRxDATA]            ; columnas 8.. (reusa canales k%8)
  8× [SPRxPOS = posición izquierda]           ; fin de línea (orden inverso 7..0)
```

Posiciones del fuente (`spr_layer/Data/copperlists.i`): `SPRPOS=$48` (X=144), `DMAPOS=$2c00|SPRPOS`
(VSTART=$2c; posición de los DMA), `SPRRPOS=$7f00|SPRPOS+$40` (X=272; columnas Copper 0–11), y las
columnas **12+ usan `DMAPOS`** como base. El `VSTART` de las posiciones de rearmado **no es la línea
actual** sino el de la constante. El orden **inverso** en la reposición de fin de línea evita que el
haz alcance una X antes de reposicionarla. Un montaje que ponga `VSTART = línea actual` en todas las
posiciones deja una **junta de una columna** en la frontera entre las columnas de base distinta.

El **`WAIT`** va al inicio de la línea (después del fetch DMA de sprites, `DDFSTRT`). Un `WAIT` por
canal en su X **no** funciona. La CPU queda libre; se devora **DMA de Copper**.

## Procedimiento reproducible (capas free-form nuevas)

Receta para montar una capa free-form en otra demo/juego, en el orden que evita los fallos que ya
costaron diagnóstico (referencia completa: `218_free_form_sprite_layer`; módulos del engine:
`graphics/sprite_line_layer.hpp`, `effects::SpriteLayer`):

1. **Geometría primero, en papel**: `n` columnas de 16 px (8 por DMA + `n-8` por Copper reusando
   canales `k%8`), alto de banda `L` líneas. La X de cada columna avanza 8 unidades de `SPRxPOS`
   (= 16 px); la reposición de fin de línea va **en orden inverso (7→0)** con `SPRxPOS` solo.
2. **Estructuras DMA por canal** (doble juego A/B): cabecera `POS,CTL` + `L` parejas
   `DATA,DATB` + terminador `0,0`. Sin terminador el canal sigue leyendo memoria y deja la
   *columna fantasma* (`docs/reference/emulators/winuae/sprite-dma.md`).
3. **Copperlist por línea**: `WAIT` al inicio + ráfaga de `SPRxPOS,SPRxDATB,SPRxDATA` por columna
   (**nunca `SPRxCTL` por columna**: desarma el comparador; `SPRxDATA` es quien arma) + reposición.
   Los punteros `SPRxPT` de la lista se fijan al juego de estructuras del par.
4. **Scroll = mover X, no imagen**: `SPRxPOS` baja 1 unidad (1 px lores) cada 4 updates con
   **vuelta a 0 tras 7** (`cpos_offset`), y el bit 0 de `SPRxCTL` alterna cada 2 (`$0C03/$0C02`)
   para el medio píxel. Reparto: posiciones en 4 updates (cuartos de columna, repartiendo la
   última entre bloques), DATA en 32 updates (1 columna/frame: `frames 0-7` estructuras,
   `8-18` lista 1, `19-29` lista 2 tras retroceder 11 columnas, `30` retrocede 18 y el avance neto
   es +1 columna/ciclo). El mundo avanza **una columna por ciclo de 32**; el
   reset de posición (+14 px en 4 unidades) se compensa con el avance de contenido (−16 px).
5. **Doble buffer de pares**: 4 copperlists; el par en pantalla y el par que se escribe nunca
   coinciden. El índice de lista mostrada, el de escritura de DATA y el de estructuras flipan
   juntos en el wrap de 32; el índice de posiciones, 4 updates antes (c32==28).
6. **Anclaje de fase**: esperar **siempre la misma línea temprana** (`WaitRaster 0x2c` del
   original) al inicio del update. Sin ancla, las escrituras vivas (`SPRxCTL` y las `SPRxPOS` de
   las estructuras) caen en posiciones de haz distintas cada frame y barren la capa.
7. **Un solo buffer de playfield** (si hay playfield): el selector por bit alto de un contador
   0..31 del original es **código muerto, siempre el mismo buffer**. Alternar buffers por update
   parece inofensivo pero introduce un salto periódico del playfield cuando el puntero de planos
   avanza (medido: +8 px cada 16 updates).
8. **Presupuesto**: medir con los contadores del periférico de depuración
   (`run-demo.sh --read-debugperiph counters`); el update debe caber con holgura dentro del
   periodo (en la 218: 220k de 284k ciclos = 2 campos) y el periodo quedar clavado al ancla.

Medición y validación de la dinámica (lo que distingue un fallo real de un artefacto):

- **Capturar a resolución de update con fase fija**: watchpoint de **escritura en `COP1LC`**
  (`0xDFF080`) — una captura por update, el mismo punto del bucle siempre. La captura por *probe*
  muestrea a resolución de frame y puede mostrar “congelados” que no existen.
- **Correlacionar sobre contenido no periódico**: el tramado de la capa aliasa en cualquier
  búsqueda de paso 2 (y el texto/BOBs pertenecen al playfield y contaminan la máscara). Usar el
  **cuerpo de montaña** (grises medios) o bordes con contraste; con `dx` de paso 1 cuando se mide
  medio píxel.
- **Comparar contra la referencia con el mismo método** antes de tocar nada: si el patrón del port
  es idéntico al del original, no hay bug. Cierre con visión (regla de oro) sobre una secuencia.

## Límites y notas

- El efecto consume **mucho tiempo de raster** y es **proporcional al número de líneas** que ocupa: conviene acotarlo a una banda, mezclar con una banda de sprite layer estándar o dejar zonas sin efecto.
- El número de DMA del Copper tiene que caber **entre el borde izquierdo y la posición de cada rearmado**: es una **carrera contra el haz**, no un presupuesto por frame.
- `SPRxPOS`/`SPRxCTL` son *write-only* en la práctica (su lectura no es fiable). Las posiciones y data deben vivir en Chip RAM.
- **AHRM:** capítulo 4 (Sprite): "Reusing Sprite DMA Channels" (~línea 3507), "Manual Mode" (~3703), control del hardware y `SPRxPOS`/`SPRxCTL`/`SPRxDATA`. Índice: [amiga-hardware-manual-index.md](../../ahrm/amiga-hardware-manual-index.md).
- **Estado en el engine:** la **capa de fondo** completa está en `effects::SpriteLayer`, con **free form** (`Config::columns` + `Config::image`: DATA distinta por columna y línea → fondo **no repetitivo**) y `bind`/`patch` para el scroll **~0 CPU** (demo `212_free_scroll_layer`, 320 px no repetitivos + scroll). El emit por línea es **`WAIT` + ráfaga de `SPRxPOS`+`SPRxDATB`+`SPRxDATA`** (canales extra ciclando `k % channels`) **+ reposición de fin de línea** (ver arriba: **sin** `SPRxCTL` por posición). Cada canal (DMA y Copper) **necesita una estructura DMA válida** con cabecera y terminador; si no, el DMA del canal avanza por memoria y deja una **columna fantasma** (ver `docs/reference/emulators/winuae/sprite-dma.md`). Un `WAIT` por canal en su X **no** funciona. `graphics::SpriteHorizontalRearm` + `Scheduler::emit_sprite_horizontal_rearm` cubren un rearm suelto; para una **capa/HUD con imagen propia por línea** el módulo es `graphics/sprite_line_layer.hpp` (`SpriteLineLayer`, HOST-418).
- **Recreación fiel de la referencia:** la demo
  [`218_free_form_sprite_layer`](../../../../demos/techniques/amiga/sprites/218_free_form_sprite_layer/README.md)
  transcribe el programa completo de Jeroen Knoester (19 columnas de 16 px, 4 copperlists con dos
  pares y doble buffer de estructuras de sprite, `UpdateLayerPos` en 4 frames, `UpdateLayerData`
  en 32, `UpdateSprCtl` cada 2, playfield de 4 planos con scroll de 1 px/frame, 9 BOBs con
  restore desde el 3.º buffer y sub-buffer de 3 planos). La pantalla de título resultante es
  **píxel a píxel idéntica** al ejecutable original (ver `VALIDATION.md` de la demo). La variante
  parcial (solo capa de sprites, sin playfield) es la demo `213_spr_layer`. Resumen del
  subsistema: [sprite-layer.md](sprite-layer.md) §11.
