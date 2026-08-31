# Scroll 8-way circular en Amiga OCS — Buffer anular (ScrollingTrick)
> Documento canónico verificado (232 líneas). Restaura el contenido del task ses_fac017869ffelOf6QnKXHpgf88.
> No editar sin re-verificar contra ScrollingTrick.lha y Part 12.
## 1. Fuentes primarias
- Aminet — ScrollingTrick (fuente original): https://aminet.net/package/dev/src/ScrollingTrick
- GitHub weiju/amiga-stuff — scrolling_tricks Part 12: https://github.com/weiju/amiga-stuff/tree/master/scrolling_tricks
Ambas coinciden en: bitmap circular, banda por bloque, Blitter único escritor, Copper solo para
punteros/scroll. Lectura: README ScrollingTrick.lha → 12_8way/README → ScrollingTrick.asm.
Este documento fija geometría y Copper de esa lectura directa.
## 2. Definiciones base
- **Viewport**: ventana visible (ej. 320x256). No es la superficie física.
- **BITMAPWIDTH/HEIGHT**: dimensiones visibles. WIDTH múltiplo de 16 en OCS.
- **BLOCKWIDTH/HEIGHT**: unidad de reserva/dibujo (16x16 típico), múltiplo de TILE.
- **BLOCKPLANELINES**: líneas por bloque = BLOCKHEIGHT * planes (ej. 16*3=48, 16*4=64, 16*5=80, 16*6=96 para 3..6 planos; genérico bytes*planes).
- **BITMAPBLOCKSPERROW**: bloques por fila = BITMAPWIDTH / BLOCKWIDTH (22 para 352, 24 para 384).
- **BLOCKSDEPTH**: profundidad de bloques en horizontal por plano (planes 3..6, 2^planes colores).
- **level_map.width**: anchura lógica del nivel en bloques/tiles.
- **plane_bytes/row_bytes**: bytes por fila = BITMAPWIDTH/8 + margen fetch (40+2 con DDFSTRT=$30; interleaved genérico `BITMAPBYTESPERROW*planes`).
- **surface**: viewport + margen circular (VW+2*BW en X, filas extra en Y).
- **BPLxPT/BPLCON1/BPLMOD/DDFSTRT**: registros chipset. BPLCON1 fine 0-15, BPLMOD salto de módulo.
- **Copper**: reprograma registros por scanline (WAIT+MOVE). Solo punteros/scroll.
- **Blitter**: único que copia píxeles. Nunca la CPU.
Invariantes: TILEWIDTH múltiplo de 16; margen mínimo 2 bloques; wrap_x/wrap_y para scroll infinito;
edge_tile cierra mundo no periódico; 3 bloques opcional para diagonal/inversión.
## 3. Geometría canónica
Fórmula canónica del alto físico (ScrollingTrick.asm + Part 12, xlimited.c:45-73; genérica planes 3..6, bytes*planes):
```text
bitmapheight = BITMAPHEIGHT + (level_map.width / BITMAPBLOCKSPERROW / planes) + 1 + 3
  // planes = BLOCKSDEPTH 3..6 (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3); genérico bytes*planes
  // ej. 22*3=66, 22*4=88, 22*5=110, 22*6=132 bloques por planelínea extra para 352 (24*planes para 384)
```
Desglose:
- `BITMAPHEIGHT`: **depende del algoritmo** (errata corregida 2026-08-31):
  - **X-Limited puro** (`xlimited.c`): `BITMAPHEIGHT = SCREENHEIGHT` (solo el alto visible).
  - **Corkscrew/XY** (`XYLimited`, `Scroller_XYLimited/main.c`): `BITMAPHEIGHT = SCREENHEIGHT + EXTRAHEIGHT`, con `EXTRAHEIGHT = 2*BLOCKHEIGHT` (32 para tiles de 16). Ese extra de 2 bloques es la **banda de staging** donde se pre-pinta la fila/columna entrante antes de que el display la alcance al dar la vuelta en `display_height = viewport_h + 2*tile_height`. En el engine esto se parametriza con `scroll_y=true` (ver `docs/architecture/AMIGA_8WAY_SCROLLING.md` §13 y `xlimited.hpp` §1).
