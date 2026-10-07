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

## 3. Desviaciones del engine (medidas con el banco aislado, 2026-10)

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

## 4. Plan de arreglo (incremental)

1. Fijar la geometría canónica de la referencia: bitmap `(SCREENWIDTH+16) × (SCREENHEIGHT+32)` y
   **una sola** `row_bytes` por plano para PF1 y PF2 (misma DDF); calcular `BPL1MOD/BPL2MOD` con
   el sobre-fetch.
2. Split/punteros: parchear Hi/Low por línea de split (ya se hace) verificando que **todos** los
   planos de un campo avanzan igual (`field_plane_address` con el mismo `planeaddy`).
3. `DIWSTRT/STOP` = pantalla visible (256), no 208; el área extra del bitmap es solo wrap.
4. Fine X con `BPLCON1` + corrección del fillup de ancho.
5. Banco de pruebas: 110 **solo-scroll** con filas como glifo (`g_map = y & 15`), captura paso a
   paso y comparación de dígitos por frame (la expectativa: fila superior = `mapposy/16`,
   sin duplicados ni filas saltadas).

Deuda relacionada: `ROADMAP_DEUDA_TECNICA.md` DT-006.
