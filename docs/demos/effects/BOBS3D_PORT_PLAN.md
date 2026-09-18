# Porte de `effects/bobs3d` (demo 117)

Documento técnico del porte de `demoscene-repo-orig/effects/bobs3d/bobs3d.c` al engine:
qué hace el original, cómo se mapea, qué se verificó y qué coste tiene. La demo vive en
`demos/amiga/117_bobs3d/`.

## 1. La rebanada

```
effects/bobs3d/bobs3d.c
effects/bobs3d/data/{pilka.c, flares32.c, carrion-metro-data.c, carrion-metro-pal.c}
include/{effect.h, blitter.h, copper.h, 3d.h, fx.h, gfx.h, bitmap.h, pixmap.h, cdefs.h, common.h}
lib/lib3d/*     (Object3D, matrices, proyeccion, NewObject3D)
lib/libgfx/*    (Bitmap, CopList, SetupMode/DisplayWindow/BitplaneFetch, LoadColors)
lib/libmisc/*   (fx, normfx, div16)
system/*        (arranque, bucle de efecto, vblank)
```

## 2. Qué hace el original

Un objeto `pilka` (malla `obj2c` de 60 vertices) rota y **cada vertice proyectado se
dibuja como un BOB OR intercalado** (chispa de 48x32x3), no como poligono. El fondo es un
segundo playfield ("carrion-metro", 256x256x2) con la paleta reescrita **por linea** por el
Copper.

### Display (doble playfield)

```
SetupMode(MODE_DUALPF, DEPTH + carrion_depth) = 3 + 2 planos
BPLCON0 = 0x5600   (BPU=5 + DBLPF + COLOR)
BPL1MOD = WIDTH/8 * (DEPTH-1)          = 32 * 2 = 64
BPL2MOD = WIDTH/8 * (carrion_depth-1)  = 32 * 1 = 32
DIWSTRT/DIWSTOP = 0x2CA1   DDFSTRT/DDFSTOP = 0x48/0xC0
punteros intercalados: BPL1/3/5PT = screen planos 0/1/2 ; BPL2/4PT = carrion planos 0/1
```

El Copper intercala los punteros para que PF1 (planos impares hardware) sea `screen` (3
planos, colores 0..7) y PF2 (pares) sea `carrion` (2 planos, colores 8..11). Por eso
`BPL2PT=carrion.planes[0]` y `BPL4PT=carrion.planes[1]`.

### Paleta por linea (`MakeCopperList`)

```
CopMove16(color[0], carrion_cols_pixels[0]);
CopWait(Y(-1), HP(0));  CopMove32(BPLxPT, ...) x5
for i in 0..255:
  bgcol = carrion_cols_pixels[4i+0]
  CopWaitSafe(Y(i-1), X(288));  CopMove16(color[0], 0)
  CopWaitSafe(Y(i),   X(0));    CopMove16(color[9],  [4i+1])
                                CopMove16(color[10], [4i+2])
                                CopMove16(color[11], [4i+3])
                                CopMove16(color[0], bgcol)
```

`SetupMode` deja `BPLCON2 = PF1P_BOTTOM | PF2P_BOTTOM`. La tabla
`carrion_cols_pixels` tiene 4 valores por linea: `[bgcol, color9, color10, color11]`.

### Transform (`TransformVertices`)

Misma identidad empaquetada que `wireframe`: por fila, `(c0+y)·(c1+x) + c2·z − x·y`, con
`>>4` en filas x/y y `normfx` (`>>12`) en la fila z. Recorre **todos** los vertices (sin
culling), lee de `POINT(i)`, escribe en `VERTEX(i)` y guarda `(xp/zp, yp/zp, zp)`.

### Dibujo (`DrawObject`)

