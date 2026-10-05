# Demo 208 — fondo de sprites estilo *Risky Woods*: diagnóstico y reinicio

**Estado: E0–E4 verificados.** El reparto híbrido y el scroll funcionan: E0 (1 canal),
E1 (8 sueltos / patrón 128 px), E2 (repetición a 320 px sin huecos), E3 (scroll 1 px/frame
por punteros pre-shifteados + rotación de columna) y E4 (3 franjas: 8 sueltos / 6 + 2
objetos / 4 pares attached, con scroll común). Hallazgos clave: auto-armado de canales
(§2.5), el display empieza en X≈128 (§2.6) y las **libcalls 32-bit son ~50-150 ciclos**
(§2.7).

## 1. Objetivo

Una capa de fondo con **6 canales de sprite** (16 px cada uno, período 96 px) que cubra
**320 px de lado a lado**, con **scroll horizontal de 1 px por frame** y **2 canales
libres** (0/1) para objetos. Es el reparto híbrido de `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.

## 2. Hechos de hardware verificados (fuente: WinUAE-DBG)

Leídos en `../WinUAE-DBG/` (regla §1.12 de `AGENTS.md`).

### 2.1 El dibujado lo dispara la coincidencia de X

`drawing.cpp:2644` (`sprwrite`) y `:2656`:

- El sprite se **dibuja** cuando `s->armed && s->xpos_lores == denise_hcounter` (el haz
  iguala la X actual): se copia la DATA al registro de desplazamiento.
- Por tanto **el mismo canal puede aparecer varias veces en una línea** reescribiendo
  `SPRxPOS` a X mayores: cada coincidencia vuelve a disparar el dibujado.
- **Si se reescribe `SPRxPOS` a una X que el haz aún no ha alcanzado, y luego se vuelve a
  escribir antes de que llegue, la instancia anterior se PIERDE** (solo cuenta el último
  valor). De ahí el mínimo ≈24 px entre usos del mismo canal.

### 2.2 Escribir `SPRxCTL` DESARMA el canal

`drawing.cpp:2695` (`sprwrite`, rama no-DATA): si `second` (CTL) → `s->ctl = v; spr_arm(s, 0)`
→ desarma. **En la carrera solo se escribe `SPRxPOS`; `SPRxCTL` únicamente en el armado.**

### 2.3 El fetch DMA ocurre solo al inicio de la línea

`custom.cpp:10056` (`generate_sprites`): el fetch se hace solo si `hp <= plfstrt_sprite`
(blanking izquierdo). Con `dmastate`:
- slot 0 lee **POS** (si desarmado) o **DATA** (si armado); reg = `0x140 + num*8 + (dmastate?4:0)`.
- slot 2 lee **CTL** (si desarmado) o **DATB**.
- `custom.cpp:4018` (`sprstartstop`): `dmastate=1` cuando `vpos == vstart`; `dmastate=0`
  cuando `vpos == vstop`. Escribir `SPRxPOS` con `VSTART = línea` **rearma** el canal.

### 2.4 Consecuencias

- La **DATA se fetchea 2 words/línea** mientras el canal está armado; el `SPRxPT` avanza
  solo. Para un patrón **repetido** basta UNAR estructura con las N líneas de DATA.
- El canal **necesita una estructura DMA válida** (cabecera `POS`+`CTL` y un final que NO
  deje `VSTART=0`; un terminador `0,0` provoca que la **línea 0** del frame siguiente
  re-dispare el fetch y el canal quede desfasado 2 words → *sprites fantasma*).

### 2.5 El DMA **auto-arma** un canal apuntado a una estructura con `VSTART` válido

Cuando un canal está desarmado y su `SPRxPT` apunta a una estructura, el DMA lee la
**primera word como `POS`** y usa su `VSTART` para armarlo: un canal "de sobra" que apunte
a la estructura del fondo **se dibuja solo**. En la 208 el reset apuntaba los **8** canales
a la misma estructura → todos dibujaban la columna y ganaba el canal 0 (con sus colores
COLOR17-19, no los del canal pedido). Fue la causa de los huecos y de los objetos perdidos.

Los canales no usados deben apuntar a una estructura **desactivada** donde *todas* las words
tengan `VSTART=VSTOP` (nunca arma): **no** `0,0` (arma en la línea 0 → fantasma) y **no** la
estructura del fondo. Verificado en E0: con el fix, el canal 2 muestra **COLOR21/22/23**.

### 2.6 El display visible empieza en X lo-res ≈ 128

El borde izquierdo usa `COLOR0` (aquí navy), así que no se distingue del fondo. Los sprites
con `SPRxPOS` HSTART < 64 (X < 128) caen en el **borde izquierdo** y no se ven en la captura;
el fondo de 320 px arranca en **X ≈ 128**. Verificado en E1: con `kDisplayX0=128` la figura
aparece en x=128..255; con `kDisplayX0=0` no aparece nada.

### 2.7 Las libcalls aritméticas (`__udivsi3`/`__mulsi3`) son lentísimas en `-nostdlib`

La generación de estructuras **por píxel** (E4: 3 franjas × 16 sets ≈ 1,35 M píxeles) con
divisiones, módulos o multiplicaciones de 32 bits paga una **libcall (~50-150 ciclos)** por
operación: el `init` tardaría decenas de segundos y el runner **expira** (parece un cuelgue:
`state=2`, con el PC en la ROM por una IRQ del sistema en el momento de la captura). **No es
un crash: es un timeout.** Solución: **precalcular en compilación** (una tabla `constexpr`
hace que el compilador evalúe la división) y usar `mulsw`/`mulu.w` (16-bit nativo) y
desplazamientos; para el módulo, un lazo de resta. Ver `docs/reference/toolchain/m68k-gcc.md`.

### 2.8 Prioridad de sprites: **canal mayor = detrás** (los sprites 0/1 van DELANTE)

**Un sprite con número de canal mayor se dibuja DETRÁS de uno con número menor**, con independencia del orden de la copperlist. En E4 el fondo de la franja B usaba los canales 0..5 y los dos objetos los canales 6/7: los objetos quedaban **ocultos tras el fondo** (el fondo los tapaba) aunque sus estructuras, `POS` y paleta fuesen correctas. Síntoma engañoso: parecía que los objetos «no se dibujaban», pero bastaba mirar que no había ni un píxel de su color.

Solución: **el fondo de la franja B ocupa canales 2..7** (número alto = detrás) y **los objetos 0/1** (número bajo = delante). Como los objetos pasan al par de canales (0,1), su paleta es el grupo **COLOR16-19** (no el 28-31 que usaban en 6/7); el fondo de la franja B, al usar 2..7, emplea **COLOR20-31**, de modo que ambos grupos no se solapan y la recarga de paleta de los objetos (COLOR16-19) no afecta al fondo. El ancho visible va de `kDisplayX0=128` a `128+320=448`; la X de los objetos debe acotarse a `[128, 448-16]` (acotar a `[0, 304]` los pegaba al borde y los solapaba).

## 3. El algoritmo de Risky Woods (artículo de codetapper)

- 4 pares **attached** (8 canales) = patrón de **64 px** a 15 colores.
- Copperlist **estática**: por línea, **un único `WAIT` en hpos `$30`** y luego una
  **ráfaga de `SPRxPOS`** con X creciente de 16 en 16 px, **ciclando canales**
  (`0→1→…→7→0`); la X avanza 8 en unidades de POS (=16 px). No intercala WAITs.
- El **scroll** no mueve las X: se generan **8 sets pre-shifteados** (paso 2 px) y por
  frame se **reescriben los `SPRxPT`** (8 canales × 2 words) + el **bit H0** del CTL
  (par/impar). "8 sprites × 2 words = 32 bytes" por frame → **coste ~0**.
- La 1.ª columna cae `$18` (24 unidades = **48 px**) por detrás del `WAIT`: la "ventaja"
  para que el Copper gane al haz.

## 4. Errores concretos ya identificados en mis intentos

1. **WAIT en la X de la primera columna del período** → esa columna se pierde (el haz ya
   pasó). Los WAITs de pacing deben caer **por detrás** de la X que van a escribir.
2. **Columnas del DMA armadas en X>0** se pisan antes de dibujarse cuando el carrusel
   reescribe ese canal → huecos entre bloques. El artículo arma los canales juntos y deja
   que el Copper los reparta.
3. **Desajuste de stride** (`kBgStride` contaba un terminador que el generador ya no
   escribía) → el fetch de DATA se desalineaba por estructura → mortero (color 2) fuera
   de la ventana.
4. **Terminador `0,0`** → `VSTART=0` → rearme en la línea 0 → canal desfasado (fantasma).
5. **Re-emitir la copperlist cada frame** para el scroll: funciona pero es lo contrario
   al diseño (el original es ~0 de CPU).

## 5. Plan de reimplementación (por etapas verificables)

Cada etapa con su evidencia; **no se avanza sin cerrar la anterior** (protocolo de
etapas). Reutiliza el ledger/allocator (`graphics/sprite_channel_window.hpp`, ya en verde HOST-416)
y el driver `effects::RiskyWoodsLayer` (HOST-417).

- **E0 — Un solo canal, ventana pequeña, estático.  ESTADO: OK (verificado).** 1 canal,
  16 px, sin scroll: el canal dibuja su columna y **se sostiene** durante la ventana.
  Evidencia: captura con columna estable 16×96 en `x=160`, tiras COLOR21/22/23 (los 3
  colores del par 2/3). Fijado: estructura DMA (cabecera + DATA + candado `VSTART=VSTOP`,
  sin `0,0`) y **desactivación** de los canales no usados (§2.5).
- **E1 — 8 sprites sueltos lado a lado (128 px), UNA franja, sin repetir.  ESTADO: OK
  (verificado).** 8 canales, cada uno con su estructura, forman una **figura coherente**
  (colina: cielo azul, suelo marrón, cresta amarilla) de 128×80. Evidencia: captura con la
  figura en x=128..255 (borde izquierdo del display). Fija la geometría y el mapeo de color
  por pares (canales 0/1→COLOR17-19, 2/3→21-23, …; todos los pares con los MISMOS 3 colores
  para coherencia).
- **E2 — Repetición a 320 px (carrera contra el haz).  ESTADO: OK (verificado).** Por línea,
  por período (X = 128, 256, 384), un `WAIT` con margen (`kCuGap=24 px`) por detrás de la
  1.ª columna y una **ráfaga** de `SPRxPOS` (8 canales; el 3.er período 4). Ciclar canales
  reutiliza cada uno cada 128 px (≥24 px). Evidencia: figura a x=128..447 con **0 columnas
  vacías** (sin huecos); Ollama ve 3 repeticiones continuas. La copperlist se emite **una vez**
  (CPU ~0); el coste es de Copper.
- **E3 — Scroll por cambio de punteros** (8 sets pre-shifteados + bit H0), copperlist
  **estática**. Verificación: secuencia de frames consecutivos; el patrón se mueve 1 px por
  frame y el coste por frame son ~20 words.
- **E4 — Objetos (2 canales libres)** animados sobre la ventana. Verificación: secuencia +
  los dos objetos visibles con color propio del par (uno por `DAT`, otro por `DATB`).
- **E5 — Gate visual** (Ollama) + FPS/flicker.

## 6. Preguntas a resolver antes de E2

- La X exacta del `WAIT` y de la 1.ª columna (la "ventaja") para 4 bitplanes y `DDFSTRT=$38`.
- El orden exacto de la ráfaga (canal y X por MOVE) que garantiza que ningún canal se
  reescribe antes de que el haz dibuje su instancia previa.
- Cómo cubrir el **borde izquierdo (0..X_inicial)** sin que el DMA quede pisado (¿armar
  todos los canales juntos en X=0?, ¿dejar el borde al playfield?).

## 7. Referencias

- Artículo: https://codetapper.com/amiga/sprite-tricks/risky-woods/
- WinUAE: `../WinUAE-DBG/custom.cpp:4018` (`sprstartstop`), `:10056` (`generate_sprites`),
  `:4099` (comentario del shifter); `drawing.cpp:2644` (`sprwrite`), `:2656` (coincidencia).
- Docs: `docs/reference/amiga/techniques/sprite-layer.md`,
  `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`,
  `docs/reference/emulators/winuae/sprite-dma.md`, `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.
