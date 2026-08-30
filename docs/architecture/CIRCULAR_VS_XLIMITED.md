# Circular vs XLimited — comparativa canónica

> Documento de referencia (2026-08). Compara el modelo circular anterior
> (`TileFieldController` con `surface_origin`, margen `2*BLOCK`, recentrado y
> Copper con *split*) frente al modelo **XLimited interleaved** de Georg Steger
> (`xlimited.hpp`: *bitmap* 352/384, altura `+ (width/22/planes)+1+3`,
> *plane-shifted*, `saveword`, sin *split*). Cita fuentes primarias y fija
> por qué el circular no es canónico para scroll X infinito.

## Fuentes primarias

| Fuente | Qué fija |
|---|---|
| `amiga-stuff/scrolling_tricks/xlimited.c:45-84` — `EXTRAWIDTH`, `BITMAPWIDTH`, `BITMAPBYTESPERROW`, `fetchinfo[]` | Geometría 352/384 y modos de *fetch* (normal 16 px → `DDFSTRT=$30/$D0`, 2; `BPL32/BPAGEM` → 4/16/32; `BPL32+BPAGEM` → 8/48/64) |
| `amiga-stuff/scrolling_tricks/xlimited.c:115` / `weiju/xlimited.c:68` / `xylimited.c:144` | `bitmapheight = BITMAPHEIGHT + (map_width / BITMAPBLOCKSPERROW / planes) + 1 + 3` |
| `amiga-stuff/scrolling_tricks/xlimited.c:201-227` — `DrawBlock` | Contrato Blitter: `x=(x/8)&0xFFFE`, `y*=BITMAPBYTESPERROW`, `bltapt=blocksbuffer+map`, `bltdpt=frontbuffer+y+x`, `bltsize=BLOCKPLANELINES*64+1` |
| `amiga-stuff/scrolling_tricks/xlimited.c:242-288` — `ScrollLeft/Right` | Columna entrante, `x` plane-shifted (`+BITMAPWIDTH`), `y=mapy*BLOCKPLANELINES`, `saveword/savewordpointer` |
| `amiga-stuff/scrolling_tricks/xlimited.c:297-323` — `UpdateCopperlist` | `xpos=videoposx+I-1`, `planeaddx=(xpos/I)*(I/8)`, `fine=(I-1)-(xpos&(I-1))`, `BPLCON1=(fine&15)*0x11|0x4400|0x8800`, `BPLxPT=Planes[i]+planeaddx` |
| `ScrollingTricks/Docs/xlimited-uk.html` — §§ *overallbitmapheight*, *plane-shifted*, *saveword* | Explicación del *wrap* vertical y de la guarda de 1 *word* (figs. `xlimited.gif`…`xlimited7.gif`) |
| `ScrollingTricks/Docs/xlimited64-uk.html` | Variante 384 px (alineación a 64 px, `EXTRAWIDTH=64`) |
| `Amiga Hardware Reference Manual` (HRM) §§ *BPLCON1/BPLCON0/BPLxPT/BPLMOD/DDFSTRT* + *Amiga Chipset Explained* (ACE) | `BPLCON1=(16-fine)&15`, `fetch=(scroll_x-1)&~15` continuo; `BPLMOD=row_bytes*planes - fetch_bytes - offset` |
| `engine/include/eng/field/xlimited.hpp:14-199` | Implementación fiel del algoritmo en el *engine* (geometría, oblig. interleaved, sin *split*, `saveword`) |
| `engine/include/eng/field/tile_field.hpp:115-419` | Modelo circular: `surface_origin`, `window_x/y`, `recenter_axis`, bandas X/Y, `FieldHardwareView::split_*`, guarda horizontal `+2*BLOCK` |
| `docs/architecture/AMIGA_8WAY_SCROLLING.md` §§3-8 | Síntesis canónica del circular (fórmulas, `BPLMOD`, `BPLCON1`, Copper *split*) |

Los rangos de línea citados corresponden a `amiga-stuff/scrolling_tricks` tal como está
vendorizado en `C:\Users\dvdjg\Documents\programa\AI\Amiga\amiga-stuff`. La lectura
recomendada es `xlimited.c` → `xlimited-uk.html` → `xylimited.c` → `xlimited.hpp`.

