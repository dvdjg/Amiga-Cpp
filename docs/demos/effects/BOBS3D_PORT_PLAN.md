# Porte de `effects/bobs3d` (demo 117)

Documento técnico del porte de `demoscene-repo-orig/effects/bobs3d/bobs3d.c` al engine:
qué hace el original, cómo se mapea, qué se verificó y qué coste tiene. La demo vive en
`demos/techniques/amiga/effects/117_bobs3d/`.

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
| `BitmapClearI` | `AmigaBackend::blitter_clear` | ✅ |
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
Corregido en `amiga.cpp`; vision confirma circulos limpios. Referencia:
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

**Sincronia de pantalla (flickering).** Con `run_frames` (update en la ISR) el render no
empieza alineado al VBlank; con **doble buffer** el dibujo del buffer destino empieza
~0,4 campos antes de que el Copper haga el swap => tearing.

Solucion adoptada (sin triple buffer): **2 buffers + `run_frames_polling`** (el `update`
arranca alineado al VBlank, como el original) **+ solapar el `clear` con el `transform`**
(`blitter_clear(..., wait=false)`; el `blitter_or_bobs_begin` espera despues), que es la
estructura de `DrawObject`. Vision sobre 8 frames: sin tearing ni frames incompletos.

Coste medido con esta configuracion: el trabajo real baja de ~354k a **~289k (2,04
campos)** porque el clear (~75k) se solapa con el transform (que sube 84 -> 92k por
contender con el Blitter). Como 289k > 2 campos (283.752), el bucle alineado **cuantiza a
3 campos = 16,7 fps**. Estamos a **~6k de caber en 2 campos = 25 fps**: bastaria recortar
~6k del transform o del bucle de BOBs. Quitar la instrumentacion `ENG_PROF` no cambia la
cuantizacion (medido: sigue en 3 campos).

El modo `run_frames` (IRQ, sin alinear) daba 19,3 fps pero exigia triple buffer. El usuario
prefirio la via sincronizada de 2 buffers.

## 7. Optimizaciones

Hecho:

1. **Batch de BOBs**: `AmigaBackend::blitter_or_bobs` fija las constantes del blit
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
- ✅ **Sincronia con 2 buffers** (elegida): `run_frames_polling` (update arranca alineado
  al VBlank) + `clear` solapado con el transform. Sin tearing (vision sobre 8 frames).
- ✅ **Objetivo 25 fps alcanzado reduciendo BOBs** (`K_117_MAXBLOBS=56` de 60): `update`
  = 282.322 ciclos (2,0 campos) = **25,13 fps**, sin tearing y con vision coherente.
  Barrido medido: 60->16,6 / 58->21,8 / **56->25,0** / 54-46->25,0 fps. Se recortan 4
  chispas (7 %) a cambio de la sincronia estricta de 2 campos.
- ✅ `run_frames` (IRQ) probado: 19,3 fps pero exige triple buffer; descartado por
  preferencia del usuario.
- ✅ Oraculo del original medido: **20,1 fps (2,49 campos/render)**, no 50.
- ⏭ Alternativa sin recortar BOBs: acortar ~2,5k del transform/bucle (rotacion
  incremental de `load_rotate`) para 60 BOBs en 2 campos.

### API de engine anadida

- `engine/include/eng/platform/amiga/blob.hpp`: `eng::amiga::OrBlobBatch` (lote OR
  intercalado `inline`, constantes fijadas una vez, sin `jsr` por objeto).
- `AmigaBackend::custom_registers()` (frontera unsafe para rutinas de lote inline).
- `AmigaBackend::blitter_or_bobs_begin/one/end` delegan en el mismo `OrBlobBatch` (una
  sola fuente de verdad para la secuencia de registros).
- `object3d::update_object_transformation_forward(Object3D&)`: matriz directa **sin** la
  inversa ni la camara (para efectos de solo proyeccion).

`OrBlobBatch` es una ruta **especializada** (mismo tamano, OR, intercalado, un lote
homogeneo); NO sustituye al camino general `bob.hpp`/`FramePlan` (cookie-cut, opaco,
save-under, tamanos distintos). Decision: **no migrar la 086** (usa el camino general y va a
49,92 fps sin regresion); `OrBlobBatch` queda para efectos tipo `bobs3d`.