Por vertice visible: `x -= 16`, `y -= 16`; `z = ((z>>4) + 256 + 96)*3 − 32`, redondeo a 32 y
recorte a `[0, 480]`; la chispa se elige como `frame = z/32`. Un **unico blit** de
`BOBH*DEPTH` filas (96) x `BOBW/16` palabras (3) con `A_OR_B`, `bltcon1=0`,
`bltafwm=bltalwm=-1`, `bltamod=0`, `bltbmod=bltdmod=(WIDTH-BOBW)/8=26`, fuente en `apt` y
destino `bpt=dpt`, y `bltcon0 = rorw(x&15,4) | SRCA|SRCB|DEST | A_OR_B`. Limpia la pantalla
activa con un solo blit (`BitmapClearI`, 768 filas x 16 palabras, `D=0`).

## 3. Mapeo al engine

| Original | Engine | Estado |
|---|---|---|
| `Object3D`/`Mesh3D`/macros `NODE3D/POINT/VERTEX` | `eng::object3d` (`object3d.hpp`) | ✅ |
| `TransformVertices` (empaquetado 4.12) | `eng::math::projector` (`affine.hpp`) | ✅ |
| `DrawObject` (BOB OR intercalado, 1 blit) | `eng::graphics::bob` + `FramePlan` | ✅ (ver §5) |
| `MakeCopperList` (DPF + paleta por linea) | `eng::copper::Scheduler` | ✅ |
| `CopWaitSafe(Y(i), X(0))` | `Scheduler::wait_position_safe` | ➕ anadido (port de CopWaitSafe **con H**) |
| `BitmapClearI` | `MinimalBackend::blitter_clear` | ✅ |
| assets `data/*.c` | copiados verbatim a `src/data/` | ✅ |

La re-encodificacion del atlas de BOBs (de filas densas de 6 B a filas de 8 B con palabra
de guarda) es lo unico que se transforma: el contrato de `bob.hpp` exige la guarda. No
cambia los pixeles visibles.

## 4. Interruptores de diagnostico

`-DK_117_BG=0` deja el fondo en negro (solo BOBs); `-DK_117_BOBS=0` deja solo el fondo. Son
la base de la validacion incremental con vision (`K_117_*`).

## 5. Fidelidad: el bug de `BLTCON1` (BSH)

Primera version: los BOBs salian con "colas" horizontales. Aislado por capas (fondo solo =
limpio) y confirmado con el modelo de vision local. Causa: el backend OR-BOB ponia
`BLTCON1 = shift<<12`, pero los bits 15-12 de `BLTCON1` son **BSH** (shift del canal B), no
un duplicado de ASH. Como en el OR-BOB `B = D = destino`, BSH desplazaba la lectura del
fondo y emborronaba. El original deja `bltcon1 = 0` y solo desplaza A via `BLTCON0`.
Corregido en `amiga_minimal.cpp`; vision confirma circulos limpios. Referencia:
`amiga-bootcamp/08_graphics/blitter_programming.md` ("Shift and Alignment", BLTCON1/BSH).

## 6. Rendimiento medido (A500_debug, `-O1`)

Frame PAL = 141.876 ciclos. Conteo por secciones (`ENG_PROF_*`, contador del Amiga):

| Variante | ciclos/frame | fps | lectura |
|---|---|---|---|
| Solo fondo (`K_117_BOBS=0`) | 284.204 | 24,96 | 2 campos: clear + transform ya no caben en 1 |
| Solo BOBs, fondo negro (`K_117_BG=0`) | 712.878 | 9,95 | los BOBs anaden ~428k |
| Efecto completo, camino generico (`K_117_BATCH=0`) | 712.878 | 10,08 | release (`-O2`) da lo mismo: no es la optimizacion del C |
| Efecto completo, lote de BOBs (`K_117_BATCH=1`) | 564.668 | 12,56 | −21 % frente al camino generico |

Reparto del efecto con el **lote de BOBs** (constantes fijadas una vez, atlas denso, 3
palabras fieles; `blitter_or_bobs`):

| Seccion | ciclos/frame | % | c/BOB | lectura |
|---|---|---|---|---|
| `clear` | 82.914 | 14,7 | — | 12.288 palabras D=0; limitado por DMA de bitplanes |
| `transform` | 83.658 | 14,8 | ~1.394/vertice | matriz + proyeccion |
| `draw` | 45.673 | 8,1 | ~761 | construir las entradas del lote |
| `blits` | 261.846 | 46,4 | ~4.364 | Blitter + espera, limitado por bus |
| `install` | 275 | 0,0 | — | swap de copperlist |