---

## Resumen ejecutivo

| Dimensión | Circular (`TileFieldController`) | XLimited (`XlimitedField`) |
|---|---|---|
| **Objetivo** | 8-way genérico (X e Y) con ventana recentrable | X infinito (Steger *Scroller_XLimited*); Y separado si hace falta |
| **Geometría** | `VW+2*BW` × `(VH+2*BH)/BH*BH+1` + guarda lineal; recentrado lógico | `352` (22 bloques) ó `384` (24 bloques) × `256+(map_width/blocks_per_row/planes)+1+3` interleaved |
| **Chip RAM** | `row_bytes*surface_h*planes` (separate); crece con viewport+margen, no con el mapa | `row_bytes*bitmap_height*planes + bitmapoffset`; crece `+1` planelínea cada `blocks_per_row*planes` bloques de mapa (~88 bloques para 352/4) |
| **Blitter** | Ráfaga cada 16 px: `0` jobs 15/16 frames, `VISIBLE_Y+2` jobs (~16-18) en el cruce; Y análogo | `1` job por píxel de scroll (`1` bloque, `BLOCKPLANELINES*64+words`); uniforme, sin ráfaga |
| **Copper** | 1 WAIT de *split* vertical (`split_line≈VH/2`) + 2×`BPLxPT` por plano | Sin *split*; una sola lista: `BPLCON1`+`BPLxPT` lineales |
| **Direccionamiento** | Separate: `base + n*plane_bytes + y*row_bytes + x`; `BPLMOD=row_bytes-fetch_bytes` | Interleaved: `frontbuffer + y*BITMAPBYTESPERROW + x`; `y` = planelínea (`block*BLOCKPLANELINES`); `BPLMOD=row_bytes*planes-fetch_bytes-offset` |
| **Oblig. OCS** | *Fetch* $30 (42 B) opcional; funciona con cualquier alineación múltiplo de 16 | **Interleaved obligatorio** (`BMF_INTERLEAVED`; `IS_BITMAP_INTERLEAVED` en `xlimited.c:129`); `x` *word-aligned* `&0xFFFE` |
| **Canonicidad** | Abstracción didáctica del *engine*, no publicada por Steger para X | **Canónico para X**: código original de Steger, doc `xlimited-uk.html`, misma fórmula en `weiju` y `xylimited` |

---

## 1. Geometría

### 1.1 Fórmulas

```text
Circular (tile_field.hpp:122-128; AMIGA_8WAY_SCROLLING.md §3):

  surface_w = scroll_x ? VW + margin_blocks*BW : VW
  surface_h = scroll_y ? floor((VH + margin_blocks*BH)/BH)*BH + 1 : VH
  margin_blocks ∈ {2,3}  (2 por defecto → 1 bloque por lado)
  guard_line  = surface_h - 1  (fuera de retícula, a 0)

XLimited (xlimited.c:45-73,115; xlimited.hpp §1):

  BITMAPWIDTH        = SCREENWIDTH + EXTRAWIDTH          // 320+32=352 ó 320+64=384
  BITMAPBYTESPERROW  = BITMAPWIDTH/8                      // 44 ó 48
  BITMAPBLOCKSPERROW = BITMAPWIDTH / BLOCKWIDTH           // 22 ó 24
  BITMAPBLOCKSPERCOL = BITMAPHEIGHT / BLOCKHEIGHT         // 16
  BLOCKPLANELINES    = BLOCKHEIGHT * planes               // 16*4=64
  BITMAPPLANELINES   = BITMAPHEIGHT * planes

  bitmapheight = BITMAPHEIGHT
               + (map_width / BITMAPBLOCKSPERROW / planes)  // desenrollado horizontal en vertical
               + 1                                          // planelínea de guarda del Blitter
               + 3                                          // margen fetch ancho (DDFSTRT=$30)
  // Para XY: 384×(256+32) en xylimited.c:24-38; TWOBLOCKS=BITMAPBLOCKSPERROW-NUMSTEPS_Y
```

Desglose del `+1+3` (xlimited-uk.html § *overallbitmapheight* + xlimited.c:68):

* `+1`: planelínea de guarda; el Denise hace *fetch* especulativo de la siguiente
  planelínea al envolver el borde derecho; sin ella lee fuera de `plane_bytes`.