- `level_map.width / BITMAPBLOCKSPERROW / planes`: filas del desenrollado horizontal en vertical (genérico `blocks_per_row*planes`).
- `+1`: guard line (1 planelínea a 0 en interleaved, 1 línea en separate) absorbe fetch especulativo del Denise.
- `+3`: reserva vertical para 8-way e inversiones (2 mínimo, 3 cubre diagonal+inversión; en XLimited margen fetch ancho con `modulo_offset` 2/4/8).
Ancho físico:
```text
bitmapwidth = BITMAPWIDTH + 2*BLOCKWIDTH
```
Genérico VW/VH, BW/BH:

```text
X: VW + 2*BW
Y: floor((VH + 2*BH) / TILEHEIGHT) * TILEHEIGHT + 1
```

Última línea = guard line. Con solo X: (VW+2*BW)xVH; con solo Y: VWx(floor((VH+2*BH)/BH)*BH+1).
La fórmula 2*viewport+2*block era de dos páginas lineales, no del circular mínimo.

Ejemplos genéricos bytes*planes (planes 3..6, 24*planes para 384 vs 22*planes para 352):
- level_map.width=1024, BITMAPBLOCKSPERROW=20, planes=1 (ej. didáctico 1 plano): bitmapheight=256+(1024/20/1)+1+3=311 -> 320.
- 320x256, bloque 16, level_map.width=1024, BITMAPBLOCKSPERROW=22, planes=4: bitmapheight=256+ floor(1024/22/4)=11+4=271 (genérico `blocks_per_row*planes`).
  Para planes=3: 256+ floor(1024/22/3)=15+4=275; planes=5: 256+9+4=269; planes=6: 256+7+4=267.
- bitmapwidth=352px=44 bytes/fila base (40 visibles+2 margen DDFSTRT=$30+2 guarda; interleaved total por scanline = 44*planes → 132/176/220/264).
- plane_bytes=bitmapwidth/8*bitmapheight por plano (separate) o `BITMAPBYTESPERROW*bitmap_height*planes` interleaved; validar offsets < plane_bytes y `BPLMOD=row_bytes*planes-fetch_bytes-modulo_offset`.

## 4. Layout en Chip RAM: interleaved vs separate

### 4.1 Interleaved (por línea)

```text
Dirección → [ L0 P0 ][ L0 P1 ][ L0 P2 ][ L0 P3 ] <- scanline 0 (4 planos)
            [ L1 P0 ][ L1 P1 ][ L1 P2 ][ L1 P3 ] <- scanline 1
Stride plano=row_bytes; stride scanline=row_bytes*bitplane_count
BPLMOD=(row_bytes*(bitplane_count-1)+margen_extra)
```

Ventaja: job fusionado con destination_plane_stride=row_bytes escribe varios planos con 1 wait.
Desventaja: offset con multiplicación por planos.

### 4.2 Separate (planos contiguos)

```text
Chip RAM: 0x00000 ┌───────────────┐
                  │ Plane 0 H líneas │ H=bitmapheight
                  ├───────────────┤
                  │ Plane 1 H líneas │
                  ├───────────────┤
                  │ Plane 2 H líneas │
                  └───────────────┘
Offset plano n=n*plane_bytes; offset scanline y=y*row_bytes
```

Ventaja: cálculo simple. Desventaja: 1 wait por plano si no se fusiona.

Esquema buffer circular (VW=320,BW=16):