Comparativa del lote frente al camino generico (`bob.hpp` + `FramePlan`): `draw` 159.218 →
45.673 (−71 %) y `blits` 371.899 → 261.846 (−30 %, por las 3 palabras y menos escrituras de
registro).

Conclusion: el efecto esta **limitado por bus/Blitter**, no por el `C`. El `clear` (83k para
12.288 palabras D) y los `blits` (~5,4 ciclos/palabra-evento) confirman contencion con el
DMA de bitplanes (5 planos x 256 px). Para 25 fps (2 campos = 283.752) el `update` debe
caber en 2 campos; hoy son ~4 (564.668).

### Con BLTPRI y lote (medido con el canal lateral)

Activado `BLTPRI`/`BLITHOG` (`DMACON` bit 10) como el original, el total baja a
**424.614 ciclos/frame (2,99 campos, 16,7 fps)**:

| Seccion | ciclos/frame | % | lectura |
|---|---|---|---|
| `clear` | 75.682 | 17,8 | 12.288 palabras D=0; **6,2 ciclos/palabra** |
| `transform` | 84.755 | 20,0 | 60 vertices |
| `draw` | 46.077 | 10,9 | entradas del lote |
| `blits` | 194.297 | 45,8 | 60 BOBs; **3,7 ciclos/canal-palabra** |
| `install` | 286 | 0,1 | swap |

Aislado: **solo `clear` = 50,09 fps** (el clear cabe en un campo), **solo
`transform`+`draw`+`blits` = 16,58 fps**.

### Oraculo: el original va a ~20 fps (no 50)

**Ojo con la metrica.** `object->rotate.x/y/z = frameCount * 12`, y `frameCount` es el
contador de frames de la **CIA (50 Hz)**, no un contador de renders. Si `Render` corre cada
M campos, `rotate` salta `12*M` cada M campos, es decir **12/campo siempre**: la pendiente
de `rotate` NO mide el framerate. Una primera version del oraculo midio esa pendiente e
informo erroneamente 49,86 fps.

La medida correcta usa el profiler del propio original (se compila con `PROFILER`; el map
enlaza `profiler.o`): `_DrawObject_profile.count` incrementa una vez por render. Leido por
el canal lateral (`mem`, que responde aunque la CPU duerma en `TaskWaitVBlank`):

```
campos=474,2  renders=192  ->  20,24 fps  |  350.439 ciclos/render (2,47 campos/render)
DrawObject (60 BOBs): 429 lineas/render = 1,37 campos ~ 194k ciclos
```

Conclusion: **el original va a ~20 fps (2,47 campos/render)**, no a 50. Su `DrawObject`
(los 60 BOBs, setup + Blitter) cuesta ~194k ciclos, **practicamente igual que nuestros
`blits` (194-195k)**. La premisa de "el original a 50 fps" no se sostiene con esta medida.

Verificacion de registros en runtime: `DMACONR=0x47c0` (BLTPRI), `BPLCON0=0x5600`,
`BPL1MOD=0x40`, `BPL2MOD=0x20`, `DIW=0x2CA1`, `DDF=0x48/0xC0` — **identicos a los
nuestros**. La secuencia de registros del blit del lote es identica a `DrawObject`.

### Comparacion componente a componente (original vs nosotros)

Medida del original con el mismo profiler (`ProfileT.total`, en lineas; 313 lineas/campo,
453 ciclos/linea):

| Componente | Original | Nosotros | Diferencia |
|---|---|---|---|
| `TransformObject` / `transform` | 183 lineas = **83.072 c** | 84.563 c | **≈ igual** |
| `DrawObject` / bucle de BOBs (mates + Blitter) | 428 lineas = **194.266 c** | 231.706 c | **+37k (+20 %)** |
| `clear` (no perfilado en el original) | ~76k (resto del frame) | 76.569 c | ≈ igual |
| total | 2,49 campos (20,1 fps) | 3,0 campos (16,7 fps) | ~1,2x |