* `+3`: reserva para el *fetch* ancho (42 B con `DDFSTRT=$30`, 48 B en 4×). Cubre
  diagonal y margen de composición; 2 sería mínimo, 3 es el valor publicado.

### 1.2 Tabla comparativa de geometría

| Parámetro | Circular | XLimited |
|---|---|---|
| Ancho físico | `VW + margin*BW` (ej. 320+32=352 con margen 2) | `352` (22 bloques) ó `384` (24 bloques), fijo por `EXTRAWIDTH` |
| Alto físico | `floor((VH+margin*BH)/BH)*BH+1` (ej. 256+32→288+1=289) | `256 + map_width/22/planes +4` (ej. 256+11+4=271 sin alinear; 268 en xlimited.c con `map_width=1000/22/4`) |
| Dependencia del mapa | Ninguna (la altura no crece con el mapa) | Sí: cada `22*4=88` bloques de anchura → `+1` planelínea (~1,4 KB en 4 planos) |
| Guarda | Última scanline completa (`surface_h-1`) a 0 | Última **planelínea** (`y+ BLOCKPLANELINES-1`) solapada; se salva con `saveword` |
| Wrap | Lógico: `surface_origin` + máscara `VW+2*BW`; Copper *split* en Y | Físico: el *fetch* lineal envuelve a la siguiente planelínea; el Blitter escribe plane-shifted (`+BITMAPWIDTH`) |
| Múltiplos | `VW% BW==0`, `VH% BH==0`, `BW%16==0` | `BITMAPWIDTH%16==0` y `BITMAPWIDTH% I==0` (`I=16/32/64` según `fetchmode`) |

### 1.3 Esquemas ASCII

**Circular — buffer anular recentrable (VW=320, BW=16, margen 2):**

```text
              BW=16     VW=320          BW=16
            ┌──────┬──────────────────┬──────┐ ─┬─
            │      │                  │      │  │ VH=256
            │ MARGEN│ VIEWPORT VISIBLE │MARGEN│ ─┴─
            │ IZQ  │  window_x/y      │ DER  │ ─┬─ BH=16
            ├──────┼──────────────────┼──────┤  │ banda inf.
            │      BANDA INFERIOR + guard line  │ ─┴─
            └──────┴──────────────────┴──────┘
◄─ surface_w=VW+2*BW=352 ─────────────►
◄─ row_bytes=surface_w/8=44 ──────────►

surface_origin (world→physical) se desplaza al recentrar;
window_x/y se resetea a left_margin; Copper hace split en VH/2
si scroll_y (tile_field.hpp:232-236, AMIGA_8WAY_SCROLLING.md §7.4).
```

**XLimited — bitmap interleaved 352×268 (4 planos, 44 B por planelínea):**

```text
Dirección →  0            44           88          132         176 ...
          ┌──────────┬──────────┬──────────┬──────────┐
 planelínea 0 │ L0 P0 44B│ L0 P1 44B│ L0 P2 44B│ L0 P3 44B│  ─┬─ scanline 0 (4 planelíneas)
          ├──────────┼──────────┼──────────┼──────────┤   │
 planelínea 4 │ L1 P0 44B│ L1 P1 44B│ L1 P2 44B│ L1 P3 44B│  ─┤ scanline 1
          └──────────┴──────────┴──────────┴──────────┘   │
                     ...                                  ▼
          ┌──────────────────────────────────────┐
          │  altura extra: map_width/22/planes   │  ej. 268-256=12 planelíneas
          ├──────────────────────────────────────┤  +1 guarda  ─┬─ 4 planelíneas
          │  margen fetch (+3)                   │  total +4     ─┴─
          └──────────────────────────────────────┘

  Visible inicial (xlimited.gif): rectángulo rojo = actual-bitmap-area (352),
  amarillo = visible (320). Al scrollear +16 px, el borde derecho envuelve
  a la siguiente planelínea (xlimited3/4.gif): offset 42 → 44 ya es P1 de la
  siguiente línea, no P0 de la misma. Por eso el bloque entrante se blitea
  con +BITMAPWIDTH en X (plane-shifted, xlimited-uk.html § plane-shifted).
```

---

## 2. Memoria Chip