---

## 9. Descubrimientos e interioridades (para reutilizar)

### 9.1 Secuencia de registros del BOB OR intercalado (`DrawObject`)

Un objeto = **UN blit** de `BOBH*planos` filas (96) x `BOBW/16` palabras (3), con las
constantes del lote fijadas una vez (`BLTCON1=0`, `BLTAFWM/BLTALWM=0xFFFF`, `BLTAMOD=0`,
`BLTBMOD=BLTDMOD=(WIDTH-BOBW)/8=26`) y por objeto solo `BLTCON0 = rorw(x&15,4)|SRCA|SRCB|
DEST|A_OR_B`, `BLTAPT`, `BLT(B/D)PT`, `BLTSIZE=(96<<6)|3`. Destino **intercalado**: el
puntero avanza 32 B por fila del blit, de modo que 96 filas cubren los 3 planos de 32
scanlines. Fuente **densa** (sin guarda) con `BLTAMOD=0`.

### 9.2 Reglas de fidelidad de registros (leccion BSH)

- **`BLTCON1` bits 15-12 = BSH (shift del canal B)**, no un duplicado de ASH. En un OR-BOB
  `B = D = destino`, asi que poner BSH != 0 desplaza la lectura del fondo y emborrona.
  Original: `bltcon1 = 0`. Se paga caro "normalizar" un registro sin leer su semantica.
- Anadir la primitiva que faltaba: `CopWaitSafe` **con H** (`wait_position_safe`).

### 9.3 Cadencia de frame y tearing (el punto mas sutil)

- `Engine::run_frames` corre `update`/`render` **dentro de la ISR de VBlank**; si `update`
  dura > 1 campo, la IRQ pendiente lo relanza **a media pantalla** (no alineado).
- Con **2 buffers** y render no alineado, el dibujo del buffer destino empieza antes del
  swap (recarga de `COP1LC` al VBlank) => **tearing**.
- El original usa 2 buffers y no parpadea porque `TaskWaitVBlank()` (al final de `Render`)
  **alinea el arranque del render al VBlank** y hace el swap en el VBlank: siempre dibuja
  el buffer que acaba de salir de pantalla. Su precio es cadencia entera de campos.
- **Solucion elegida (sin triple buffer)**: `run_frames_polling` (update alineado al
  VBlank) + **solapar el clear con el transform** (`blitter_clear(..., wait=false)`; el
  `begin` del lote espera despues), que es la estructura de `DrawObject`.
- **Triple buffer (fallback documentado)**: cuando la carga es tan alta que no cabe ni
  alineada, usar `kRing = 3` y `run_frames`. El buffer que se dibuja lleva >=2 swaps sin
  mostrarse, asi que no importa que el render no este alineado. Coste: +1 pantalla
  (24 kB) +1 copperlist y el tiempo de construirla; **rendimiento identico** (19,3 fps en
  bobs3d, porque el limite era el trabajo, no el swap). Regla: con `update` > 1 campo y
  sin alineacion se necesitan `ceil(update/campo) + 1` buffers. Aqui 3.

### 9.4 Donde se va el tiempo (paridad por componente con el original)

Medido con el **profiler del propio original** (`_DrawObject_profile`, `_TransformObject_profile`)
via canal lateral, y con `ENG_PROF` en el nuestro:

| Componente | Original | Ours (antes) | Ours (ahora) |
|---|---|---|---|
| transform | 83k | 92k | **83,7k** |
| bobs (mates + Blitter) | 194k | 202k | 197k |
| clear (solapado) | ~75k | 75k | 75k (oculto) |
| total real | ~2,0 campos | ~2,6 | ~2,0 |

### 9.5 Optimizaciones aplicadas (y su efecto)

1. **Lote OR inline** (`OrBlobBatch`): elimina `jsr` + 4 pushes por BOB. `blits` 231,7 ->
   202,5k.
