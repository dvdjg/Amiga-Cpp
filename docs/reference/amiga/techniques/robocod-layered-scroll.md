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

## 3. Scroll del fondo a `1/div` y **compensación sub-píxel**

El display ya suma `+camx` al plano de fondo (lo lee desde `camx`), así que para que el fondo
avance a `1/div` de la velocidad del FG hay que desfasar el CONTENIDO del patrón:

```text
patron_visible(screen_x) = src + camx + screen_x
queremos                 = camx/div + screen_x
=> src = camx/div - camx = -camx*(div-1)/div     (helper parallax_pattern_offset_px)
```

`src` es entero en píxeles, pero no siempre múltiplo de 16. La parte no alineada a word la
resuelve el **barrel shifter del Blitter** (solo actúa sobre A/B; AHRM 6, "Copying Arbitrary
Regions"):

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
- Coste por frame: **una copia de Blitter** de `display_height` filas × `bytes_per_row`. Es
  proporcional a la superficie del fondo: si el bitmap es más ancho que la ventana, conviene
  copiar solo la ventana visible (o limitar el ancho del bitmap).
- **Guarda de hasta 15 px** al principio del bitmap por el shift-in enmascarado (§3); la cámara
  X debe mantenerse ≥ 16 (o reservar una word de guarda).
- El scroll vertical del fondo comparte el del FG (la copia parte de una fila fija `src_y`);
  un parallax vertical propio exigiría una fuente distinta por fila.
- `BPLCON2` no interviene (no hay DPF).
- Chip RAM: bitmap `bitmap_bytes_per_row * bitmap_height * planes` (1 bit más que el FG-only).

### Rendimiento (medido en la demo 112, A500 / WinUAE-DBG)

- La copia de fondo (`display_height × bytes_per_row`, 25×288 palabras) cuesta **~77k ciclos**
  de CPU (espera de Blitter) con la pantalla activa: ~10 ciclos/palabra por contención de bus
  display+Blitter, muy por encima de los ~2-4 ciclos/palabra teóricos.
- El bucle de la demo (scroll + composición + copia) queda en **~284k ciclos ≈ 2 VBlanks**
  (≈25 fps): el `update` (212k ≈ 1.5 VBlanks) cruza el límite de 1 VBlank y el `wait_vblank`
  lo cuantiza a 2. La sincronía de VBlank **es correcta** (el periodo medido es exactamente
  2×141875).
- Conclusión: **un fondo re-copiado por Blitter cada frame no permite 50 fps** en esta escena
  (haría falta que la copia costase < ~15k ciclos). Para 50 fps reales con parallax hay que
  **no** re-copiar el fondo cada frame: usar **DPF** (cada campo tiene su propio `BPLCON1` fino)
  o un fondo estático con scroll de puntero. Ojo: el parallax por **puntero por plano** en un
  solo playfield tiene un diente de sierra de hasta 16 px, porque el `BPLCON1` fino es
  compartido y solo el coarse (`BPLxPT`) es independiente.
- El «41.79 fps» de mediciones previas era la tasa **free-running** (`CPU_HZ / ciclos_update`),
  no la tasa real con sincronía de VBlank.

## 8. Demo de referencia

`demos/amiga/112_xlimited_robocod`: single playfield de **5 planos** (4 FG plataformas +
1 BG con bandas diagonales a 1/2 de velocidad), X `Finite` + corkscrew Y, paleta 32→16
duplicada, copia de fondo con shift sub-píxel y guarda X. Verificación visual:
`analyze-sequence.sh` + `ollama-desc.mjs` (ver `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).
