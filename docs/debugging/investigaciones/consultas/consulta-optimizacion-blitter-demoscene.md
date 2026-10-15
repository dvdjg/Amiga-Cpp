# Consulta para IA: por qué el port C++ del efecto es 2.4x más lento que el original

Pregunta técnica autocontenida para una IA externa. Contexto: portamos el efecto `flatshade-convex` de un repo demoscene (C/asm 68000, Amiga OCS/ECS) a nuestro engine C++23 y funciona visualmente idéntico, pero es **2.4x más lento**. Tenemos medidas exactas por sección y sospechamos del coste del Blitter del emulador y/o del código de transformación. Necesitamos entender **dónde está la diferencia** y cómo cerrarla.

## Efecto y hardware

Amiga OCS/ECS (A500, 68000 a 7.09 MHz, PAL 50 Hz), **Blitter** con canales A/B/C/D, 4 bitplanes de 256×256 en **Chip RAM contigua** (`plane_bytes = 32*256 = 8192`), **doble buffer**. Objeto convexo girando con sombreado plano por cara: se calcula la luz de cada cara en CPU, se dibujan las aristas visibles con Blitter (`ONEDOT`+EOR) y se rellena con **un** area fill `XOR`.

## Algoritmo del original (C + asm inline)

```c
/* Limpia los 4 planos contiguos con UN blit. */
static void BitmapClearFast(BitmapT *dst) {
  u_short height  = (short)dst->height * (short)dst->depth;   /* 256*4 = 1024 */
  u_short bltsize = (height << 6) | (dst->bytesPerRow >> 1);  /* (1024<<6)|16 -> u16 = 16 */
  void *bltpt = dst->planes[0];
  WaitBlitter();
  custom->bltcon0 = DEST | A_TO_D;   custom->bltcon1 = 0;
  custom->bltafwm = -1; custom->bltalwm = -1; custom->bltadat = 0;
  custom->bltdmod = 0; custom->bltdpt = bltpt; custom->bltsize = bltsize;
}

/* Rellena el hueco con UN area fill XOR desde la ultima palabra del ultimo plano. */
static void BitmapFillFast(BitmapT *dst) {
  void *bltpt = dst->planes[0] + (dst->bplSize * DEPTH) - 2;
  u_short bltsize = (0 << 6) | (WIDTH >> 4);                  /* = 16 */
  WaitBlitter();
  custom->bltapt = bltpt; custom->bltdpt = bltpt;
  custom->bltamod = 0;    custom->bltdmod = 0;
  custom->bltcon0 = (SRCA | DEST) | A_TO_D;
  custom->bltcon1 = BLITREVERSE | FILL_XOR;
  custom->bltafwm = -1; custom->bltalwm = -1; custom->bltsize = bltsize;
  WaitBlitter();
}

/* Por cada arista visible (edgeColor > 0), linea ONEDOT+EOR replicada en cada plano
   con el bit puesto. Comunes seteados UNA vez; bltdpt SIEMPRE la base del bitmap. */
static void DrawObject(void *planes, Object3D *object) {
  _WaitBlitter();
  custom->bltafwm = -1; custom->bltalwm = -1;
  custom->bltadat = 0x8000; custom->bltbdat = 0xffff;
  custom->bltcmod = WIDTH/8; custom->bltdmod = WIDTH/8;
  do { while ((e = *group++)) {
    char edgeColor = EDGE(e)->flags;
    if (edgeColor > 0) {
      /* x0,y0,x1,y1 = vertices proyectados; si y0==y1 continue; si y0>y1 swap */
      dmax = abs(x1-x0); dmin = y1-y0;
      if (dmax >= dmin) { bltcon1 = (x0>=x1?AUL|SUD:SUD)|LINEMODE|ONEDOT; }
      else { bltcon1 = (x0>=x1?SUL:0)|LINEMODE|ONEDOT; swap(dmax,dmin); }
      bltcpt = (int)planes + (short)(((y0<<5)+(x0>>3)) & ~1);
      bltcon0 = rorw(x0&15,4) | BC0F_LINE_EOR;
      bltcon1 |= rorw(x0&15,4);
      dmin <<= 1; derr = dmin - dmax;
      bltamod = derr - dmax; bltbmod = dmin; bltsize = (dmax<<6)+66; bltapt = derr;
      /* DRAWLINE(): _WaitBlitter(); bltcon0/1=; bltcpt; bltapt; bltdpt = planes;
                     bltbmod; bltamod; bltsize;  (8 escrituras) */
      if (edgeColor & 1) DRAWLINE(); bltcpt += WIDTH*HEIGHT/8;
      if (edgeColor & 2) DRAWLINE(); bltcpt += WIDTH*HEIGHT/8;
      if (edgeColor & 4) DRAWLINE(); bltcpt += WIDTH*HEIGHT/8;
      if (edgeColor & 8) DRAWLINE();
    }
  } } while (*group);
}
```

Bucle por frame: `BitmapClearFast` -> transform/culling en CPU -> `DrawObject` -> `BitmapFillFast` -> `TaskWaitVBlank()` -> swap de buffer.

## Nuestro port (registros idénticos)