### 2.1 Fórmulas de reserva

```text
Circular (tile_field.hpp:130-131):
  row_bytes   = surface_w / 8
  plane_bytes = row_bytes * surface_h
  total_chip  = plane_bytes * planes
  // separate: planos contiguos, cada uno plane_bytes

XLimited (xlimited.c:117-123; xlimited.hpp:335-346):
  row_bytes   = BITMAPWIDTH / 8                // 44 ó 48
  // interleaved: una sola columna de BITMAPPLANELINES planelíneas
  total_chip  = row_bytes * bitmap_height * planes + bitmapoffset
  // bitmapoffset = 0 (normal), 16 (BPL32/BPAGEM), 48 (BPL32+BPAGEM)
  // frontbuffer = Planes[0] + bitmapoffset/8   (xlimited.c:122-123)
```

### 2.2 Tabla comparativa (bytes Chip, sin copperlist)

| Configuración | Circular (separate) | XLimited (interleaved) |
|---|---|---|
| 320×256, 16×16, 4 planos, `row=44`, `surface_h=289` vs `bitmap_height≈268-271` | `44*289*4 = 50 864` (+ 2 copperlists ~3 KB) | `44*268*4 = 47 168` (+ 0-48 B offset) |
| Mapa ancho 1000 bloques (16 000 px, 50 pantallas) | idéntico (no depende del mapa) | `256+11+4=271` → `44*271*4=47 696` |
| Mapa gigante 4000 bloques (64 000 px, 200 pantallas) | idéntico | `256+45+4=305` → `44*305*4=53 680` |
| 5 planos EHB (320×256, 44×289) | `44*289*5=63 580` | `44*(256+1000/22/5+4)*5 ≈ 59 000` |
| Validación | `offsets < plane_bytes`; `BPLMOD=row_bytes-fetch_bytes` | `IS_BITMAP_INTERLEAVED` (xlimited.c:129); `BPLMOD=row_bytes*planes-fetch_bytes-modulooffset` |

Lectura: el circular paga ancho+alto fijos aunque el mapa sea pequeño; XLimited paga
alto proporcional al mapa pero con un factor `1/(blocks_per_row*planes)` muy pequeño.
Para mapas grandes XLimited sigue siendo menor que el *double buffering* clásico
(que duplicaría/triplicaría cualquiera de los dos).

### 2.3 Esquema de validación

```text
Circular:  BPLxPT = chipbase + n*plane_bytes + display_byte_offset
           display_byte_offset = (fetch/8) + py*row_bytes
           fetch = (px-1) & ~15   (tile_field.hpp:223)

XLimited:  BPLxPT = Planes[n] + planeaddx          // Planes[n]=base+n*row_bytes (interleaved)
           planeaddx = (xpos/I)*(I/8)              // I=16/32/64 (xlimited.c:302)
           BPLxPT validado contra frontbuffer+planeaddx < base+total_chip
```

---

## 3. Coste Blitter — *jobs* por *frame*, *words* y presupuesto

### 3.1 Contrato del Blitter

```text
Circular (tile_field.hpp:335-365; AMIGA_8WAY_SCROLLING.md §5):
  TileBlockCopy por tile, sin fusión general (TileMajor).
  Solo RowMajor/ColumnMajor fusionan; OCS no tiene blit multi-plano → 1 wait por plano.
  words_per_row = tile_width/16, height = tile_size
  bltmod_src = 0 (TileMajor) ó stride; bltmod_dst = row_bytes - words*2
  jobs lógicos = 1 por tile; jobs físicos = jobs * planes

XLimited (xlimited.c:201-227; xlimited.hpp §6):
  Un único job copia los 4 planos a la vez (interleaved = 1 columna tall).
  words = BLOCKWIDTH/16 (=1), height = BLOCKPLANELINES (=64 en 4 planos)
  bltamod = 40-2, bltdmod = BITMAPBYTESPERROW-2, bltsize = 64*64+1 (=4097)
  bltcon0=0x09F0 (A→D), bltafwm/bltalwm=0xFFFF
  jobs físicos = jobs lógicos = 1  (un wait)
  Coste de inversión de dirección: +1 word (saveword) restaurada, no re-blit de columna.
```

### 3.2 Tabla comparativa de coste

