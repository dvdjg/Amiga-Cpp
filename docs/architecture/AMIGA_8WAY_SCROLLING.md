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
- **BLOCKPLANELINES**: líneas por bloque = BLOCKHEIGHT (stride según layout).
- **BITMAPBLOCKSPERROW**: bloques por fila = BITMAPWIDTH / BLOCKWIDTH.
- **BLOCKSDEPTH**: profundidad de bloques en horizontal por plano.
- **level_map.width**: anchura lógica del nivel en bloques/tiles.
- **plane_bytes/row_bytes**: bytes por fila = BITMAPWIDTH/8 + margen fetch (40+2 con DDFSTRT=$30).
- **surface**: viewport + margen circular (VW+2*BW en X, filas extra en Y).
- **BPLxPT/BPLCON1/BPLMOD/DDFSTRT**: registros chipset. BPLCON1 fine 0-15, BPLMOD salto de módulo.
- **Copper**: reprograma registros por scanline (WAIT+MOVE). Solo punteros/scroll.
- **Blitter**: único que copia píxeles. Nunca la CPU.
Invariantes: TILEWIDTH múltiplo de 16; margen mínimo 2 bloques; wrap_x/wrap_y para scroll infinito;
edge_tile cierra mundo no periódico; 3 bloques opcional para diagonal/inversión.
## 3. Geometría canónica
Fórmula canónica del alto físico (ScrollingTrick.asm + Part 12):
```text
bitmapheight = BITMAPHEIGHT + (level_map.width / BITMAPBLOCKSPERROW / BLOCKSDEPTH) + 1 + 3
```
Desglose:
- `BITMAPHEIGHT`: alto visible.
- `level_map.width / BITMAPBLOCKSPERROW / BLOCKSDEPTH`: filas del desenrollado horizontal en vertical.
- `+1`: guard line (1 línea a 0, fuera de retícula) absorbe fetch especulativo del Denise.
- `+3`: reserva vertical para 8-way e inversiones (2 mínimo, 3 cubre diagonal+inversión).
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

Ejemplo 320x256, bloque 16, level_map.width=1024, BITMAPBLOCKSPERROW=20, BLOCKSDEPTH=1:
- bitmapwidth=352px=44 bytes/fila (40 visibles+2 margen DDFSTRT=$30+2 guarda).
- bitmapheight=256+(1024/20/1)+1+3=311 -> alineado TILE -> 320.
- plane_bytes=bitmapwidth/8*bitmapheight por plano; validar offsets < plane_bytes.

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

### 7.3 BPLMOD — módulo

```text
BPLMOD1/2=row_bytes - BITMAPWIDTH/8 - fetch_extra
```

Con 320px y 42 bytes fetch: BPLMOD=row_bytes-40 (interleaved ajusta por bitplane_count).

### 7.4 Split vertical

Sin fine vertical: se ajusta BPLxPT. Al cruzar final físico, Copper WAIT a mitad de viewport y
segundo juego BPLxPT al inicio físico. FieldHardwareView expone split; compositor valida offsets.
TileFieldController nunca escribe chipset.

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

Documento canónico — 232 líneas. No editar sin re-verificar contra ScrollingTrick.lha y Part 12.
