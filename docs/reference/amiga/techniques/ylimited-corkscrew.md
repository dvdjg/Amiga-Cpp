# Corkscrew Y-limited: registros, algoritmo de referencia y desviaciones del engine

Ficha de la técnica **scroll vertical “corkscrew” (anillo + split de vídeo)** usada por la familia
XLimited (`eng/field/xlimited*.hpp`). La implementación del engine **está rota** (ver §3): esta
ficha documenta los registros que intervienen, el algoritmo correcto (referencia
`ScrollingTricks`) y las desviaciones observadas, para arreglarla con criterio.

## 1. Registros que intervienen (AHRM cap. 3 «Playfield Hardware»)

| Registro | Papel en la técnica | Referencia |
|---|---|---|
| `DIWSTRT`/`DIWSTOP` | Ventana visible. `vstart` = primera línea visible; `vstop` la cierra. El anillo enseña `SCREENHEIGHT` líneas; el resto del bitmap es para el *wrap* y no debe verse. | AHRM cap. 3 «Setting the Display Window» |
| `DDFSTRT`/`DDFSTOP` | Ventana de **fetch** de bitplanes: fija cuántas palabras por línea lee cada plano. El algoritmo exige que el fetch cubra **más de `SCREENWIDTH`** (16/32/64 px extra según FETCH mode). Fórmulas: `DDFSTRT = DDFSTOP − 8·(words−1)` en baja resolución. | AHRM cap. 3 «Data Fetch», `:1686-1690` |
| `BPL1MOD` | **Modulo de los planos IMPARES (PF1 = 1,3,5)**. Se suma al puntero al llegar a `DDFSTOP`: `siguiente_línea = ptr_tras_fetch + MOD`. | AHRM cap. 3 «Modulo», `:1723-1729` |
| `BPL2MOD` | **Modulo de los planos PARES (PF2 = 2,4,6)**, independiente (permite distinta geometría por playfield en DPF). | ídem, `:1729` |
| `BPLxPT` (1-6) | Punteros de plano **dinámicos**: avanzan de 2 en 2 bytes durante el fetch; el split los re-apunta al inicio del bucle. En DPF: 1,3,5 = PF1; 2,4,6 = PF2. | ídem, `:1723` |
| `BPLCON0` | Nº de planos, DPF (`DUALPF`), resolución. | AHRM cap. 3 |
| `BPLCON1` | **Fine scroll horizontal** (nibble bajo PF1, alto PF2): la vía del *fine X* que la demo no usa. | AHRM cap. 3 |
| `BPLCON2` | Prioridad PF2/PF1 (`PF2PRI`), etc. | AHRM cap. 3 |
| `DMACON` | Habilita bitplane DMA, copper, sprites. | AHRM cap. 3 |
| `COP1LC`/`COP2LC` | Copperlist activa/inactiva (doble buffer). | AHRM cap. 5 |

## 2. Algoritmo de referencia (`ScrollingTricks/Docs`, XYLimited/YUnlimited2)

- **Geometría del bitmap**: ancho = `SCREENWIDTH + BLOCKWIDTH` (sobre-fetch de 1 bloque);
  alto = `SCREENHEIGHT + 2·BLOCKHEIGHT` (p. ej. 256 + 32 = **288** para una pantalla de 256).
  El área visible empieza en **`(mapposx+BLOCKWIDTH, mapposy+BLOCKHEIGHT)`**: los primeros 16 px
  de ancho y 16 de alto del mapa **no se ven** (son la zona de sobre-fetch/guarda).
- **Modulo-trick**: el fetch lee 16/32/64 px de más por línea según FETCH mode; el modulo se
  calcula para que el puntero caiga en la primera palabra de la línea siguiente **teniéndolo en
  cuenta**, y con scroll X el offset viejo→nuevo se mantiene constante (el modulo no cambia).
- **Split de vídeo**: en la línea `VIDEOSPLIT` se re-apuntan los planos al inicio del anillo; las
  Hi/Low words de esos punteros pueden variar con el scroll X y por ello se actualizan en
  `UpdateCopperlist`, no una sola vez en `InitCopperlist`.
- **Fillup-row/col**: la fila/columna de relleno se mueve al cruzar fronteras de bloque y ambos
  se corrigen mutuamente (blits de corrección con backup de la planeline que se pisa).

## 3. Desviaciones del motor detectadas y corregidas

> Estado: las cinco desviaciones de esta sección están **corregidas y verificadas** con copperlist viva + medidas (detalle en §5). Se conservan aquí como catálogo de los fallos que el algoritmo puede presentar y sus firmas medibles.