```text
              BW=16     VW=320          BW=16
            ┌──────┬──────────────────┬──────┐
            │      │                  │      │ ─┬─
            │ MARGEN│ VIEWPORT VISIBLE │MARGEN│  │VH
            │ IZQ  │                  │ DER  │ ─┴─
            ├──────┼──────────────────┼──────┤ ─┬─BH
            │ BANDA INFERIOR + guard line   │ ─┴─
            └──────┴──────────────────┴──────┘
 ◄─ bitmapwidth=VW+2*BW ─────────────►
 ◄─ row_bytes=bitmapwidth/8 ─────────►
```

Display se mueve dentro; al tocar margen se recentra (ver §7).

## 5. draw_block

Primitiva que materializa un bloque del mapa en el margen. No dibuja páginas.

```c
void draw_block(int map_x, int map_y, int dest_x, int dest_y) {
    Tile *t = level_map[map_y*level_map.width+map_x];
    for (int p=0;p<bitplane_count;++p) blit_tile(t->plane[p],dest_x,dest_y,p);
}
```

Fusión 2026-08: 1 job por tile con destination_plane_stride=row_bytes reduce wait de N a 1.
Reglas: destino fuera del viewport; tile resuelto justo antes de encolar (evita cola obsoleta si
invierte sentido); job pendiente se consume por tiles (take_budget); CPU solo encola TileBlockCopy.

Banda H=columna en Y; banda V=fila en X. En XY esquina pertenece a primera y se descuenta de segunda.
Jobs por cruce: 0,1 o 2 — nunca página completa ni esquina duplicada.

## 6. scroll_right / scroll_left

Avanzan 1px; solo al cruzar frontera de bloque encolan banda. Máscara 4 bits = anillo 16 bloques.

```c
void scroll_right(void) {
    scroll_x++;
    if ((scroll_x & (BLOCKWIDTH-1))==0) {
        int mapx=(scroll_x/BLOCKWIDTH)+VISIBLE_BLOCKS_X;
        int mapy_base=mapposx & 15;
        for(int i=0;i<VISIBLE_BLOCKS_Y+2;++i){
            int mapy=(mapy_base+i)&15;
            int y = mapy*BLOCKPLANELINES;
            int dest_x=(scroll_x+BITMAPWIDTH)&bitmap_mask_x;
            draw_block(mapx,mapy,dest_x,y);
        }
    }
    copper_update(scroll_x,scroll_y);
}
void scroll_left(void) {
    scroll_x--;
    if ((scroll_x & (BLOCKWIDTH-1))==0) {
        int mapx=(scroll_x/BLOCKWIDTH)-1;
        int mapy_base=mapposx & 15;
        for(int i=0;i<VISIBLE_BLOCKS_Y+2;++i){
            int mapy=(mapy_base+i)&15;
            int y = mapy*BLOCKPLANELINES;
            int dest_x=scroll_x&bitmap_mask_x;
            draw_block(mapx,mapy,dest_x,y);
        }
    }
    copper_update(scroll_x,scroll_y);
}
```

Claves: `mapy=mapposx&15` y `y = mapy*BLOCKPLANELINES` anillan vertical en bitmap físico. &15 es
ejemplo 16 bloques (Part 12); otro tamaño usa potencia de dos/módulo. Destino X con máscara
VW+2*BW para wrap circular. scroll_y análogo con `mapx=mapposy&15` / `x=mapx*BLOCKWIDTH`.
Solo encola al cruzar BLOCKWIDTH; resto de frames solo mueve Copper.

## 7. Copper: planeaddx / BPLCON1 / BPLMOD

### 7.1 BPLCON1 — fine scroll 0-15

Canónica ACE/HRM (corrige errata §9):

```text
fine=scroll_x & 15
BPLCON1=(16-fine)&15
fetch_x=(scroll_x-1)&~15  // coarse: word antes solo si fine==0
display_start==scroll_x continuo 0..15->0
```

DDFSTRT=$30 en 320px: fetch 42 bytes (40+2 margen). Word extra cubre 0-15 sin perder píxeles.
Ruido en 16px iniciales se oculta en composición, no se rellena por CPU. Dual PF: dos nibbles.

