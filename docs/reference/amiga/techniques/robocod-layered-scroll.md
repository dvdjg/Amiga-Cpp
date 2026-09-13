# RoboCod — plano de fondo con scroll propio (parallax) en Amiga OCS

Técnica del *James Pond 2: Codename RoboCod*: **un solo playfield de 5 bitplanes**. Los
**4 primeros planos (16 colores) son el FG de juego** (plataformas, etc.); el **5.º plano es el
FONDO**, que **scrollea a distinta velocidad** y se pinta con el **Blitter** con su propio
offset. No usa dual playfield. El plano de fondo **"duplica la paleta"** para que, al moverse
bajo el FG, no se note un cambio de color en el FG.

## 1. Idea y por qué funciona

```text
   5 bitplanes -> 32 indices de paleta, PERO mapeados a 16 colores:

   indice = (FG[3:0])            + 16*BG        color index 0..31
            \____ 4 planos FG ___/   \_ 1 plano fondo _/
   palette[c] == palette[c+16] para c=1..15   -> el plano de fondo NO tinta el FG
   palette[0] = negro (fondo off) ; palette[16] = color del patron (fondo on)

   ┌───────────────────────────────────────────────┐
   │ FG (planos 0..3, 16 colores, poca cobertura)   │  scroll RÁPIDO (juego)
   │   transparente donde no hay plataformas         │
   ├───────────────────────────────────────────────┤
   │ BG (plano 4): patron/motivo, áreas amplias      │  scroll LENTO (p. ej. 1/2)
   └───────────────────────────────────────────────┘
```

- El **movimiento relativo** entre el FG (planos 0..3) y el plano de fondo (4) crea profundidad.
- El FG deja ver por **transparencia** (color 0) el patrón del plano de fondo.
- La **duplicación de paleta** evita el "salto de color" del plano de fondo bajo el FG.

## 2. Implementación en el engine ("soft DPF")

`XLimitedPlayfield` con `planes = 5` y `parallax_plane = 4` (el ÚLTIMO), `parallax_div = N`:

- **FG (planos 0..3)**: el tilemap normal. Su blit (`draw_block_job`) es un `TileBlockCopy`
  **interleaved** que cubre los 5 planos y por tanto **escribe 0 en el plano de fondo**. No se
  puede saltar el 5.º plano en un blit interleaved (§3).
- **BG (plano 4)**: un patrón 1bpp en Chip RAM (fuente del Blitter). En cada frame, **después**
  del blit del FG, se **re-copia por Blitter** la ventana del patrón al plano 4 con
  `XLimitedPlayfield::make_bg_plane_copy_job(pattern, pattern_row_bytes, src_x_pixels, src_y)`:
  una copia A→D de `display_height` filas × el ancho de la fila del bitmap.
- El movimiento del fondo lo da el **contenido** (qué ventana se copia), **no un puntero
  propio**: el display lee el plano 4 con el `BPLxPT` **normal** (compartido con el FG), así el
  split del corkscrew sigue cuadrando.
- **Paleta de 32** (5 planos): `palette[c] == palette[c+16]` para c=1..15; `palette[0]` negro,
  `palette[16]` color del patrón.

Ventajas frente a un blit por-planos del FG: una sola pasada interleaved para el tilemap y una
copia plana del fondo (barata en Blitter), sin tocar la emisión del tilemap.

## 3. Fondo FIJO/parallax, sub-píxel y **compensación del Copper split**

El plano de fondo comparte el scroll hardware del FG (mismos `BPLxPT`/`BPLCON1`), así que el
movimiento del fondo lo da el CONTENIDO. La posición aparente en pantalla es
`patron_visible(screen_x) = src + scroll_x + screen_x`; para un desplazamiento deseado `bg_x`
(posición de mundo del fondo) hay que hacer:

```text
src = bg_x - scroll_x            (fórmula general; desired_bg = bg_x)
```

- **Fondo FIJO** (`bg_x = 0`): `src = -scroll_x` → `eng::field::fixed_bg_offset_px`.
- **Fondo a 1/div** (`bg_x = scroll_x/div`): `src = -scroll_x*(div-1)/div` →
  `eng::field::parallax_pattern_offset_px`.