Conclusiones:

- **El setup por BOB no es el problema**: con 1 fila por BOB `blits` cae a 30k => ~500
  ciclos/BOB de coste fijo.
- **El Blitter ni su configuracion no son el problema**: los registros coinciden y el coste
  de datos coincide.
- **El `transform` no es el problema**: ya esta a la par (84,5k vs 83k). La nota de `lib3d`
  (~57k) era de otro efecto; para bobs3d el original mide 83k.
- El unico hueco real es el **bucle de BOBs** (matematicas del vertice + programacion del
  blit): 232k vs 194k, +20 %. Es codigo C++ (aritmetica de punteros, ramas, calculo de
  `x/y/z/frame`) frente al C afinado del original.

Optimizaciones aplicadas al bucle:

1. **Fusion** del calculo del vertice y la programacion del blit en un solo bucle
   (`draw_bobs_stream`, sin array intermedio): `draw` 46k -> 0.
2. **Lote `inline`** (`eng::amiga::OrBlobBatch` en `blob.hpp`): elimina el `jsr` + 4 pushes
   por BOB. `blits` 231,7k -> **202,5k**.

Estado final medido: `clear` 75k, `transform` 84k, `blits` **202,5k**, total ~425k
(3,0 campos, 16,6 fps). Verificado en el `.s`: 0 llamadas a `blitter_or_bobs_one` (todo
inline). Queda un `jsr __mulsi3` en `blitter_clear` (`row_bytes*h == plane_bytes`, una vez
por frame), despreciable.

**Paridad por componente**: transform 84k vs 83k del original; bobs 202,5k vs 194k; clear
~75k vs ~76k. El trabajo real por frame es ~360k (2,54 campos) en ambos lados. La unica
diferencia que queda (nuestro 16,6 fps vs 20,1 del original) es la **cuantizacion de
campos** de `run_frames_polling` (2,54 campos => salta a 3), mientras el original, guiado
por el contador de la CIA, promedia 2,49 campos. No es coste de algoritmo.

Para igualar el framerate hay que cambiar el **bucle de frame** del engine, no optimizar
mas el efecto. Probado: con `Engine::run_frames` (interrupt-driven) en vez de
`run_frames_polling`, el total baja a **366.793 ciclos/frame (2,6 campos, 19,34 fps)**,
practicamente el original (2,49 campos, 20,1 fps), y vision confirma que sigue limpio.
El polling cuantiza a 3 campos porque `2,54 > 2`; la cadencia por IRQ no cuantiza.
La demo usa `run_frames` por defecto (`K_117_IRQ=1`). Batir al original exigiria reducir
Blitter/clear, que ya estan a su nivel.

**Nota de modelo (importante).** El original **no** ejecuta `Render` dentro de la ISR: el
`EFFECT` pasa `VBlank = NULL` y `Render` corre en la **tarea de primer plano**, que
`TaskWaitVBlank()` **duerme** hasta que la IRQ de VBlank la despierta (`TaskNotifyISR`).
Es cadencia por IRQ con ejecucion en primer plano. `Engine::run_frames` ejecuta
`update`/`render` **dentro** de la ISR de VBlank (variante cercana, no identica); da la
misma cadencia en la practica. `run_frames_polling` es la otra alternativa (busy-wait en
`VPOSR`), que es la que cuantiza a campos enteros. Para efectos que no caben en 1 campo,
`run_frames` es la cadencia correcta.

**Sincronia de pantalla (flickering).** Con `run_frames` el `update` dura ~2,6 campos y no
empieza alineado al VBlank. Con **doble buffer**, el dibujo del buffer destino empieza
~0,4 campos antes de que el Copper haga el swap (recarga de `COP1LC` al VBlank) => se
dibuja/borra un buffer que aun se muestra, y aparece tearing. El original lo evita porque
`TaskWaitVBlank()` alinea el inicio del render al VBlank (con 2 buffers). Nuestro
`update`, al durar >2 campos, no puede. Solucion: **triple buffer** (`kRing = 3`): el
buffer que se dibuja lleva >=2 swaps sin mostrarse, sin solape. Medido: 19,24 fps
(2,6 campos) y vision no detecta frames incompletos ni tearing. Raspar ~10k no lo
arreglaria: el trabajo seguiria por encima de 2 campos.