| Métrica | Circular | XLimited |
|---|---|---|
| **Jobs por píxel de scroll** | `0` en 15/16 frames; `VISIBLE_BLOCKS_Y+2` (~18) en el cruce de `BLOCKWIDTH` (scroll_right en AMIGA_8WAY_SCROLLING.md §6). En diagonal: 2 bandas pero esquina pertenece a una sola → máx. `~18+20-1≈37` tiles en un cruce XY, repartidos con `max_tiles_per_frame` | `1` job por píxel (siempre 1 bloque entrante). `ScrollRight: x=BITMAPWIDTH+ROUND(videoposx)`, `y=mapy*BLOCKPLANELINES`; `ScrollLeft: x=ROUND(videoposx)` (xlimited.c:249-281) |
| **Jobs por frame (media a 1 px/frame)** | `~1,1` de media (`18/16`), pero con pico de `18` jobs (= `72` waits físicos en 4 planos) | `1,0` uniforme (`1` job = `1` wait) |
| **Jobs por frame (a 5 px/frame, tile_field clamp [-5,5])** | Hasta `5*18=90` lógicos si cruza 5 bloques en un frame (limitado por `max_tiles_per_frame=4` y `pump` → se acumula `pending`) | `5` jobs (uno por píxel, 5 waits) |
| **Words por job** | `words= w/16` × `h` × `planes` palabras lógicas; ej. 16×16×4 → `64` words por plano → `256` words de datos pero en `4` blits separados | `BLOCKPLANELINES*words = 64*1 = 64` words de Blitter (misma cantidad de datos, pero en 1 blit) |
| **Presupuesto recomendado** | `warning 1 job/128 words, max 2/192` (single EHB, `GRAPHICS_DRIVERS.md:174`) — necesita `pump(budget)` y `PendingStrip` | `1 job / 64 words` por frame es suficiente; `FillScreen` inicial: `22*16=352` jobs (una vez, con *budget* 120 en demo 107) |
| **Guardia / solapamiento** | Guarda de scanline completa; sin `saveword`; bandas opuestas duplicadas (entering+opposite) | Guarda de 1 planelínea; `savewordpointer/saveword` (xlimited.c:254-281): al invertir dirección se restaura 1 *word* (2 B), no toda la columna |

### 3.3 Por qué XLimited es la mitad de coste que *XUnlimited*

`xlimited-uk.html` §1: *“We also no longer do double-blits. Therefore this algorithm
is twice as fast.”* El antecesor `xunlimited.c` (doble ancho, 640 px) hacía
`double-blit` (dos destinos); XLimited, al ser interleaved y reutilizar el *wrap*
vertical, necesita un solo blit por bloque. El ahorro no es solo ancho (352 vs 640),
sino número de operaciones Blitter.

---

## 4. Copper — *waits*, registros y *split*

### 4.1 Tabla comparativa

| Aspecto | Circular | XLimited |
|---|---|---|
| **Lista Copper** | Doble buffer (`copper_blocks[2]`), pero con *split* vertical activo si `scroll_y` (tile_field.hpp:232-236) | Doble buffer sin *split*; una sola lista por frame (xlimited.c:153-199) |
| **Waits por frame** | `1` (setup) + `1` *split* en `split_line≈VH/2` (`wait DIWSTRT+split_line` + segundo juego `BPLxPT`) = **2 waits** visibles si Y activo; `1` si solo X | **1** wait de fin de lista (`wait_line(0xF8)`) — sin *split*; el *wrap* X no necesita WAIT intra-scanline (AMIGA_8WAY_SCROLLING.md §8) |
| **Moves por frame** | `BPLCON0/1/2 + BPL1MOD/BPL2MOD + DIW/DDF + 2*planes` punteros + *split* `2*planes` extra = `~6+2+2+2*planes+2*planes` | `BPLCON0/1/2 + BPL1MOD/BPL2MOD + DIW/DDF + 2*planes` punteros = `~6+2+2+2*planes` (xlimited.hpp:711-738); parcheo posterior de solo `BPLCON1` + punteros (13 *words*) |
| **BPLCON1** | `fine = (16-(scroll_x&15))&15` (HRM canónico corregido en AMIGA_8WAY_SCROLLING.md §9); `fetch=(scroll_x-1)&~15` | `I= scrollpixels (16/32/64)`, `xpos=videoposx+I-1`, `fine=(I-1)-(xpos&(I-1))`, `BPLCON1=(fine&15)*0x11|0x4400|0x8800` (xlimited.c:300-308) — para `I=16` coincide con `(16-fine)&15` duplicado en nibbles |
| **BPLMOD** | `BPLMOD = row_bytes - fetch_bytes` (`fetch_bytes= VW/8+2` si `scroll_x`) | `BPLMOD = BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulooffset` (`modulooffset 2/4/8` según `fetchinfo[]`, xlimited.c:169-172; xlimited.hpp:349-353) |
| **BPLxPT** | `chipbase + n*plane_bytes + offset` (separate) ó `chipbase + offset + n*row_bytes` (interleaved opcional) | `Planes[n] + planeaddx` donde `Planes[n]=base+n*BITMAPBYTESPERROW` (interleaved obligatorio) |
| **DDFSTRT/DDFSTOP** | `$30/$D0` → *fetch* 42 B (40 visibles +2 margen) en circular con X | Idem `$30/$D0` en modo normal; `$28/$C8` (32 px) y `$18/$B8` (64 px) en modos anchos (xlimited.c:78-84) |