`src` es entero en píxeles pero no siempre múltiplo de 16; la parte no alineada a word la
resuelve el **barrel shifter del Blitter** (AHRM 6, "Copying Arbitrary Regions"):

- Copia **A→D** con minterm `$F0`, canal A como fuente.
- En modo **ascendente** el Blitter desplaza a la derecha: `destino[d] = patrón[q + d - S]`.
  Para que `destino[0] == src` se apunta A a la word `q = src + S` con `S = (-src) & 15`.
- El registro de desplazamiento arrastra bits entre filas. Para que no aparezcan datos de la
  fila anterior, se enmascara la **última word de cada fila** con `BLTALWM = 0xFFFF << S`: los
  bits desplazados hacia fuera quedan a cero y forman una **guarda de hasta 15 px** al principio
  del bitmap. El llamador debe mantener esa guarda fuera de la ventana visible (p. ej. cámara
  `X >= 16` en la demo 112).

> Nota de hardware: el modo descendente del Blitter se activa con `BLTCON1` bit 1
> (`BLITREVERSE = $0002`), no con `$0400` (AHRM 6, "Descending Mode"). El backend usaba
> `$0400`, un bit no usado de `BLTCON1`; era inocuo porque ningún driver activaba `descending`.
> Se corrigió al implementar la copia con shift.

### 3.1 Compensación del Copper split (obligatoria)

El corkscrew reduce la altura del buffer con un **split vertical de Copper**: cuando
`display_offset + viewport > display_height`, el Copper espera en `split_line =
display_height - display_offset` y recarga los punteros al inicio del buffer. La ventana visible
son entonces **DOS trozos** del bitmap (arriba `[d, display_height)`, abajo
`[0, viewport-split)`), donde `d = display_offset`.

Un solo blit continuo dejaría el fondo **discontinuo** en el corte. Hay que emitir **DOS rects**
con el mismo `src_x` y las filas del patrón **contiguas**:

| trozo | filas de bitmap | filas de patrón | filas de pantalla |
|---|---|---|---|
| superior | `[d, d+split)` | `[bg_y, bg_y+split)` | `[0, split)` |
| inferior | `[0, viewport-split)` | `[bg_y+split, bg_y+viewport)` | `[split, viewport)` |

Así el fondo queda continuo (las dos mitades muestrean filas consecutivas) y, con `bg_y`
constante, **fijo** aunque el FG haga wrap. API:
`XLimitedPlayfield::make_bg_plane_copy_rect_job(pattern, row_bytes, src_x, src_y, dest_row, rows, dest_byte_off, words)`.
Cuando no hay split (`split >= viewport`) basta un solo rect en `[d, d+viewport)`.

### 3.2 LÍMITE de altura (Amiga 500 / OCS)

El `WAIT` del Copper solo compara **8 bits** de línea (0..255); hay 312 líneas PAL. La línea del
split móvil es `DIWSTRT_y + split_line = 41 + (display_height - display_offset)`. Para que nunca
cruce 255 con el split activo, el **campo visible debe ser ≤ 214**. El anillo `display_height`
se dimensiona para el viewport TOTAL + 2 bloques de staging. Canónico: **campo 208** (13 filas
de tile) con **anillo 288** (invariante §7 de `201_ehb_map`). **NO usar 256**: el split se sale
del rango de 8 bits y aparece el wrap adelantado (síntoma: banda incorrecta en el pie). Si se
quiere ocupar las 256 líneas, añadir un HUD (201: campo 208 + HUD 48 sobre un viewport total de
256 y anillo 288). El compositor **falla rápido** si el campo con split supera 214.

### 3.3 Tearing: el blit de filas VISIBLES va en el blanking

Un bitmap **único** no admite reescribir filas visibles mientras el haz las está mostrando: la
zona ya reescrita se ve desplazada por el delta de cámara respecto a la aún no reescrita
(tearing de ~2 px). El FG no lo sufre porque escribe solo la zona de *staging*; el fondo FIJO
sí, porque debe reescribir las filas visibles cada frame (su contenido compensa la cámara).

Solución (sin doble buffer, siguiendo la recomendación de reorganizar la programación temporal):
**ejecutar el blit de fondo FIJO dentro del blanking vertical** (fin de la ventana visible →
inicio de la siguiente), de forma que el haz nunca lea una fila a medio escribir:

1. En `update`: scroll + blits del FG (staging) + `compose()` de la copperlist.
2. Esperar al inicio del blanking: `while (backend.current_raster_line() != kBlankStart);`
   con `kBlankStart = DIWSTRT_y + viewport_h = 41 + 208 = 249`.
3. Ejecutar el plan de fondo (solo las filas visibles, dos rects del split) + `install` de la
   copperlist.

Además se **reduce el ancho** del blit a la ventana que el display puede leer más una word de
guarda: `bg_window_for(camx, period, fetch_bytes)` copia
`[planeaddx-2, planeaddx+fetch_bytes)` (21 words en vez de 25) y devuelve el `src_x` que deja la
imagen fija; la guarda del barrel shifter (≤15 px) cae justo antes de la cámara. Con esto el
blit visible (~103 líneas de raster) cabe en el blanking (~105 líneas).

> Aviso de margen: el blit visible ocupa casi todo el blanking; si se necesita holgura, reducir
> el campo visible (p. ej. 192) para ampliar el blanco, o usar doble buffer **solo del plano de
> fondo**. Medición en 112: el reordenamiento bajó las diferencias de fondo entre frames de
> ~185k a ~13-25k píxeles (el residuo es en su mayoría capturas hechas durante el blanking, que
> no se ven en pantalla).

### 3.4 Verificación (112, release/debug)

- **Compensación EXACTA**: con la cámara congelada (`K_DIAG_SKIP_SCROLL`) y `K_INIT_CAMX` =
  16, 17, 32 y 80, el borde de banda del fondo es idéntico (43,38,33,28,8) → no hay off-by-one
  ni dependencia con `d`/`camx`. El residuo observado no es de cálculo.
- **El blit cabe holgadamente en el blanking**: medido con `K_DIAG_BG` (contador de ciclos
  alrededor del `execute_frame_plan` del fondo), **25–31k ciclos ≈ 55–68 líneas** de raster;
  el blanco disponible es ~105 líneas. No hay overrun hacia la zona visible.
- Por tanto, el residuo de 1–2 px que aparece en **capturas** es la instantánea leyendo el
  bitmap a medio actualizar dentro del blanking (invisible en el haz). Para una medida
  concluyente hace falta captura sincronizada al haz o comprobar el bitmap tras el blit.
- Aun con 208 filas el blit cabe (76 líneas para 256 filas), pero el **WAIT de 8 bits** impide
  un split móvil con campo >214. Si se quiere 256 visibles con fondo estable y bitmap único,
  la alternativa del engine es `linear_display` (espejo, sin split), que cabe en el blanking;
  el coste es duplicar los blits de Y (amortizados).
- **Doble buffer del plano de fondo (soft DPF, IMPLEMENTADO)**: con fondo FIJO/scroll propio el
  contenido se reescribe cada frame y no hay conmutación atómica → jitter de 1 px. Se resuelve
  doble-bufferizando SOLO el plano 4 (`m_bg_bitmap[2]` + `bg_flip()`): el blit escribe el buffer
  trasero y el compositor lee el delantero (`PlayfieldHardwareView::bg_plane_base`). Verificado:
  el borde pasa de oscilar 43/44 a constante. Análisis y diagnóstico completo:
  `docs/debugging/112_BG_FLICKER.md`. Con esto el plano de fondo es una capa con su **propia
  cámara** (soft DPF con scroll independiente); el siguiente paso es un tilemap XYLimited completo
  en ese plano.

## 4. Patrón de fondo

- 1 bit por píxel en Chip RAM, generado una vez (procedimental o desde un tileset). Para que la
  ventana copiada no invada la fila siguiente al desplazarse en X, el patrón se genera con **dos
  periodos** de ancho (margen ≥ ancho de ventana + 16 px).
- Un motivo diagonal sin costura usa `(x + y) & 63 < 32`; rombos, cruces y puntos son más
  variantes de `glyph`.

## 5. Raster colors (Copper)