- `blitter_clear`: **1 blit** sobre los 4 planos contiguos (`blit_clear_region(dst, row_bytes, 0,0, words=16, h=1024)`; `bltcon0 = D=0`, `bltdmod = row_bytes - words*2`).
- `blitter_line_eor`: mismos `bltcon0/1`, octante, `bltamod=derr-dmax`, `bltbmod=dmin<<1`, `bltapt=derr`, `bltsize=(dmax<<6)+66`, descarta `y0==y1`; **`bltdpt = base del bitmap`** (truco replicado). Setea los comunes `bltafwm/alwm/adat/bdat/cmod/dmod` **por llamada** (el original los setea una vez por `DrawObject`).
- `blitter_area_fill`: `bltcon0=(SRCA|DEST)|A_TO_D`, `bltcon1=BLITREVERSE|FILL_XOR`, `bltamod=bltdmod=0`, semilla = última palabra del último plano, `bltsize=(0<<6)|16`.
- Transform/culling en C++ (`eng::object3d`, `div16`/`normfx` 4.12) en 4 pasadas: `update_object_transformation`, `update_face_visibility`, `update_edge_visibility_convex`, `transform_vertices`.

## Medidas exactas (emuladas, WinUAE-DBG con `cpu_cycle_exact=1`, `blitter_cycle_exact=1`)

- **Original**: ~**24.7 renders/s**, **~287,000 ciclos/render** (medido contando cambios del puntero `BPL1PT` de su copperlist y el contador de ciclos del monitor; `warp=0`).
- **Nuestro port**: ~**10.6 fps**, **~670,000 ciclos/frame**.
- **Solo CPU** (con clear/edges/fill comentados): **~286,000 ciclos/frame** (≈ lo mismo que el frame COMPLETO del original).

Desglose de nuestro port (contador de ciclos `0xB7E928` alrededor de cada sección):

| Sección | ciclos/frame |
|---|---|
| `clear` (1 blit, 4 planos) | 88,920 |
| `transform` + culling + visibilidad (CPU) | **176,152** |
| `edges` (contorno `ONEDOT`+EOR) | 147,648 |
| `fill` (area fill XOR, 1024 líneas) | **143,420** |
| `draw` (edges+fill) | 291,068 |
| `update` total | 556,344 |
| bucle/`render`/espera VB (resto) | ~114,000 |

## La paradoja que no sabemos resolver

Si el original hace el **mismo** `clear`+`fill` (mismos `bltsize`: anchura 16 words, altura 0 = 1024 líneas), en el mismo emulador debería pagar un coste similar al nuestro: `clear` 89k + `fill` 143k = **232k**, que es **casi todo** su frame de 287k. Eso dejaría solo ~55k para `transform` + `edges` + bucle, cuando nuestro `transform` solo ya son 176k. O el original no hace realmente esas operaciones completas, o nuestro coste de blit está inflado, o hay solape CPU/Blitter que no modelamos.

## Preguntas concretas

1. **¿`BLTSIZE` con campo de altura 0 significa 1024 líneas o 0 (no-op)?** Lo damos por 1024 porque nuestro `fill`/`clear` con altura 0 rellenan/limpian correctamente el bloque de 4 planos (imagen verificada, 0 % de huecos). ¿Es correcto en hardware real y en WinUAE?
2. **¿Es realista que `clear`+`fill` de 32,768 bytes cada uno cuesten ~89k+143k ciclos (~5.4 y ~8.75 ciclos/word)?** ¿O el original los superpone con la CPU (el Blitter corre en background) mientras que nuestro port serializa con `WaitBlitter`? Nuestro `blitter_line_eor` hace `wait_blitter()` al **inicio** de cada llamada y la siguiente línea reprograma registros; el `fill` espera al final. ¿Cómo organizaría el original el `TaskWaitVBlank`/`WaitBlitter` para esconder el Blitter?
3. **¿Cómo cabe `transform`+`edges` del original en ~55k** si nuestro transform C++ son 176k y nuestros edges 148k? ¿Es solo el asm optimizado del original (registros `register short *v asm("a3")`, `swap` para rotaciones, `muls/mulu` 16-bit) o hay una estructura distinta (p. ej. menos pasadas, saltarse caras, precalcular más)?
4. **¿Qué modelo de coste de Blitter usa WinUAE con `blitter_cycle_exact=1`** y cómo se compara con el hardware real? ¿El coste de un area fill `XOR` con `BLITREVERSE` y `bltamod=bltdmod=0` depende del contenido del buffer (número de bits a 1), y podría nuestro `fill` estar procesando más bits que el del original?
5. **¿Qué tres optimizaciones** nos acercarían más a 287k ciclos/frame sin cambiar la imagen? (p. ej. limitar `clear`/`fill` a la bounding box del objeto, reducir a 1 blit el contorno por arista multi-plano, transform en asm/16-bit, eliminar las 4 pasadas).

## Lo que pedimos

El mecanismo exacto por el que el original cabe en ~287k ciclos/render y el nuestro necesita ~670k, y una lista priorizada de cambios concretos para igualar el framerate conservando la imagen (que ya es idéntica: 0 % de huecos internos, IoU de máscara 93–97 % frente al original).