Config de la demo 110: `viewport_h = 208`, `display_height = 288`, `y_mode = Ring`,
`x_mode = Finite` («puntero directo; no repinta»), `dy = −2` (`src/main.cpp:242-251`).

Dump de registros en ejecución (MCP): `DIWSTRT=$2981`, `DIWSTOP=$F9C1`; `BPL1MOD=$0078` (120),
`BPL2MOD=$004E` (78); punteros PF1 `$020746/$02077C/$0207B2` (**54 B/plano**), PF2
`$02B300/$02B328/$02B350` (**40 B/plano**). `BPL2MOD` se calcula «para igualar fetch»
(`xlimited_composer.hpp:481-484`) y los punteros se emiten/parchan por plano en
`field_plane_address` (`xlimited_composer.hpp:430-434`).

Desviaciones:

1. **Geometría por playfield distinta con DDF compartido** (PF1 54 B/plano vs PF2 40 B/plano):
   el DMA lee para ambas capas el mismo número de palabras por línea, pero las filas de PF2 son
   más cortas → sobre-lectura dentro de la fila vecina. Firma visible: **planos/desplazamientos
   laterales** («cada plano a su lado») y basura.
2. **`viewport 208` + `display_height 288`**: la referencia exige pantalla 256 y bitmap 288
   (256+2·16). El engine mezcla una ventana de 208 con un anillo de 288 sin implementar el
   fillup-row/col ni el sobre-fetch del ancho → **filas de tiles duplicadas** (1,1,2,2,3,3…) y
   **banda basura inferior** (roja/rayas) en la ventana real.
3. **`X: Finite` con punteros directos**: sin `BPLCON1` (fine X) ni la corrección de sobre-fetch
   del ancho del bitmap → sin scroll fino y saltos de bloque.
4. El screenshot **interno** de 288 líneas recorta la zona donde se ve el fallo; la captura de
   **ventana** (arreglada en `mcp-winuae-emu`, `PrintWindow`) es la evidencia válida.

## 4. Verificación y trabajo restante

### 4.0 Mecanismos localizados en código (análisis estático, corregidos)

- **PF1 fila = 54 B**: con `x_mode = Finite` el bitmap toma el ancho del mundo + guarda:
  `bitmap_width = world_w + EXTRAWIDTH` (400 + 32 = 432 px → 54 B), y
  `BPL1MOD = row·planes − (viewport_w/8) − 2 = 54·3 − 42 = 120` ✓ (valor medido)
  — `xlimited_playfield.hpp:146,152,:234`.
- **PF2 (canvas)**: corregido a fila 42 B (fetch real) con guarda izquierda de 16 px
  (`CanvasPlayfield::Config::row_bytes`/`x_offset_px`, `xlimited_scene.hpp`); en vivo
  `BPL2MOD = 42·3 − 42 = 84` ✓.
- **Anillo**: `display_height = viewport_h + 2·tile_height` (= 240 para 208); la envoltura
  `split_line = display_height − display_offset` (`xlimited_composer.hpp`) es coherente con
  los punteros (verificado: Δpunteros 234 filas + WAIT en fila 6 = 240).
- **Camino caliente del composer dual**: parchea BPLxPT **y** la palabra del WAIT del split
  **y** BPLCON1 (antes quedaban obsoletos: costura rota y fine X congelado).
- **Eje Y**: `finite_y()` separado de `finite_x()`. Con `x=Finite`+`y=Ring`, el atajo de
  pintar la fila entera al cruzar (modo finito) producía un pico de ~1 campo cada cruce
  (medido: sec0 avg 38k con min 1k → 1.252 campos/frame) y el walk genérico del corkscrew
  (que asume anillo también en X) escribía la fila entrante con contenido mezclado. La forma
  correcta para X finita + Y anillo es la **fila en rodajas**: 1/16 de las columnas por
  sub-paso de 1 px en la fila fija del anillo (`block_videoposy`), sin walk plane-shifted:
  **49.87 fps / 1.003 campos** con X e Y activos, enrollado sin costuras (visión + flicker OK).

### 4.1 Trabajo restante

1. Verificar el fine X con mapa no uniforme (1 px/píxel) — el parche de BPLCON1 es nuevo.
2. Reactivar el FG (≈307k ciclos, DT-001 F4: pre-render + Blitter/BOB) hasta 50 fps.
3. Casos `linear_display` (visor 256) y viewports con HUD: revalidar con la copperlist.
4. Repetir en 202 (misma familia DPF) y en 107/201 (single: re-emiten cada frame; evaluar
   darles el mismo camino caliente con parche de WAIT/BPLCON1).

Deuda relacionada: `ROADMAP_DEUDA_TECNICA.md` DT-006.
