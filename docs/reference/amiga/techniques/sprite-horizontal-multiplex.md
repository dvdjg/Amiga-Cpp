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

## Límites y notas

- El efecto consume **mucho tiempo de raster** y es **proporcional al número de líneas** que ocupa: conviene acotarlo a una banda, mezclar con una banda de sprite layer estándar o dejar zonas sin efecto.
- El número de DMA del Copper tiene que caber **entre el borde izquierdo y la posición de cada rearmado**: es una **carrera contra el haz**, no un presupuesto por frame.
- `SPRxPOS`/`SPRxCTL` son *write-only* en la práctica (su lectura no es fiable). Las posiciones y data deben vivir en Chip RAM.
- **AHRM:** capítulo 4 (Sprite): "Reusing Sprite DMA Channels" (~línea 3507), "Manual Mode" (~3703), control del hardware y `SPRxPOS`/`SPRxCTL`/`SPRxDATA`. Índice: [amiga-hardware-manual-index.md](../../ahrm/amiga-hardware-manual-index.md).
- **Estado en el engine:** `SpriteIntent` ya modela la **tira horizontal de canales contiguos** (`strip_id`/`strip_index`/`strip_span`, ver `engine/include/eng/graphics/raster_intent.hpp`), que cubre el "attach" y el reparto en canales. **Falta** el rearmado horizontal del mismo canal (reescritura de `SPRxPOS`/`SPRxCTL` + data a media línea), hoy no expresable con `CopperIntentKind`; ver la nota de kinds pendientes en `raster_intent.hpp`.