### 4.2 Por qué el *wrap* horizontal no necesita Copper segmentado

`AMIGA_8WAY_SCROLLING.md §8` y `xlimited.hpp:84-113` coinciden: fijados `BPLxPT+BPLCON1+BPLMOD`,
el Denise recorre `row_bytes` contiguos por scanline sin reinicio intra-línea. En XLimited
el ancho físico `352` es contiguo y el área visible `320` cabe siempre dentro; cuando
`videoposx…videoposx+320` cruza el borde derecho, el siguiente byte que *fetcha* el Agnus
es el byte 0 de la **siguiente planelínea** (xlimited3/4.gif). Como el *bitmap* es
interleaved, esa planelínea es el siguiente plano de la misma fila de píxel y su
contenido fue escrito *plane-shifted*. No hay discontinuidad de bus que el Copper deba
reparar. En el circular, el *wrap* vertical sí necesita *split* porque el final físico
no es contiguo al inicio en la siguiente scanline visible.

Coste del *split* vertical: 1 WAIT + `2*planes` MOVEs por frame (~9-13 *words* Copper
adicionales y 1 línea donde el Copper no puede hacer otra cosa). XLimited lo elimina.

---

## 5. Direccionamiento — interleaved vs separate

### 5.1 Tabla

| Propiedad | Separate (circular por defecto) | Interleaved (XLimited obligatorio) |
|---|---|---|
| **Layout Chip RAM** | `Plane0 H líneas | Plane1 H líneas | …` contiguos | `L0P0 L0P1 L0P2 L0P3 | L1P0 L1P1 …` entrelazado por scanline |
| **Stride** | `row_bytes = surface_w/8`; `plane_bytes = row_bytes*surface_h` | `BITMAPBYTESPERROW = BITMAPWIDTH/8`; `stride plano = BITMAPBYTESPERROW`; `stride scanline = BITMAPBYTESPERROW*planes` |
| **Fórmula de dirección** | `addr = base + n*plane_bytes + y*row_bytes + x/8` | `addr = frontbuffer + y*BITMAPBYTESPERROW + (x/8 & 0xFFFE)` con `y`=planelínea (`y = block_row*BLOCKPLANELINES + plane`) |
| **Cálculo Y** | `y` en píxeles (`py = world_y - surface_origin_y`) | `y` en planelíneas (`mapy*BLOCKPLANELINES`, xlimited.c:251-274) |
| **Blitter** | 1 operación física por plano (`m_config.tileset_planes` waits) | 1 operación para todos los planos (`bltsize=BLOCKPLANELINES*64+words`, 1 wait) |
| **Validación** | `offsets + VH*row_bytes < plane_bytes` | `IS_BITMAP_INTERLEAVED(bitmap)` + `TypeOfMem & MEMF_CHIP` (xlimited.c:125-131) |

### 5.2 Esquema de direccionamiento

