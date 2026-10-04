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

## Otras fichas (*Sprite Tricks*)

### Saint Dragon — multiplexado vertical de balas con 4 canales

- **Reparto**: canales 0-3 para las **balas** (fondos/parallax por bitplanes); paletas 17-19 y
  21-23 duplicadas en verdes para que todas las balas se vean igual. Sprites 4-7 en null.
- **Técnica**: **multiplexado vertical puro**: balas ordenadas por Y; cada canal se rearma en
  varias franjas (p.ej. sprite 0 en `$5e-$65, $7e-$85, $8b-$92, $a2-$a9`). 4 canales → **15
  balas**. Cada reuso = 2 words POS/CTL + 7 longwords de gráfico (4 colores, 2 planos).
- **Copper**: `BPLCON2=$003f` (sprites con máxima prioridad); conmuta bitplanes/`COLOR01` y scroll.
- **Límite**: con solo 4 canales, dos balas en la misma horizontal → una desaparece; con
  `DDFSTRT=$28` los sprites 6 y 7 no están disponibles (scroll horizontal).

### Parasol Stars — panel de texto con un solo sprite (mux horizontal + palette split)

- **Reparto**: se configuran los 8, pero el efecto usa **solo el sprite 6** para el panel
  superior ("BUBBY", "PRESS FIRE…").
- **Técnica**: por línea, `SPR6POS` avanzando de 8 en 8 px (`$40,$48,…,$d8`) + `SPR6DATA` con la
  palabra del gráfico → panel de **64 px** reusando la franja DMA, con `WAIT` en hpos `$30`.
  7 líneas por fila de fuente. Generado con macros `rept`/`set`, no a mano.
- **Palette split**: reescribe `COLOR31` por línea (`$04f4` verde antes de BUBBY, `$04ff` azul
  antes de PRESS FIRE) → dos colores con un sprite de 1 bit.
- **Idea**: HUD de texto barato (COLOR + POS + DATA por línea) sin tocar bitplanes.

### Brian the Lion — capas de 2 colores anchas y parallax

- **Reparto** (4 planos): **0+1 attached** cuerpo de Brian (16 colores); **2+3** parte media +
  icono de vida + contador de joyas; **4+5** parte derecha + vidas + "red thing"; **6+7** joya
  azul, palmeras y bola del panel.
- **Técnica**: **mux horizontal con parcheo de DATA por Copper**: en 16 colores escribe un word
  cada 8 px. Nube: `SPR7POS` con paso 8 px y **solo `SPR7DATA`** (deja `SPR7DATB` fijo, 2
  colores) repetido 4 veces → 64 px. Palmeras: alterna las DATA de los sprites 6 y 7.
  Datos citados: `SPR7POS=$68b0/$68b8/$68c0/$68c8`, DATA `$0175,$7f5f,$ffff,$dc00`.
- **Palette**: `COLOR00` por línea (degradado `$09ff,$0aff,…` y vuelta a `$0000`).
- **Límite**: solo 4/5 planos; en **6 planos no hay DMA** para cambiar DATA a mitad de línea.

### Pac-Mania — cuándo **no** usar sprites

- **Reparto**: DPF a 50 fps + **4 sprites en attach** (15 colores) para Pacman; con overscan
  (340×272) varios sprites quedan deshabilitados desde el de número más alto (solo 0-3). La
  **sombra** de Pacman va en el **foreground** (no en sprite).
- **Anti-técnica**: **no hay multiplexado**; se "engaña" moviendo a Pacman más arriba y haciendo
  bloques más altos/separados para que el foreground nunca lo ocluya (no saben recortar).
  Fondo = bitmap de **880×848 px (280 KB chip)** sin double-buffer; foreground sí.
- **Copper**: `BPLCON0` conmuta DPF (`$0200`→`$6600`→`$0200` en vpos `$2B`), `BPLCON2=$0044`,
  sprites 4-7 con `SPRxPT=$4FF4`, `SPRxCTL=0`.
- **Idea**: útil como contraste — a veces conviene resolver la oclusión por **layout** en vez de
  por sprite.

### R-Type 2 — fondo de 64 px con los 8 sprites (mux horizontal)

- **Reparto**: los 8 sprites dibujan el fondo (SPR4=SPR0…); en realidad solo 4 juegos de datos.
- **Técnica**: 8 sprites cada 16 px (128 px, `SPR0POS=$3F48/$3F50/…/$3F80`); tras `WAIT` hpos
  `$80` reposiciona **+$40** (otros 128 px) y tras `$B0/$C0` mueve 2 más; se repite por ~192
  líneas. Patrón de 64 px → nivel de indentación × 64 = **1024 px de ancho, 192 alto, ~50 KB**.
- **Copper**: el layer "aparece de golpe" alternando `COPJMP1` a una lista que salta la sección
  de sprites vs un NO-OP (`$01FE`). Al cubrir pantalla, scroll a media velocidad (parallax).
- **Idea**: pocos sprites redirigidos y multiplexados en X con `WAIT` de hpos.

### Rod-Land — HUD de 16 colores con sprites attached

- **Reparto**: los 8 sprites forman **solo el panel de score** en el hueco de 64 px, en pares
  **attached** (`$1602/$1682`) → 16 colores. Sin objetos/balas por sprite durante el juego.