### 7.2 BPLxPT — punteros

```c
int coarse=(scroll_x-1)&~15; int offset_x=coarse>>3;
int offset_y=scroll_y*row_bytes; int planeaddx=offset_x+offset_y;
// separate: BPLxPT=chipbase+n*plane_bytes+planeaddx
// interleaved: BPLxPT=chipbase+planeaddx+n*row_bytes
```

offset_y ya incluye anillo `y = mapy*BLOCKPLANELINES`.

### 7.3 BPLMOD — módulo (genérico bytes*planes, modulo_offset 2/4/8)

```text
BPLMOD1/2 = BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
  // modulo_offset = 2 (normal DDF $30), 4 (BPL32/BPAGEM $28), 8 (BPL32+BPAGEM $18)
  // SCREENBYTESPERROW = 40 (320/8); FETCH_BYTES = 42 (40+2) normal
```

Con 320px y fetch 42 bytes: `BPLMOD=row_bytes*planes-40-modulo_offset` (ej. 44*4-40-2=134 normal; 44*3-40-2=90; 44*6-40-2=222). Para 384 fetch ancho: 48*planes-40-4/8.

### 7.4 Split vertical

Sin fine vertical: se ajusta BPLxPT. Al cruzar final físico, Copper WAIT a mitad de viewport y
segundo juego BPLxPT al inicio físico. FieldHardwareView expone split; compositor valida offsets.
TileFieldController nunca escribe chipset.

En el corkscrew (XYLimited) el split vertical es **obligatorio** y su línea varía con el scroll
(ver §13): `raster = DIWSTRT_y + (display_height - display_offset)` con
`display_offset = (videoposy + tile_height) % display_height`. El segundo juego de punteros
apunta a la fila 0 del bucle de display (`real_base + planeaddx + p*row_bytes`). **Límite del
chipset**: el comparador de WAIT es de 8 bits con semántica `>=` (`vp = vpos & 0xFF >= vcmp`, sin
bit V8; verificado en `WinUAE-DBG/custom.cpp coppercomp`), por lo que `raster > 255` (cuando
`display_offset ∈ [33,73]`) no se puede esperar con precisión: el WAIT dispara en la primera
coincidencia del byte bajo y el split se recorta a la línea 255 (banda de 1..41 filas al pie con
el wrap adelantado). Es inherente al chipset; el `XYLimited` original degrada igual a 255.

## 8. Por qué el wrap horizontal no necesita Copper segmentado

Fetch horizontal es lineal por scanline: fijados BPLxPT+BPLCON1+BPLMOD, Denise recorre row_bytes
contiguos sin reinicio intra-línea. Wrap X se resuelve con geometría, no con Copper intra-scanline:
- Ancho físico VW+2*BW contiguo: display lee ventana BITMAPWIDTH contigua dentro, aunque cruce límite lógico.
- Blitter mantiene coherentes ambos márgenes con mismo contenido; al acercarse al borde físico el
  controlador recentra ventana al lado opuesto (surface_origin_* + punteros) sin mover memoria.
- Copper solo parte verticalmente (dos juegos BPLxPT). Partir cada scanline horizontal duplicaría WAITs
  y rompería fetch contiguo sin aportar nada: wrap X es offset, no discontinuidad de bus.
Wrap vertical sí necesita split (final físico no contiguo al inicio en siguiente scanline visible).

## 9. Corrección errata OCS

- Antes (incorrecto): BPLCON1=fine con fetch=coarse-16. Invertía fine y salto ~31px al cruzar 15->0.
- Ahora (canónico): BPLCON1=(16-fine)&15 con fetch=(scroll_x-1)&~15. Continuo. Verificar con
  verify-tile-scroll-modes.mjs y analyze-fine-scroll.sh --warp.
