# Sprites hardware en juegos reales (análisis de *Sprite Tricks*)

Cómo usaron los **8 canales de sprite** los juegos clásicos, destilado de la serie *Sprite
Tricks* de [codetapper.com](https://codetapper.com/amiga/sprite-tricks/) y del *Free Form
Sprite Layer* de [powerprograms.nl](https://www.powerprograms.nl/amiga/spr-layer.html)
(Jeroen Knoester). El inventario de técnicas está en
[sprite-techniques-catalog.md](sprite-techniques-catalog.md); los mecanismos, en
[sprite-layer.md](sprite-layer.md) y [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md).

## Risky Woods — fondo de 64 px repetido con 8 sprites (15 colores)

- **8 canales = 4 pares attached** (15 colores) → un **patrón de 64 px** que se repite por
  toda la pantalla.
- **Copperlist estática**: por línea, **un solo `WAIT`** (hpos `$30`) y una **ráfaga de
  `SPRxPOS`** que reposiciona los pares: `SPR0POS/SPR1POS=$48`, `SPR2POS/SPR3POS=$50`,
  `SPR4/5=$58`, `SPR6/7=$60`, y repite `$68,$70,…` (paso 8 en POS = **16 px**). Las X
  **crecen** y los canales **ciclan**; la 1.ª columna va **24 POS (~48 px) por detrás** del
  `WAIT` (ventaja del Copper sobre el haz). Un canal nunca se reutiliza a <96 px.
- **Scroll sin mover las X**: se pre-bufferan **8 sets** del patrón desplazados **2 px** y por
  frame se reescriben los **`SPRxPT`** de los 8 canales (8×2 words) + el **bit H0** del
  `SPRxCTL` (par/impar). "8 sprites × 2 words = 32 bytes" → coste ~0. El juego recorta el
  ancho a 287 px para no delatar el borde.
- Al acabar cada línea, las posiciones quedan a la derecha; la ráfaga de la **línea
  siguiente** las vuelve a poner a la izquierda.
- Detalle de hardware y errores típicos:
  [diagnóstico 208](../../debugging/investigaciones/risky-woods-208-sprite-scroll.md).

## Jim Power — 2 sprites repetidos (fondo 4 colores) + inyección de DATA

Reparto de canales:

- **0+1 attached**: personaje principal + barra de estado inferior.
- **2+3 attached**: lado derecho del personaje.
- **4+5**: balas del jugador (límite de 2 en pantalla).
- **6+7**: fondo de **4 colores** repetido + barra superior.

Fondo (patrón de **32 px**, 2 sprites no attached): el Copper, por línea, **carga la DATA**
(`SPR6DATA`/`SPR6DATB`, `SPR7DATA`/`SPR7DATB` — ¡el patrón se anima al vuelo!) y luego
**reposiciona** `SPR6POS=$40, SPR7POS=$48, SPR6POS=$50, SPR7POS=$58, …` (paso 8 = 16 px)
hasta cubrir la pantalla. Es **animación de DATA + reposición** en la misma línea: no hace
falta pre-shiftear miles de frames. Cada línea del efecto usa **1 `WAIT` + ~41 MOVEs**.

- Las **bandas de color sólido** del fondo se aprovechan para **cambiar el resto de la
  paleta** de la pantalla sin que se note en el fondo de sprites.
- Limitaciones autoimpuestas: al no quedar tiempo de DMA para el Copper en las líneas del
  fondo, **no se reutiliza ningún otro canal** → 2 balas; el personaje nunca supera 32 px;
  **todos los enemigos y plataformas van por Blitter**.

## Agony — dual playfield 3+3 con lluvia y 3 capas de parallax

- **DPF 3+3** (2 conjuntos de 8 colores) + 16 colores de sprites. La ilusión de **3 capas**:
  el back playfield se parte en dos: **bitplane 2** = capa estática (luna, nubes, montaña,
  2 colores de los que 1 es transparente, no scrollea); **bitplanes 4 y 6** = capa con
  árboles/rocas/**mar animado** (4 colores, scroll a 25 fps). El agua es una **secuencia de
  12 frames** redibujada cada 4 frames. El front playfield (bits 1,3,5) = 8 colores.
- **Lluvia con sprites 6+7**: el mismo gráfico se dibuja **dos veces por línea**, y cada
  update se mueve a otra posición → efecto de lluvia realista. Sprites con **prioridad más
  alta** (encima de todo).
- Reparto: **0+1** mitad izquierda del búho (+ espada), **2+3** mitad derecha (ambos attached,
  16 colores), **4+5** balas, **6+7** lluvia.
- **Truco de paleta (colores duplicados)**: `COLOR10=COLOR11=0x0050`, `COLOR12=COLOR13`,
  `COLOR14=COLOR15`. Así, tanto si la capa estática (luna) tiene bit como si no, el color
  resultante es el del fondo → la capa de fondo **tapa** la estática de forma limpia.
- `BPLCON2=0x0024` → **sprites con prioridad máxima** sobre los playfields. `COLOR00` se
  cambia a azul cielo entre hpos `$42` y `$d6` de cada línea (y vuelve a negro) para que el
  fondo no sea negro; el panel de score aplica un **degradado** cambiando `COLOR03` por línea.

## Shadow of the Beast — 11 capas de parallax, sprites tras los playfields

- **DPF 3+3**; el back playfield lleva **11 capas** de parallax (5 de nubes, montañas, 5 de
  hierba) cambiando punteros `BPL2/4/6PT` y `BPLCON1` por franja a lo largo de la línea.
- **3 bandas de sprites**, con **reset de punteros + cambio de paleta + cambio de prioridad**
  entre bandas:
  1. **Arriba, prioridad máxima** (`BPLCON2=0x0024`): **0+1** contador de vidas (14 líneas con
     `COLOR17` en degradado por línea), **2+3** monitor de latidos, **4+5** poción azul,
     **6+7** poción morada (attached, 16 colores).
  2. **Medio, prioridad mínima** (`BPLCON2=0x0000`): los sprites (blimp grande 0-3, luna 4-7,
     blimp pequeño 0-1) van **detrás** de los playfields; solo se ven **donde el color 0 es
     el fondo**, así que el Copper **cambia `COLOR00` a azul cielo entre hpos `$40` y `$d0`**
     de cada línea (y vuelve a negro) para que asome el azul.
  3. **Abajo**: los 8 sprites **lado a lado** forman un **árbol de 128 px**; prioridad
     intermedia; paleta común de 3 tonos marrones (`COLOR17..31`).
- Paletas cambiadas **por línea** a lo largo de cientos de líneas (nubes, "Psygnosis
  presents", hierba, piedra) → parte del "milagro" visual. Típico uso de **palette splitting
  + prioridad dinámica + reset de punteros** por conmutación de bandas.

## Free Form Sprite Layer (spr-layer, Roondar)

Evolución del truco de Risky Woods para un fondo **de forma libre** (cualquier tilemap, 3
colores sin patrón repetitivo):

- Se muestran los **8 sprites juntos** (128 px) y, mientras el haz avanza, el Copper
  **reescribe `SPRxPOS` + `SPRxDATA`/`SPRxDATB`** de los canales ya dibujados, moviéndolos a
  la derecha con **datos nuevos**. Necesita **≥24 px** de separación entre instancias.
- Cálculo del reparto (hasta 320 px): 128 px de DMA + 5 sprites rearmados antes del px 128
  (→208), +3 hasta 256, +2 hasta 288, +2 hasta 320.
- **Scroll**: en vez de regenerar los datos, se **actualiza la X** (bit par/impar del
  `SPRxCTL`) y se **reparte la actualización de DATA** en el tiempo con **4 copperlists**
  (visible A/B + work A/B): posiciones en 4 frames, datos en 32 (el fondo se mueve 1 px cada
  2 frames). El Blitter rellena las columnas; la lista se organiza **por columnas**.
- Costes y memoria: ver [sprite-techniques-catalog.md](sprite-techniques-catalog.md)
  (Free Form estático ~19k, con scroll ~21.7k ciclos DMA; ~163 KB de chip con 4 copperlists).

## Encaje en el engine

- **Risky Woods**: driver `effects::RiskyWoodsLayer` + reparto por franja
  ([SPRITE_BANDS.md](../../engine/architecture/SPRITE_BANDS.md)); demo `208`.
- **Jim Power (animación de DATA)**, **Free Form (datos por columna)**, **bending por tabla
  de seno** y **scroll por punteros pre-shifteados**: pendientes (catalogados en
  [sprite-techniques-catalog.md](sprite-techniques-catalog.md) §Encaje en el engine).

## Referencias

- [codetapper.com — Risky Woods](https://codetapper.com/amiga/sprite-tricks/risky-woods/)
- [codetapper.com — Jim Power](https://codetapper.com/amiga/sprite-tricks/jim-power/)
- [codetapper.com — Agony](https://codetapper.com/amiga/sprite-tricks/agony/)
- [codetapper.com — Shadow of the Beast](https://codetapper.com/amiga/sprite-tricks/shadow-of-the-beast/)
- [powerprograms.nl — Free Form Sprite Layer](https://www.powerprograms.nl/amiga/spr-layer.html)
