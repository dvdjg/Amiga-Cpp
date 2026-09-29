# BOB interleaved de una sola pasada (cookie-cut `$CA` con máscara expandida)

**Qué problema resuelve.** Dibujar un BOB **enmascarado** sobre un bitmap **interleaved**
(planos entrelazados por scanline, como las escenas del engine) con **un solo blit**, en vez de uno
por plano. Es el truco clásico de *interleaved bitplane BOBs* (Use Case 4 de
`amiga-bootcamp/08_graphics/blitter_programming.md`; AHRM 3.ª cap. 6, Blitter).

**Idea.** En el BOB interleaved, cada fila de cada plano lleva su **propia copia de la máscara**
(máscara **expandida**, una copia por plano). Así un único blit de `altura = filas_objeto *
planos` recorre, con `DMOD` = bytes de una fila de plano, todas las filas de todos los planos, y el
mismo par A=máscara / B=imagen vale para cada uno.

**Layout del BOB** (32×16, 5 planos, con máscara). Por cada `(plano p, fila r)` hay un par
`[máscara 2 palabras][imagen 2 palabras]` (8 B); el blit avanza una "fila" cada 8 B:

```
offset = ((r * planos) + p) * 8
   +0..3 : máscara (2 palabras de 32 px)
   +4..7 : imagen  (2 palabras de 32 px)
```

**Registros** (blit de `32/16 = 2` palabras × `16*5 = 80` filas, con desplazamiento fino `sh`):

| Registro | Valor |
|---|---|
| `BLTCON0` | `$CA \| USEA\|USEB\|USEC\|USED \| (sh << 12)` |
| `BLTCON1` | `sh << 12` (BSH; el barrel shifter actúa sobre A y B) |
| `BLTAPT` | base del BOB + `0` (máscara) |
| `BLTBPT` | base del BOB + `4` (imagen) |
| `BLTAMOD` / `BLTBMOD` | `8 - words*2 = 4` |
| `BLTCPT` / `BLTDPT` | `bitmap + fila_destino*row_stride + (x>>3)` |
| `BLTCMOD` / `BLTDMOD` | `row_plano - words*2` (p. ej. `40 - 4 = 36`) |
| `BLTAFWM` / `BLTALWM` | `$FFFF` |
| `BLTSIZE` | `(80 << 6) \| 2` |

Minterm `$CA` = `D = (A·B) + (¬A·C)`, con `C = D =` destino: pinta la imagen donde la máscara está
a 1 y **conserva el fondo** donde está a 0. El puntero de destino puede caer en byte impar
(`x>>3`); el Blitter ignora el bit 0 y `sh` corrige el sub-píxel.

**Coste/límites.**

- Bus del Blitter: `palabras × (canales usados) = (2×80) × 4 = 640` slots por BOB (más el arranque
  y la contención con el display DMA). El clear de la banda es un blit D-only aparte.
- `DMOD` y `AMOD` deben **coincidir con el layout real** del bitmap y del BOB; un módulo de más
  mezcla filas de planos distintos.
- Requiere máscara expandida (una copia por plano), lo que **duplica** el tamaño del BOB respecto a
  la máscara única (5120 B en vez de 3 planes + 1 máscara).

**En el engine.** `Bob`/`bob_draw` (`eng/graphics/bob.hpp`) **no** cubre cookie-cut interleaved
(devuelve `false`: la máscara expandida no la produce el ejecutor). Para este caso se describe el
blit con un `BlitJob` (`MaskedBobCookieCut`, `BlitJob::interleaved = true`, `height` ya incluye los
planos) y se envía con `AmigaBackend::blitter_submit`. El helper
`eng::graphics::make_interleaved_masked_bob(job, src, dest, w, h, planes, dest_row_bytes, shift)`
rellena ese `BlitJob` a partir del layout `[máscara][imagen]` (evita al llamador conocer los
campos de módulo/altura). La demo **213_bartman_abyss** lo usa con su `bob.bpl` (generado con
`kingcon ... -Interleaved -Format=5 -Mask`) y un solo `blitter_submit` por BOB. Ver
`engine/graphics/blit_job.hpp` y `docs/reference/amiga/techniques/blitter-memcpy.md` para el
descriptor.

**Generar el asset.** El conversor `kingcon image.png out -Interleaved -Format=N [-Mask]` produce
este layout (interleaved, `N` planos, con máscara expandida si `-Mask`). El original es la demo de
Bartman/vscode-amiga-debug (`BartmanBasic/gfx/convert.cmd`).