```text
Separate (circular):
  Chip 0x00000 ┌──────────────────┐
              │ Plane 0: H*row   │  H=surface_h
              ├──────────────────┤
              │ Plane 1: H*row   │
              ├──────────────────┤
              │ Plane 2: H*row   │
              └──────────────────┘
  BPL0PT = base + 0*plane_bytes + offset
  BPL1PT = base + 1*plane_bytes + offset
  BPLMOD = row_bytes - fetch_bytes

Interleaved (XLimited):
  Chip 0x00000 ┌─────────────────────────────────┐
              │ L0P0 44B │ L0P1 44B │ L0P2 44B │ L0P3 44B │  y=0..3
              ├─────────────────────────────────┤
              │ L1P0 44B │ L1P1 44B │ L1P2 44B │ L1P3 44B │  y=4..7
              └─────────────────────────────────┘
  frontbuffer = Planes[0] + bitmapoffset/8
  Planes[n]   = frontbuffer + n*BITMAPBYTESPERROW   // xlimited.c:122,230-238
  BPLnPT      = Planes[n] + planeaddx
  BPLMOD      = BITMAPBYTESPERROW*planes - fetch_bytes - modulooffset
  Blitter ve BITMAPPLANELINES planelíneas como una sola columna alta.
```

---

## 6. Limitaciones OCS y decisiones de diseño

| Limitación OCS | Cómo la maneja el circular | Cómo la maneja XLimited |
|---|---|---|
| **Sin *fine scroll* vertical** | Ajusta puntero por scanline + Copper *split* (FieldHardwareView::split_*) | En X puro no hay scroll Y; en XY usa `videoposy` modular + *split* con truco de módulo (`CopVIDEOSPLITMODULO`, xylimited.c:241-254) |
| ***Fetch* alineado a 16 px** | `BPLCON1` fine 0-15 + `DDFSTRT=$30` (42 B) oculta ruido de 16 px en composición | Idem `$30/$D0` (normal) ó `$28/$C8`/`$18/$B8` en modos anchos; `fetchinfo[].scrollpixels` 16/32/64 |
| **Ancho múltiplo de 16** | `TILEWIDTH%16==0`, `BITMAPWIDTH%16==0` | `BLOCKWIDTH==16`, `BITMAPWIDTH 352%16==0`, `384%64==0` (requerido para 4×) |
| **DMA de bitplanes** | 6 planos máx., `BPLCON0` con `BPU`; EHB con `BPLCON0=0x6200` | `BLOCKSDEPTH` 1-6; `BPLCON0 = BPU*… + COLOR + USEBPLCON3` (xlimited.c:162) |
| **Blitter sin *multi-plane*** | Emula *multi-plane* con N waits; `TileBlockCopy` es lógica, *backend* fusiona con *stride* si puede | Aprovecha que interleaved es *single-column*: 1 wait real para N planos |
| **Chip RAM 512 KB (A500)** | Debe caber `surface*planes` + copperlists + sprites + audio; `352*289*6≈59 KB` por *frame* + doble buffer si lo hubiera | `352*268*4≈46 KB` + offset ancho; incluso `352*305*4≈53 KB` para mapas de 200 pantallas sigue < 64 KB |
| **Copper: 1 MOVE por *slot*** | El *split* consume *slots* en mitad de pantalla; paletas pesadas compiten con él | Sin *split*; más *slots* libres para paletas/efectos raster |

---

## 7. Por qué el circular no es canónico para scroll X infinito

1. **No está publicado como algoritmo de Steger.** El corpus canónico es
   `ScrollingTrick.lha` + `weiju/amiga-stuff/scrolling_tricks` (Parts 12, 8-way).
   El circular del *engine* (`tile_field.hpp`) es una abstracción didáctica posterior
   que generaliza 8-way con `surface_origin` + bandas + `recenter_axis`. Para X
   infinito el original es `Scroller_XLimited` (Steger, 1996-1997), no un *ring buffer*
   genérico.

2. **Paga *split* de Copper y recentrado lógico que XLimited evita.**
   El circular necesita `split_active`, `split_line`, `split_display_byte_offset`
   y `surface_origin_x/y` para envolver. XLimited demuestra en `xlimited.c` y
   `xlimited-uk.html` que con `EXTRAWIDTH=32` y un *bitmap* interleaved el *wrap*
   es físico (vertical) y no requiere ni `surface_origin` ni WAIT extra.