2. **Fusionar calculo del vertice y programacion del blit** en un bucle (sin array).
3. **BLTPRI** (`DmaBlitterPriority`): 12,56 -> 16,70 fps.
4. **Solapar clear con transform**: trabajo real 354k -> ~289k.
5. **`update_object_transformation_forward`** (sin inversa ni camara; bobs3d no las usa):
   transform 92k -> 83,7k.
6. **Saltar `scale` cuando es identidad (1.0)** y **tabla `frame->origen`** (evita `z*18`).

`Proj::project` (backend `eng/cpu/m68k/affine.hpp`) ya es **optimo**: 1 `muls.w` por fila
empaquetada + 1 `muls.w` para `c2*z` (7 en total) + `xy` compartido + 2 `divs.w`, identico
al `MULVERTEX` del original. No hay nada que sacar ahi.

### 9.6 Restante

Medido con una seccion `update` que envuelve todo el frame (contador de ciclos del Amiga):

- `update` = **286.211 ciclos**; umbral de 2 campos = **283.752** => faltan **2.459**.
- Subsecciones: `clear` 1.848 (el Blitter va solapado) + `transform` 83.754 + `blits`
  196.643 + `install` 227 = **282.472**. El resto (~3,7k) es overhead del bucle de frame
  (P_BEGIN/P_END de instrumentacion, `mark_frame`, subspan, `frame*12`, `probe`).
- Quitar `ENG_PROF` no cambia la cuantizacion (sigue 3 campos): el trabajo real ya esta
  pegado al umbral.

Recortar ~2,5k daria **2 campos = 25 fps**, igualando/superando al original (20,1). Es el
1 % del frame. Palancas evaluadas:

- **Bajar el `clear` a la bbox de los BOBs**: descartado; el clear va **oculto** tras el
  transform (no esta en el camino critico) y los BOBs cubren casi toda la pantalla.
- **Afinar el bucle de BOBs**: el asm ya esta a paridad con `DrawObject` (196,6k vs 194,3k);
  sin grasa clara.
- **Reducir `load_rotate`** (medido aislado en una seccion propia = **2.676 ciclos**, 0,9 %
  del frame; recomponer `Rx·Ry·Rz` son ~11 `muls.w` + 6 lookups de tabla). Seria suficiente
  para devolver los 60 BOBs a 2 campos (el hueco era 2.459), pero **no hay via limpia**:
  (a) la rotacion **no es subgrupo de 1 parametro** (`Rx(θ)Ry(θ)Rz(θ)` con los tres angulos
  iguales, pero cambiar los tres a la vez no es un post-producto constante), asi que la
  rotacion incremental no aplica; (b) una tabla de 4096 matrices = 72 kB (demasiado); (c)
  el eje fijo ahorra ~1k como mucho. Por eso se prefirio el recorte de BOBs.
- **Reducir BOBs (`K_117_MAXBLOBS`)**: adoptado. Barrido medido (2 buffers, polling
  alineado):

  | BOBs | campos/frame | fps |
  |---|---|---|
  | 60 | 3,0 | 16,6 |
  | 58 | 2,3 | 21,8 |
  | **56** | **2,0** | **25,0** |
  | 54..46 | 2,0 | 25,0 |

  Con **56 BOBs** (de 60) el `update` cabe en 2 campos (25 fps) manteniendo el sincronismo
  estricto. Se pierden 4 chispas; vision confirma esfera coherente y sin glitches.

### 9.7 Como reproducir la medicion del original (oraculo)

1. `out/tmp/oracle.uae`: `runner.uae` + `floppy0=<bobs3d.adf>` + `joyport0=mouse` (sin esto
   `LeftMouseButton` sale pulsado y el efecto termina al primer frame) + `warp=true`.
2. Lanzar con `WinUAEConnection` (GDB para arrancar) y leer memoria por el **canal lateral**
   `mem <addr> <len>` (responde aunque la CPU del original duerma en `TaskWaitVBlank`; GDB
   no).
3. Localizar la base de relocalizacion buscando la cadena `"Bobs3D"` y el puntero a ella
   (`Bobs3DEffect.name`). Los fps reales NO se miden con `rotate` (va con la CIA a 50 Hz):
   usar `ProfileT.count` de `_DrawObject_profile` (offset +16), que incrementa por render.