El plano de fondo usa 2 estados (patrón on/off). Para dar riqueza, se cambia el **color del
patrón** (`COLORxx` del registro del plano 4 → índice 16, `COLOR16`) **por línea** con
`WAIT`+`MOVE` del Copper: cada banda de raster usa un **color pastel distinto**, contrastando
con el "off" (negro). Ambas rutas de composición (single y dual) comparten
`eng::field::RasterColorZone { line, reg, color }` (orden ascendente; se construye con
`raster_color(line, color_index, color)`), configurable desde `XlimitedSceneConfig.color_zones`
o `dpf.color_zones`.

Requisito práctico: usar `linear_display` (sin split de Copper) para que el orden del raster no
se rompa. Limitación OCS: el comparador de `WAIT` es de 8 bits, así que conviene mantener las
líneas ≤ 255. La demo 112 usa 7 zonas de `COLOR16` (de cian claro a azul).

## 6. Alternativa: DPF de 2 capas

Si se quiere separar en **dos bitmaps** (no un solo playfield), se puede hacer con **DPF 3+3**:
FG = PF1 (delante), BG = PF2 con su propia cámara/velocidad. Cada capa tiene su bitmap (el
blit del FG no pisa el BG por construcción). Es más simple de emitir pero usa **6 planos** y
**más Chip RAM**, y ya no es "5 planos de 1 juego". El RoboCod fiel es la §2 (single 5 planos).

## 7. Parámetros y límites (A500/OCS)

- Single 5 planos: **32 colores de paleta** (mapeados a 16 por la duplicación), sin DPF.
- Coste por frame: **una copia de Blitter de la ventana visible** (`viewport × bytes_per_row`);
  con split son **dos rects** que suman la misma superficie. (Antes se copiaba el anillo completo
  `display_height × bytes_per_row`; basta con lo visible porque la copia se rehace cada frame.)
- **Guarda de hasta 15 px** al principio del bitmap por el shift-in enmascarado (§3); la cámara
  X debe mantenerse ≥ 16 (o reservar una word de guarda).
- El fondo puede ser **fijo** (las dos mitades compensan el split, §3.1) o con parallax; el
  anclaje vertical se da con `src_y` (`bg_y`). Un parallax vertical propio exige `bg_y` variable.
- `BPLCON2` no interviene (no hay DPF).
- Chip RAM: bitmap `bitmap_bytes_per_row * bitmap_height * planes` (1 bit más que el FG-only).

### Rendimiento (medido en la demo 112, A500 / WinUAE-DBG)

- La copia de fondo de la **ventana visible** (25×208 palabras; con split, dos rects) cuesta
  **~54k ciclos** de CPU (espera de Blitter) con la pantalla activa: ~10 ciclos/palabra por
  contención de bus display+Blitter, muy por encima de los ~2-4 ciclos/palabra teóricos. (Copiar
  el anillo completo eran ~77k.)
- El `update` total (scroll + composición + copia) queda en **~181k ciclos ≈ 1.28 VBlanks**; como
  cruza el límite de 1 VBlank, el `wait_vblank` lo cuantiza a **2 VBlanks (≈25 fps)**. La
  sincronía de VBlank **es correcta** (el periodo medido es exactamente 2×141875).
- Conclusión: **un fondo re-copiado por Blitter cada frame no permite 50 fps** en esta escena
  (haría falta que la copia costase < ~15k ciclos, imposible: el mínimo DMA es ~14k). Para 50 fps
  reales con parallax hay que **no** re-copiar el fondo cada frame: usar **DPF** (cada campo con
  su propio `BPLCON1` fino) o un fondo estático con scroll de puntero. Ojo: el parallax por
  **puntero por plano** en un solo playfield tiene un diente de sierra de hasta 16 px, porque el
  `BPLCON1` fino es compartido y solo el coarse (`BPLxPT`) es independiente.
- El «41.79 fps» de mediciones previas era la tasa **free-running** (`CPU_HZ / ciclos_update`),
  no la tasa real con sincronía de VBlank.

## 8. Demo de referencia

`demos/amiga/112_xlimited_robocod`: single playfield de **5 planos** (4 FG plataformas +
1 BG con bandas diagonales **FIJO** en pantalla), X `Finite` + corkscrew Y con **Copper split**,
paleta 32→16 duplicada, copia de fondo con shift sub-píxel, guarda X y **compensación del split
en dos rects** (§3.1). Campo visible **208** (límite §3.2). Verificación visual:
`analyze-sequence.sh` + `ollama-desc.mjs` (ver `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).
