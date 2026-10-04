# Demo 208 — fondo de sprites estilo *Risky Woods*: diagnóstico y reinicio

**Estado: bloqueado (abierto).** El muro de ladrillos por sprites se dibuja, pero **no
cubre de lado a lado** (quedan bloques con huecos) y el scroll/objetos no acaban de
cerrarse. El motor de la demo se ha reconstruido varias veces sin converger; este
documento fija los **hechos de hardware verificados** y el **plan de reimplementación**
para no volver a derivarlos a ciegas.

## 1. Objetivo

Una capa de fondo con **6 canales de sprite** (16 px cada uno, período 96 px) que cubra
**320 px de lado a lado**, con **scroll horizontal de 1 px por frame** y **2 canales
libres** (0/1) para objetos. Es el reparto híbrido de `docs/engine/architecture/SPRITE_BANDS.md`.

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
   de la banda.
4. **Terminador `0,0`** → `VSTART=0` → rearme en la línea 0 → canal desfasado (fantasma).
5. **Re-emitir la copperlist cada frame** para el scroll: funciona pero es lo contrario
   al diseño (el original es ~0 de CPU).

## 5. Plan de reimplementación (por etapas verificables)

Cada etapa con su evidencia; **no se avanza sin cerrar la anterior** (protocolo de
etapas). Reutiliza el ledger/allocator (`graphics/sprite_band.hpp`, ya en verde HOST-416)
y el driver `effects::RiskyWoodsLayer` (HOST-417).

- **E0 — Un solo canal, banda pequeña, estático.** 1 canal, 16 px, sin scroll: el canal
  dibuja su columna y **se sostiene** durante la banda. Verificación: captura
  (columna de color a toda la altura de la banda). Aquí se fija la estructura DMA (cabecera
  + DATA + **final sin `VSTART=0`**) y el armado.
- **E1 — 6 canales, patrón de 96 px, sin repetir (1 período).** Los 6 canales contiguos
  cubren 96 px. Verificación: captura (96 px de patrón). Fija el orden de canales y la
  colocación inicial (DMA o Copper).
- **E2 — Repetición a 320 px con UN `WAIT` + ráfaga** (patrón del artículo). Ajustar la
  X inicial y el orden de la ráfaga para que **cada MOVE caiga por detrás del haz** y las
  reutilizaciones de canal disten ≥ período. Verificación: captura **sin huecos** (análisis
  de píxeles determinista: 0 huecos internos en la banda).
- **E3 — Scroll por cambio de punteros** (8 sets pre-shifteados + bit H0), copperlist
  **estática**. Verificación: secuencia de frames consecutivos; el patrón se mueve 1 px por
  frame y el coste por frame son ~20 words.
- **E4 — Objetos (2 canales libres)** animados sobre la banda. Verificación: secuencia +
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
  `docs/reference/emulators/winuae/sprite-dma.md`, `docs/engine/architecture/SPRITE_BANDS.md`.