Otras: no triple buffering (único bitmap circular); guard line 1 línea obligatoria (sin ella fetch
lee fuera de plane_bytes); OCS 16px vs AGA 64 bits pero geometría igual; Blitter por plano (OCS sin
blit multi-plano; TileBlockCopy es lógica, backend fusiona con stride).

## 10. Validación

- Host: node tools/analyze/verify-tile-scroll-modes.mjs (coarse/fine 4/5/6 y 2+3/3+3 dual)
- Fine: bash tools/analyze/analyze-fine-scroll.sh --warp
- Secuencia: bash tools/analyze/analyze-sequence.sh --warp (WinUAE-DBG 2346)
- Regresión: bash ./tools/test-regression.sh [--warp] (build->run->analyze)
- Telemetría: winuae_debugperiph checkpoints/console (~10fps overhead; quitar para medir)
Invariantes: plane_bytes>=bitmapheight*row_bytes; offsets+BITMAPHEIGHT*row_bytes<plane_bytes;
BPLMOD constante, BPLCON1 0..15, fetch alineado 16; jobs por cruce en {0,1,2}; destino fuera
viewport; guard line a 0 nunca escrita por draw_block.

---

## 11. Comparativa con XLimited (apéndice)

Para scroll X infinito el modelo circular anterior no es canónico. La comparativa
completa — geometría, memoria Chip, coste Blitter (jobs/*frame*, *words*), Copper
(*waits*), direccionamiento interleaved vs separate, limitaciones OCS y por qué el
circular no es canónico — está en `docs/architecture/CIRCULAR_VS_XLIMITED.md`,
verificado contra `xlimited.c:45-323`, `xlimited-uk.html` y
`engine/include/eng/field/xlimited.hpp`. En resumen: XLimited usa *bitmap*
352/384 interleaved, altura `256+(map_width/blocks_per_row/planes)+1+3` genérica
(22*3=66, 22*4=88, 22*5=110, 22*6=132 para 352; 24*planes para 384; bytes*planes),
1 job/*frame* con *plane-shifted* y `saveword`, sin *split* de Copper; el
circular paga ráfaga de `VISIBLE_Y+2` jobs cada 16 px y 1 WAIT de *split* vertical.

---

## 12. Parametrización viewport/espacio virtual 16×16 (2026-08-30)

`XlimitedConfig` parametriza sin hardcodes el espacio virtual y el viewport:

```text
viewport_w/h = K_VIEWPORT_W/H (defecto 320/256, alternativo 288/224)
tile_w/h     = K_TILE_W/H (defecto 16)
screens_x/y  = K_SCREENS_X/Y (defecto 16 → 16×16 pantallas)
map_w = screens_x * (viewport_w / tile_w)   // si map.width==0 se deriva, si !=0 se respeta
map_h = screens_y * (viewport_h / tile_h)
bitmap_width  = viewport_w + EXTRAWIDTH (32 normal, 64 ancho) si ==0 → auto
bitmap_height = viewport_h + floor(map_w / blocks_per_row / planes) +1+3
visibleRows   = viewport_h / tile_h
colHeight     = visibleRows + (scroll_y?1:0)   // sin literales 16/17
BPLMOD = bitmap_bytes_per_row*planes - viewport_w/8 - modulo_offset
```

Casos verificados:

- **320×256** (defecto): 20×16 tiles por pantalla, bitmap 352 (22 bloques) / 384 (24), mapa 16×16 → 320×256 tiles (5120×4096 px con tile 16). Fill `22×16=352` jobs.
- **288×224** (alternativo): 18×14 tiles por pantalla, bitmap 320 (20 bloques) / 352 (22 con fetch ancho), mapa 16×16 → 288×224 tiles (4608×3584 px). Fill `20×14=280` jobs (sin scroll_y) y con scroll_y (corkscrew) `20×16=320` (`display_height=224+32=256 → 16 filas`). BPLMOD genérico usa `viewport_w/8` (36 para 288 → 40*planes-36-2). `verify-xlimited.mjs` deriva `SCREEN_W/H`, `BITMAPWIDTH`, `BLOCKSPERROW` de `K_VIEWPORT_W/H` y valida ambos viewports (`K_VIEWPORT_W=288 K_VIEWPORT_H=224 node tools/analyze/verify-xlimited.mjs`). La demo 107 deriva `kMapTilesX/Y = K_SCREENS_X/Y * (K_VIEWPORT_W/H / K_TILE_W/H)` y pasa `viewport_w/h`, `screens_x/y`, `tile_w/h` a `XlimitedConfig` sin literales 320/256.

Invariantes parametrizados: `viewport_w % tile_w==0`, `viewport_h % tile_h==0`, `bitmap_width % tile_w==0`, `bitmap_width >= viewport_w+tile_w`, `total_bytes = bitmap_bytes_per_row*bitmap_height*planes`.

---

## 13. Corkscrew / XYLimited (demo 107, scroll_y=true) — geometría del 8-way

Port fiel de `Scroller_XYLimited/main.c` (ScrollingTricks). Es el algoritmo con el que la
demo 107 hace scroll **8-way** real: el display envuelve verticalmente en
`display_height = viewport_h + 2*tile_height` y la fila/columna entrante se **pre-pinta** en la
banda de staging de 2 bloques que el display alcanza al dar la vuelta.

Fórmulas (engine, `XLimitedPlayfield`, valores por defecto 320×256, tile 16, planes 4, fetch normal):

```text
display_height      = viewport_h + 2*tile_height                      // 288 para 320×256
bitmap_height       = redondear_a_tile(display_height
                        + (map_width / bitmap_blocks_per_row / planes) + 1 + 3)   // 304
BITMAPBLOCKSPERCOL  = display_height / tile_height                    // 18 (fill = visibleRows+2)
TWOBLOCKSTEP        = bitmap_blocks_per_row - tile_height             // 22-16 = 6 (NO visible_cols)
block_videoposy     = (mapposy / tile_height * tile_height) % bitmap_height  // fila física de staging
display_offset      = (videoposy + tile_height) % display_height      // yoffset de los punteros BPL
split_line          = display_height - display_offset                 // fila del wrap dentro de la ventana
split_active        = split_line < viewport_h                         // hace falta el Copper split
```

Scroll por píxel (1 px/frame, invariante de 50 fps):

- **down/up**: la fila entrante se dibuja en `y = block_videoposy * planes` con
  `map_tile_y = mapblocky + BITMAPBLOCKSPERCOL` (down) / `mapblocky` (up), y x según
  TWOBLOCKSTEP (2 bloques si `stepy < TWOBLOCKSTEP`, 1 si no). Al cruzar fila de bloque se ajusta
  la columna de fillup y la guarda de 1 word.
- **right/left**: la columna entrante se dibuja en `x = BITMAPWIDTH + ROUND2BLOCKWIDTH(videoposx)`
  (right, plane-shifted) o `x = ROUND2BLOCKWIDTH(videoposx)` (left), con
  `y = (block_videoposy + mapy*tile_height) % display_height` y `mapy = stepx+1` (2 bloques si
  stepx==0). Al completar columna se ajusta la fila de fillup.
- Todos los blits usan **valores PRE-incremento** del original; los pasos de fillup usan los
  POST-incremento de `mapblockx/mapblocky` (el port del engine replicó esta asimetría, verificado
  con `node tools/analyze/verify-corkscrew.mjs` host).

**Invariante de la banda de staging (corrección 2026-08-31)**: `block_videoposy` debe envolver en
`display_height` (0..288), **no** en `bitmap_height` (304). El original (Scroller_XYLimited) lo
envuelve en `bitmapheight`, y ese wrap hacía que cada `bitmap_height` px de scroll vertical la
fila entrante se dibujara en las filas extra (288..303) que el *planeaddx walk* del display SÍ
muestra al scrollear en X: aparecía un tile obsoleto/parcial en el área visible y la banda de
staging quedaba sin refrescar (banda de tiles antiguos al rotar). El port envuelve en
`display_height` para mantener la fila entrante siempre en la banda oculta de 32 filas.

Copper: paleta al inicio del frame, punteros principales en `yoffset`, y si `split_active` un
WAIT en `raster = DIWSTRT_y + split_line` seguido de los punteros a la fila 0
(`real_base + planeaddx + p*row_bytes`). **Límite del chipset (confirmado 2026-08-31 en
`WinUAE-DBG/custom.cpp coppercomp`)**: el comparador de WAIT es de 8 bits con semántica `>=`
(`vp = vpos & 0xFF >= vcmp`, sin bit V8), así que `raster > 255` (cuando `display_offset ∈
[33,73]`) dispara en la primera coincidencia del byte bajo y el split se recorta a la línea 255
(banda de 1..41 filas al pie con el wrap adelantado). Es **inherente al chipset**; el `XYLimited`
original degrada igual a 255 (su truco enmascarado `0xFFDF`/`0x0001` también dispara en 255 con
`>=`). No se puede eliminar con un WAIT de 9 bits en este emulador (la comparación no tiene V8).

Verificación: `verify-xlimited.mjs` (§11) modela esta geometría; el port de los 4 scrolls se
compara bloque a bloque contra `Scroller_XYLimited/main.c` en `node tools/analyze/verify-corkscrew.mjs`.

**Evidencia runtime (WinUAE-DBG, 2026-08-31)**: fase V (cámara bajando) capturada 60 frames a
20 ms — sin banda negra al pie (0 % en la fila inferior e interna), el contenido se desplaza
arriba ~1 px/frame (mediana −1 px nativo), tiles bien formados (análisis determinista +
qwen3-vl local). Fase H: `analyze-sequence.sh` completa (100 frames derecha, `ChangedPairs=99`,
`DuplicatePairs=0`, sin hueco de 2 bytes de saveword, telemetría mapposx/videoposx/BPLCON1
avanzando). Bug de la banda de staging (block_videoposy envolviendo en bitmap_height) reproducido
en mapposy=900 con videoposx=400: comparación antes/después (mismo frame pausado) confirma que el
bug dejaba una banda de tiles obsoletos en el área visible y que envolver en display_height lo
elimina (qwen3-vl: "mitad izquierda con banda de tiles obsoletos, derecha corregida").
Limitación del split confirmada como inherente al chipset: `raster > 255` (cuando
`display_offset ∈ [33,73]`) no se puede esperar con el comparador de 8 bits `>=`, el WAIT dispara
en la primera coincidencia del byte bajo y el split se recorta a 255 (banda de 1..41 filas al pie
con el wrap adelantado; no es negro ni tearing, y el `XYLimited` original degrada igual).

## 14. Capa de dibujo sobre el corkscrew (2026-08-31)

Toda operación de dibujo sobre el framebuffer debe pasar por una primitiva que conozca el layout
físico: la planelínea y el byte de un píxel dependen de `display_offset` y del modo (split vs
espejo). Las primitivas pertenecen al **`Playfield`** (`playfield.hpp`): `Playfield` es la base
abstracta (framebuffer + geometría + primitivas CPU implementadas vía hooks de mapeo +
blits virtuales + `update_scroll` + `hardware_view`), `XLimitedPlayfield` es el corkscrew
(`xlimited.hpp`, especialización del scroll) y `CanvasPlayfield` es un lienzo plano sin scroll.
La `XlimitedScene` compone los playfields con roles (`bg()`/`fg()`) y las primitivas se llaman
sobre el playfield:

| Primitiva | Tipo | Coordenadas | Restricciones | Regla del layout |
|---|---|---|---|---|
| `pf.set_pixel(wx,wy,color)` | CPU | mundo | — | `word_byte=(wx/8)&~1`, `mask=0x8000>>(wx&15)`, planelínea `(wy%DH)*planes`; espejo en lineal |
| `pf.fill_rect(wx,wy,w,h,color)` | CPU | mundo | — | vía `set_pixel` (wrap de costura por píxel) |
| `pf.draw_line(...)` (Bresenham) | CPU | mundo | — | vía `set_pixel` |
| `pf.add_world_bitmap(src,wx,wy,...)` | Blitter | mundo | `wx` múltiplo de 16, origen Chip RAM | "espejo = duplica, split = parte" |
| `pf.add_world_bitmap_masked(src,mask,wx,wy,...)` | Blitter | mundo | `wx` múltiplo de 16, origen Chip RAM, máscara 1 bit | cookie-cut `dest=(mask&src)\|(~mask&dest)` |

Todas devuelven `bool` y validan límites.

**Walk horizontal**: el byte físico de un píxel de mundo es `(planelínea)*row_bytes + wx/8`, que
cruza a la siguiente planelínea cuando `wx/8 >= row_bytes` (el *fetch* lineal del Agnus hace lo
mismo al envolver). Las primitivas CPU lo soportan acotando contra el tamaño total del bitmap; los
blits requieren `wx` word-aligned (por eso los objetos fijos en pantalla van por CPU y los del
mundo por blit).

**Objeto fijo en pantalla (HUD)**: la ventana visible NO empieza en `videoposy` — el corkscrew
deja la banda de staging un bloque por encima, así que `display_offset = (mapposy+tile_height) %
display_height`. Un objeto en la fila de pantalla `sy` se dibuja en `wy = screen_to_world_y(sy)`
(`XLimitedPlayfield`, equivale a `(mapposy+tile_height+sy) % DH`) y `wx = screen_to_world_x(sx)`
(`mapposx()+sx`). Usar `mapposy()+sy` directamente deja el objeto en la banda de staging, fuera
de pantalla.

**Limitación de bus (medida 2026-08-31)**: las escrituras CPU al chip RAM durante el frame
visible roban ciclos al DMA de bitplanes. Con 1-2 `set_pixel` no hay efecto; con decenas-centenas
de RMW (`fill_rect`/`draw_line` grandes) el emulador muestra scanlines negros periódicos
(inanición de bus). El dibujo masivo va por **Blitter**; las primitivas CPU se reservan para
marcas pequeñas o init (boot, sin competencia).

**HUD en franja inferior como playfield separado (trabajo futuro)**: el escenario de un HUD de 32
px con configuración distinta (bitplanes/paleta) en la parte inferior es un split-screen de
Copper (cambiar DIWSTOP/BPLxPT/modulos en el raster `DIWSTRT_y + main`). Se intentó (WAIT en el
raster + cambio de BPL pointers a un `CanvasPlayfield` con la ventana abierta al total), pero el
canvas no se mostró en WinUAE; requiere inspección del estado real del Copper (BPLCON0/BPLxPT por
frame) para depurar por qué el cambio de punteros no surte efecto. La abstracción ya lo soporta a
nivel de API (escena con varios playfields + roles); falta la zona de Copper.

**Verificación**: `node tools/analyze/verify-draw-primitives.mjs` modela el contrato (planelínea +
espejo + walk + objeto fijo + CanvasPlayfield) y valida split, lineal y viewports. En emulador, el
HUD de la demo 107 (`K_HUD=1`) marca píxeles fijos en pantalla y dibuja un BOB enmascarado de
mundo (bloque blanco 16×16 con hueco 4×4 que deja ver el mapa, cookie-cut confirmado) sin
blackouts.

---

Documento canónico — corregido 2026-08-31 (errata §3 BITMAPHEIGHT vs EXTRAHEIGHT del corkscrew,
split vertical de XYLimited y banda de staging). No editar sin re-verificar contra ScrollingTrick.lha y Part 12.