- **Técnica**: posicionado manual, **sin multiplexado**; `SPR0POS=$36C0, SPR2POS=$36C8,…` (paso
  8 px) en vpos `$30`; `BPLCON2=$000F`; `DDFSTRT $38`/`DDFSTOP $B0` y módulos `$0002`.
- **Palette**: juego en 16 colores (COLOR00-15); el panel usa COLOR16-31. El title cambia paleta
  por bandas aprovechando huecos de DMACON → 42 colores en 4 planos + *color cycling* del logo.
- **Límite**: `BPLCON1` (smooth scroll) desactiva el sprite 8 → entre niveles hacen blit de toda
  el área (4 px/frame).

### Stardust — dual-playfield, túnel animado y espejo por módulo negativo

- **Reparto** en 3 bandas: arriba 8 sprites de 3 colores (score/distancia); en medio la nave
  usa 4 sprites con **2 attached** (15 colores, ≤64 px); abajo life counter con 2 attached.
- **Técnica**: DPF (`BPLCON0 $5600`, 3 planos foreground + 2 background) para mantener 50 Hz.
  Túnel = **animación de 6 frames de 4 colores dithered**, 448×384; `BPL2MOD=$FFA2` (−94) en
  vpos `$6F` para **espejar verticalmente** y reutilizar la mitad. Punteros de sprite y paleta
  cambian por banda.
- **Límite**: la nave no puede entrar en las bandas superior/inferior (>8 sprites/línea).
- **Idea**: espejo por hardware (módulo negativo) para duplicar geometría/animación.

### Stunt Car Racer — "falso" framerate actualizando solo los punteros de sprite

- **Reparto**: solo **SPR0/SPR1** para la parte alta de los neumáticos (2 variantes según
  posición); SPR2-7 comparten datos.
- **Técnica**: el mundo 3D vive en un hueco de 256×128 del dashboard; las ruedas (sprites) se
  superponen. El refresh del 3D está **bloqueado cada 6 frames** (~8-9 fps), pero actualizar a
  menudo los **`SPR0PT`/`SPR1PT`** en la copperlist da sensación de mayor framerate.
- **Idea**: refrescar solo punteros de sprite mientras el fondo bitmap permanece varios frames.

### Videokid — parallax de fondo con 6 sprites multiplexados

- **Reparto**: **6 sprites** multiplexados = capa de fondo/parallax; los otros 2, en otra parte.
- **Técnica**: patrón de **96×176** repetido 3 veces a lo ancho = **288×176**; el display (286
  px) recorta 1 px para no delatar el borde. Para scroll de 1 px ajusta las **control words** de
  los 6 sprites; para 2 px cambia a otro juego de sprites. Copper: `WAIT` en hpos `$40` y `$88`
  por línea, con 176 conjuntos de `SPR2POS`-`SPR7POS`.
- **Palette**: los 4 colores de sprite se repiten 3 veces (pares con entradas distintas);
  COLOR16-31 duplicados. El panel cambia a 16 colores vía Copper en vpos `$EB`.
- **Límite**: 8 copias en memoria (40 KB chip).

### WWF Wrestlemania — sprites de cuerda de baja prioridad

- **Reparto**: 3 sprites de **16 colores** para las cuerdas, con **prioridad más baja** (detrás
  de todo). Con smooth scroll se pierde un sprite → 3×16 = 48 px de ancho; altura ilimitada
  (175 px). Fondo en tilemap 16×16 (220 tiles, área 45×24 = **1080 B** de byte-tilemap).
- **Palette**: 16 colores duplicados (sprites usan COLOR16-31); transparente = azul oscuro.
- **Límite**: con 3 sprites no caben ambos sets de cuerdas; para cruzar entre cuerdas usan un
  frame del luchador con la cuerda dibujada (glitch al cruzar alto).
- **Idea**: reservar sprites de baja prioridad + frames con elementos de fondo incrustados.

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
- [codetapper.com — Saint Dragon](https://codetapper.com/amiga/sprite-tricks/saint-dragon/)
- [codetapper.com — Parasol Stars](https://codetapper.com/amiga/sprite-tricks/parasol-stars/)
- [codetapper.com — Brian the Lion](https://codetapper.com/amiga/sprite-tricks/brian-the-lion/)
- [codetapper.com — Pac-Mania](https://codetapper.com/amiga/sprite-tricks/pac-mania/)
- [codetapper.com — R-Type 2](https://codetapper.com/amiga/sprite-tricks/r-type-2/)
- [codetapper.com — Rod-Land](https://codetapper.com/amiga/sprite-tricks/rod-land/)
- [codetapper.com — Stardust](https://codetapper.com/amiga/sprite-tricks/stardust/)
- [codetapper.com — Stunt Car Racer](https://codetapper.com/amiga/sprite-tricks/stunt-car-racer/)
- [codetapper.com — Videokid](https://codetapper.com/amiga/sprite-tricks/videokid/)
- [codetapper.com — WWF Wrestlemania](https://codetapper.com/amiga/sprite-tricks/wwf-wrestlemania/)
- [powerprograms.nl — Free Form Sprite Layer](https://www.powerprograms.nl/amiga/spr-layer.html)