3. **Coste Blitter en ráfaga vs uniforme.** El circular encola `16-18` tiles cada
   `16` píxeles (pico de `72` waits físicos en 4 planos) y necesita `pump(budget)`
   + `PendingStrip` + `recenter`. XLimited hace `1` job/`1` wait por píxel,
   uniforme y sin cola, con `saveword` de 2 B para la inversión de dirección.

4. **Direccionamiento: *separate* es más caro y no es el contrato de Steger.**
   `xlimited.c:129` aborta si `!IS_BITMAP_INTERLEAVED`. Todo el algoritmo
   (`frontbuffer + y*BITMAPBYTESPERROW + x`) asume interleaved; con *separate*
   la fórmula `y*row_bytes + plane*plane_bytes` rompería el *plane-shifted* y
   obligaría a `N` blits por bloque.

5. **Memoria: el `+1+3` y el término `map_width/22/planes` son parte del contrato.**
   `AMIGA_8WAY_SCROLLING.md §3` ya fija `+1+3` como canónico pero lo deriva de
   `level_map.width` sin distinguir X de XY. XLimited fija exactamente cuándo y
   por qué ese término aparece (xlimited-uk.html § *overallbitmapheight*):
   cada `16` px de scroll horizontal avanza `2` B en `BPLxPT`; sin altura extra ese
   avance dejaría de ser contiguo al envolver.

6. **Documentación del *engine* ya lo refleja.** `xlimited.hpp:1-199` declara
   explícitamente *“sin arrastrar la máquina circular de `TileFieldController`”*
   y prohíbe compartir `surface_origin`/bandas. La comparativa canónica es
   `AMIGA_8WAY_SCROLLING.md` (circular) vs `xlimited.hpp` (XLimited).

En síntesis: el circular es útil como *driver* 8-way genérico o para prototipos
con scroll en ambos ejes y mapas pequeños. Para **scroll X infinito** (plataformas,
*shooters* horizontales, fondos con *parallax* por *playfield*) el canónico es
XLimited interleaved: menos Chip RAM para mapas grandes, la mitad de *waits* de
Blitter, sin *split* de Copper y con *saveword* de 2 B en lugar de bandas duplicadas.

---

## 8. Guía de migración (de `TileFieldController` a `XlimitedField`)

| Paso | Circular | XLimited |
|---|---|---|
| Reserva | `TileFieldController::begin(memory,cfg,offset)` con `viewport+margin` | `XlimitedField::begin(memory,cfg)` — `cfg.bitmap_width=352/384`, `cfg.planes`, `cfg.fetch_mode`; valida interleaved |
| Relleno inicial | `enqueue_initial()` + `pump` hasta `!busy()` | `fill_screen(plan)` → `22*16` jobs; ejecutar con `MinimalBackend` y *budget* (demo 107: 120) |
| Scroll | `update(cfg,delta,plan)` con clamp `[-5,5]` + `pump` | `scroll_right(plan)` / `scroll_left(plan)` — 1 job por llamada; `mapposx/videoposx` avanzan 1 px |
| Composición | `hardware_view(first_plane)` → `FieldHardwareView` con `split_*` | `hardware_view()` → `XlimitedHardwareView{planeaddx,bplcon1,bpl1mod/bpl2mod,bitmap_bytes_per_row}` → `XlimitedDisplayComposer::compose/install` |
| Copper | El compositor externo debe manejar `split_line` | `XlimitedDisplayComposer` sin *split*; `BPLMOD = row_bytes*planes - fetch - offset` |
| Guarda | Última scanline a 0; no tocar | `saveword` automático; no escribir la guarda manualmente |

Para scroll X infinito, preferir `engine/include/eng/field/xlimited.hpp`
(`XlimitedField` + `XlimitedDisplayComposer`). `TileFieldController` se mantiene
para casos 8-way genéricos (ver nota de deprecación suave en `TILE_FIELD_API.md`).

---

*Documento verificado contra `xlimited.c`, `xylimited.c`, `xlimited-uk.html`,
`xlimited.hpp` y `tile_field.hpp` a 2026-08-30. No editar sin re-verificar
la fórmula de altura y el contrato `saveword`.*