## 7. Optimizaciones

Hecho:

1. **Batch de BOBs**: `MinimalBackend::blitter_or_bobs` fija las constantes del blit
   (`BLTCON1/AFWM/ALWM/AMOD/BMOD/DMOD/BLTSIZE`) UNA vez y por BOB solo escribe
   `BLTCON0/APT/BPT/DPT` + espera, como el `DrawObject` original.
2. **3 palabras fieles**: atlas denso (sin guarda) y `BOBW/16 = 3` palabras, `bltcon1=0`.
3. **Constantes por objeto fuera del bucle**: `words/height/modulos/frame` ya no se
   recalculan por BOB.

Pendiente / descartado con la evidencia actual:

4. **Escalado a asm**: la seccion dominante es `blits` (46 %), que es bus/Blitter, no
   instrucciones. Portar a asm `transform`/el bucle no reduciria el bus; queda **no
   necesario** salvo que aparezca evidencia de CPU pura.
5. **Oráculo**: el ADF original **arranca y renderiza** (captura `out/tmp/oracle_shot.png`),
   pero su medicion de fps quedo bloqueada: el exe se **relocaliza** al cargarlo AmigaDOS
   (base runtime distinta de la enlazada) y las lecturas GDB exigen pausar la CPU. Falta
   localizar la base (p. ej. por la cadena `Bobs3D` o el puntero de `Bobs3DEffect.name`) y
   leer `object->rotate.x`/`frameCount`. Hasta tenerlo, el objetivo real de este efecto
   (2 vs 3-4 campos) no esta fijado.

## 8. Estado

- ✅ Port fiel que reproduce el efecto (READY; vision: esfera de chispas girando sobre
  carrion-metro, circulos limpios, sin bandas ni costuras; revalidado tras la fusion y el
  lote inline).
- ✅ Bug de fidelidad de registros (BSH, `BLTCON1`) corregido en el backend; 086 sigue a
  50 fps sin regresion.
- ✅ Assets importados verbatim; atlas re-encodificado a la guarda de `bob.hpp` y copia
  densa para el lote.
- ✅ **BLTPRI** activado (como el original): 12,56 → 16,70 fps.
- ✅ **Lote de BOBs fusionado e inline** (`OrBlobBatch`): `blits` 231,7k → 202,5k.
- ✅ **Paridad por componente** con el original (medido con su propio profiler):
  transform 84k vs 83k, bobs 202,5k vs 194k, clear 75k vs ~76k.
- ✅ **Cadencia por IRQ** (`Engine::run_frames`): **19,3 fps (2,6 campos)** — practicamente
  el original (20,1 fps, 2,49 campos). El polling daba 16,6 (3,0 campos).
- ✅ **Triple buffer** (`kRing = 3`): elimina el tearing del doble buffer con `update` >2
  campos. Vision confirma imagenes completas sin desgarro. Mismo rendimiento (19,2 fps).
- ✅ Oraculo del original medido: **20,1 fps (2,49 campos/render)**, no 50.
- ⏭ Unico margen: reducir Blitter/clear (ya al nivel del original) para bajar de 2,5 campos.

### API de engine anadida

- `engine/include/eng/platform/amiga/blob.hpp`: `eng::amiga::OrBlobBatch` (lote OR
  intercalado `inline`, constantes fijadas una vez, sin `jsr` por objeto).
- `MinimalBackend::custom_registers()` (frontera unsafe para rutinas de lote inline).
- `MinimalBackend::blitter_or_bobs_begin/one/end` delegan en el mismo `OrBlobBatch` (una
  sola fuente de verdad para la secuencia de registros).
