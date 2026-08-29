# README_104 — Scroll por hardware en dual playfield con superficie en anillo

Esta demo (`demos/104_tile_scroll_ring_dualpf`) enseña cómo se hace **scroll por
hardware** en un Amiga OCS a bajo nivel, en su variante de **superficie en anillo**
pequeña (352x288) que se recicla al cruzar el borde (estilo Lionheart), usando
**dual playfield (DPF)** para dos capas con transparencia.

No es un tutorial teórico: cada sección referencia el código real de
`src/main.cpp` (las líneas pueden variar ligeramente) y explica el "por qué" de
cada decisión, las matemáticas y cómo se optimiza para que el 68000 haga lo
mínimo posible y el chipset haga el resto.

---

## 1. El Amiga a bajo nivel: los tres cerebros

Un Amiga OCS (A500) es en realidad tres chips trabajando a la vez:

| Chip | Papel |
|---|---|
| **68000 (CPU)** | Ejecuta el código. Calcula punteros, encola blits, escribe registros. **No dibuja**. |
| **Agnus** | Controla el **DMA**: lee de Chip RAM los bitplanes, ejecuta la **copperlist**, atiende al **Blitter**, refresca memoria. |
| **Denise** | Convierte los bits de los bitplanes en píxeles de color (RGB444). |
| **Paula** | Audio, floppy, serie, timers. No nos interesa aquí. |

La clave: **el scroll no lo hace la CPU**. La CPU solo escribe unas pocas
palabras en los registros custom (`$DFF000`) y el chipset se encarga de leer los
píxeles correctos en cada scanline. En esta demo la CPU calcula desplazamientos,
recompila una copperlist y encola blits; **el movimiento en pantalla lo hace
Agnus leyendo memoria**.

Ejemplo en el código: `main()` (línea 620) solo arranca el engine. El trabajo
real está en `DemoGame::init`, `update` y `render`.

---

## 2. La memoria: Chip RAM y los registros custom

- **Chip RAM**: memoria que Agnus puede leer (bitplanes, copperlist, buffers de
  blit). Sin ella, Denise no puede mostrar nada.
- **Registros custom**: un bloque de 256 words en `$DFF000`. La CPU los escribe
  como si fueran memoria; Agnus los interpreta (punteros BPLxPT, BPLCON0,
  BPLCON1, etc.).

`configure_memory({120 * 1024, 16 * 1024, 8 * 1024})` (línea 370) reserva
**120 KB de Chip RAM** para esta demo. Reparto aproximado:

| Bloque | Tamaño | Código |
|---|---|---|
| Superficie (bitplanes) | 6 planos × 44 × 288 = 76 KB | `m_scene.init` |
| Scratch del shift | 42 × 288 = 12 KB | `m_scratch` (línea 388) |
| Caché de tiles (fg+bg) | 64×2 × 96 B = 12 KB | `TileCache::build` |
| Copperlist | 1.5 KB | `config` (línea 375) |

Optimización de memoria: la superficie es **352x288 = 76 KB**, frente a los
**245 KB** de las demos 101/102 (640x512). El ahorro es la razón de ser del
anillo: un buffer pequeño que se recicla.

---

## 3. Dual playfield (DPF): dos capas de color

### 3.1 Qué es DPF

En modo DPF, el hardware agrupa los bitplanes en **dos playfields** con paletas
independientes y prioridad:

- **PF1** = bitplanes **1, 3, 5** (índices 0, 2, 4). Usa los colores **0-7**.
- **PF2** = bitplanes **2, 4, 6** (índices 1, 3, 5). Usa los colores **8-15**.

Se activa con el **bit 10 de BPLCON0** (`0x0400`). Véase
`TileScrollScene::set_scroll` en `engine/.../tile_scroll.hpp`:

```cpp
bplcon0 = 0x0200 | (plane_count << 12) | (dual ? 0x0400 : 0);
```

En esta demo el modo se elige con `TileScrollMode::dual(3, 3)` (línea 35):
**3+3 = 6 bitplanes**, PF1 de 3 planos y PF2 de 3 planos. Ambos tienen **8
colores** (índices 0-7 y 8-15). La paleta está en `dual_palette` (línea 52):
PF1 = colores vivos (0=f0c, 1=0cf, ...), PF2 = verdes de fondo.

### 3.2 La transparencia (lo que hace visible el parallax)

En DPF, el **color 0 de cada playfield es transparente**:

- Donde PF1 (delante) tiene color 0 → se ve PF2.
- Donde PF1 tiene color 0 **y** PF2 también → se ve el borde (color 0 global).

La función `pf_plane_row` (línea 121) genera los tiles. Para el primer plano
(`transparent_bg = true`) el fondo del tile es un **tramado al 50%** (línea 138):

```cpp
const eng::u16 checker = (y & 1) == 0 ? 0xaaaa : 0x5555;
```

El patrón `0xaaaa`/`0x5555` alterna bits pares/impares: la mitad de los píxeles
del fondo del tile tienen color (el del playfield) y la otra mitad quedan en
color 0 (transparente) → el fondo PF2 se ve entre medias. Es un **dithering**
barato (una máscara de bits por fila, sin coste por píxel).

### 3.3 La prioridad

`BPLCON2` bit 6 (`0x40`) pone PF2 delante. La fábrica del modo lo calcula:
`foreground_is_pf2` se decide en `TileScrollMode::dual` (en `tile_scroll.hpp`).
En 3+3 con PF1 delante, `bplcon2 = 0` (PF1 delante). Aquí el primer plano es PF1
(`pf_foreground = 0`, línea 50).

---

## 4. Scroll por hardware: punteros de bitplane + BPLCON1

### 4.1 La idea

El scroll horizontal en OCS se consigue combinando:

1. **`BPLxPT`** (punteros de bitplane): apuntan al comienzo del fetch de cada
   plano, alineado a **word** (16 píxeles).
2. **`BPLCON1`**: desplaza el playfield **0-15 píxeles** dentro de la word (el
   "fine scroll").

La fórmula canónica del driver (en `set_scroll`, `tile_scroll.hpp`) es:

```cpp
fine   = scroll_x & 15;
coarse = (scroll_x - 1) & ~15;      // múltiplo de 16, ≥ 0
BPLCON1 = (16 - fine) & 15;
```

Con `DDFSTRT = $30` (fetch adelantado) se cumple la invariante
**`display_start == scroll_x`**: por cada píxel que avanza `scroll_x`, la imagen
se mueve un píxel, sin saltos en el cruce de tile (ver README de la demo 101).

### 4.2 El modulo del fetch

Denise no lee la superficie como un bitmap contiguo: cada **scanline** fetchea
`fetch_bytes_per_row` bytes (42 aquí) y avanza a la siguiente fila saltando
`BPLxMOD` bytes. El "modulo" es el espacio entre el final de un fetch y el
principio del siguiente:

```cpp
surface_bytes_per_row = 352 / 8 = 44
fetch_bytes_per_row   = 320 / 8 + 2 = 42
display_modulo        = 44 - 42 = 2
```

Es decir: cada scanline lee 42 bytes (la ventana de 320 px + 16 de margen) y
salta 2 bytes para llegar a la siguiente fila. Esto permite que la superficie
sea **más ancha que la ventana** sin que Denise se "salga" de la fila.

### 4.3 El puntero

```cpp
pointer_offset = cam_y * surface_bytes_per_row + (coarse / 8);
```

El puntero `BPLxPT` del plano `p` se escribe en la copperlist como
`m_bitplanes + p * plane_bytes + pointer_offset`. Lo hace `rebuild_copper`
(sección 7).

---

## 5. La superficie en anillo: 352x288

### 5.1 Por qué tan pequeña

Un scroll lineal infinito necesitaría un buffer tan grande como el rango de
scroll (320 px → 640 de ancho, como en 101/102). Eso son ~245 KB.

El anillo usa un buffer de **solo 22×18 tiles** (352x288) = **76 KB**. La ventana
visible es 320x256 (20×16 tiles). Sobran **2 tiles de margen** por eje.

### 5.2 Cómo se lee el anillo

Denise lee la superficie de forma lineal con el puntero `BPLxPT`. El truco: la
cámara **nunca apunta más allá de los primeros 16 px de cada fila** (`px ∈ [1,16]`).
El desplazamiento fino lo hace `BPLCON1`, y cuando `px` llegaría a 17, en vez de
seguir (que se saldría del buffer), el buffer se **recicla**: se desplaza el
contenido 16 px al lado contrario y se recargan los tiles nuevos que entran.

La cámara del anillo está en `RingCamera` (línea 217):

```cpp
wx = 1;  // posición de mundo en píxeles (crece sin límite)
px = (wx % 16) + 1;   // puntero fino dentro del buffer: 1..16
view_x = wx / 16;      // columna de mundo en el borde izquierdo del buffer
```

La función `derive()` (línea 338) separa el mundo (`wx`) en el puntero (`px`) y
la base (`view_x`). Cuando `view_x` cambia, `vdx`/`vdy` marcan que hay que hacer
el wrap.

### 5.3 El layout de la superficie

- `surface_bytes_per_row = 44` bytes (352 px).
- `plane_bytes = 44 × 288 = 12.672` bytes por plano.
- 6 planos → `bitplane_bytes = 76.032` bytes.

Cada tile ocupa `16 × 16` px = 2 bytes por fila, 32 bytes por plano, 192 bytes
por tile (6 planos).

---

## 6. La cámara de anillo: fases y el modo de saltos

`RingCamera::advance` (línea 241) decide entre:

- **Fases** (`advance_phases`, línea 287): recorridos de 1 px/frame —
  horizontal, vertical, diagonal, circular y senoidal, con pausas. La cámara
  **no se acumula**: cada frame se recalcula la posición desde el frame_index
  y el comienzo de la fase (`m_phase_wx`/`m_phase_wy`), lo que la hace
  determinista y sin deriva de punto flotante.
- **Modo de saltos** (`advance_jump`, línea 326): tras `jump_start` frames,
  avanza pasos aleatorios de 2-15 px (un **xorshift32**, `rng_next` línea 355)
  y re-elige las direcciones cada `repattern_frames`. 

Las tablas circulares (`circle_offset_x/y`, líneas 251/265) evitan `sin/cos`
de libm: un **lookup de 64 pasos** precalculado. Multiplicar por el radio se
hace con enteros: `96 * offset / 64` (`radius_signed`, línea 283).

---

## 7. El wrap: el corazón del anillo

Cada vez que la cámara cruza un borde de tile (`vdx != 0 || vdy != 0`), en
`update` (línea 431) se encola el wrap:

```cpp
add_shift(plan, vdx, vdy);                    // desplaza el buffer 16 px
add_edges(plan, bg, pf_background, vdx, vdy); // recarga la columna/fila nueva
add_edges(plan, fg, pf_foreground, vdx, vdy);
```

### 7.1 El shift del buffer

Cuando la cámara avanza 16 px y `px` pasa de 16 a 1, el contenido debe moverse
16 px al lado contrario para que Denise siga viendo la misma imagen continua
(la invariante `display_start == scroll_x`).

Matemáticamente: se copia `[16 px, 352 px) → [0, 336 px)` en cada fila (42 bytes
por fila, 288 filas). Un solo blit de rectángulo **solapado** (origen y destino
en el mismo buffer) sería lo lógico, pero **WinUAE-DBG no mueve el buffer en
copias solapadas** (verificado con un control por CPU y tracking de píxeles).

La solución robusta en `add_shift` (línea 473): **dos blits no-solapados** por
plano usando un scratch:

```
1) superficie → scratch   (42 bytes/fila contiguos)
2) scratch    → superficie (con el desplazamiento)
```

- Blit 1: fuente `base + plano*plane_bytes + 2` (16 px = 2 bytes), destino
  `scratch`, 21 words/fila, 288 filas, modulo de fuente = 2 (44-42).
- Blit 2: fuente `scratch` (contiguo, modulo 0), destino `base + plano*...`,
  modulo de destino = 2.

El scratch (12 KB, línea 388) solo necesita **una fila-plano** porque se
reutiliza plano a plano.

### 7.2 La recarga de bordes (`add_edges`, línea 522)

Después del shift, el tile de la columna/fila que entra es **nuevo** (viene del
mapa del mundo), no una copia. `add_edges` dibuja, para cada playfield:

- Si `vdx > 0` (avanza derecha): la columna de mundo `view_x + 21` en el slot 21.
- Si `vdx < 0`: la columna `view_x` en el slot 0.
- Análogo para filas.

Usa `make_playfield_upload_jobs` que genera **un solo job por tile** con
`destination_plane_stride = 2 * plane_bytes` (los planos de un playfield están
intercalados: PF1 = 1,3,5). Fusionar los planos reduce el overhead por blit
(wait + registros) de 3× a 1×.

### 7.3 La geometría exacta del shift

- `width_bytes = 44 - 2 = 42` (el copy cubre `352 - 16 = 336 px`).
- `words_per_row = 42 / 2 = 21`.
- `modulo = 44 - 42 = 2`.
- `src_dx = 2` si `vdx > 0` (avanza derecha → el contenido se mueve 16 px a la
  izquierda), `dst_dx = 2` si `vdx < 0`.
- Para diagonal: `src_dy = 44 * 16` (16 filas), `height = 288 - 16 = 272`.

**Error típico que ya se corrigió**: usar `surface_width - tile_size = 336`
como si fueran **bytes** daba `words_per_row = 168` (8× de más) y corrompía el
buffer. Los 336 **píxeles** son 42 **bytes** = 21 **words**.

---

## 8. El bucle del engine y la copperlist

`engine.hpp` ejecuta por frame:

```
update -> wait_vblank -> render
```

### 8.1 `update` (línea 413)

1. `m_cam.advance(frame)` → avanza la cámara, calcula `px/py` y `vdx/vdy`.
2. Si hay wrap (`vdx/vdy != 0`): recompila el `FramePlan` (shift + edges) y lo
   ejecuta por Blitter.
3. `rebuild_copper(input)` → escribe la **copperlist** con los punteros BPLxPT
   y BPLCON1 para `px/py` actuales.

### 8.2 `render` (línea 457)

`m_scene.install(backend)` → `install_copper_list` escribe `COP1LC`, dispara
`COPJMP1` y activa el DMA del Copper. La copperlist, al llegar al VBlank,
reescribe los registros de display (BPLCON0/1/2, modulos, DIW/DDF, punteros y
paleta) de forma **sincronizada con el raster**.

La copperlist la genera `rebuild_copper` en `tile_scroll.hpp`: emite los
registros estáticos (DMACON, BPLCON0/1/2, BPL1MOD/BPL2MOD, DIW/DDF), los 12
movimientos de punteros (6 planos × PTH/PTL), la paleta (32 COLORxx), y un
`wait_line(0xf8)` + `COLOR00=0` para ennegrecer la zona de border inferior.

### 8.3 El costo del Copper

Escribir una copperlist cada frame cuesta ciclos de CPU, pero es la vía
recomendada porque el Copper **escribe los registros en el momento exacto del
raster**, sin las carreras de escribir por CPU durante el VBlank (que producían
frames corruptos con blits activos, ver AGENTS.md).

---

## 9. Los blits y el presupuesto

`execute_frame_plan` (en `amiga_minimal.cpp`) traduce cada `BlitJob` a la
programación real del Blitter:

1. **Espera** a que el Blitter esté libre (`wait_blitter` leyendo `DMACONR`).
2. Escribe `BLTCON0`/`BLTCON1` (minterm 0xAA = copy D=C), modulos
   (`BLTAMOD`/`BLTDMOD`...), punteros (`BLTCPT`/`BLTDPT`) y `BLTSIZE`.

`BLTSIZE` codifica **altura** (bits 15-6) y **anchura en words** (bits 5-0):
`(height << 6) | words_per_row`.

### El presupuesto de blits

El `FramePlan` lleva un contador de palabras (words_per_row × height × planos)
y jobs. `configure_budget` (línea 465) lo dimensiona para el peor frame de un
cruce:

- shift: 2 blits × 21 × 288 × 6 planos = **72 K words**.
- edges: 80 tiles × 48 words = **3.8 K words**.

Si el presupuesto se supera, el demo marca `FAILED` y **se salta el shift** —
lo que en su día provocó que la demo "solo desplazara 15 px y reiniciara" (el
wrap se descartaba silenciosamente). `max_words = 131072` deja margen.

---

## 10. Optimizaciones aplicadas (y por qué)

| Optimización | Código | Beneficio |
|---|---|---|
| Scroll por hardware (no por CPU) | `BPLxPT`+`BPLCON1` | La CPU no toca píxeles |
| Superficie en anillo pequeña | 352x288 | 76 KB en vez de 245 KB |
| Copperlist sincronizada | `rebuild_copper` | Sin carreras de raster |
| Fine scroll con `BPLCON1` | tabla de 0-15 | 1 px suave sin costo por píxel |
| `sin/cos` en tablas | `circle_offset_*` | Sin libm en 68000 |
| Hash de mundo | `world_tile` (línea 163) | Mundo infinito sin mapa en RAM |
| Un blit por tile por playfield | `make_playfield_upload_jobs` | Menos overhead por blit |
| Shift en 2 blits no-solapados | `add_shift` | Funciona en WinUAE (el solapado no) |
| Presupuesto explícito | `configure_budget` | Fallo controlado, no corrupción silenciosa |
| Puntero de cabeza en el scheduler | `tile_scroll.hpp` | O(n²) → O(1) en `take_budget` |

---

## 11. Matemáticas clave, resumidas

- **Bytes por fila**: `ancho_px / 8`. (352 → 44)
- **Fetch por fila**: `320/8 + 2 = 42`. El +2 es el margen del fine scroll.
- **Modulo de display**: `44 - 42 = 2`.
- **Modulo de blit**: `surface_bytes_per_row - width_bytes`.
- **Puntero de plano**: `p * plane_bytes + cam_y * 44 + (coarse / 8)`.
- **Fine/coarse**: `fine = x & 15`, `coarse = (x - 1) & ~15`,
  `BPLCON1 = (16 - fine) & 15`, invariante `display_start == x`.
- **Wrap del anillo**: cuando `view_x` cambia, copiar
  `[16, 352) → [0, 336)` (shift de 16 px) y recargar la columna/fila que entra.
- **Mapeo de mundo a tile**: `tile = hash(col % 256, row % 128, semilla)`.
- **Colores por playfield**: PF1 = registros 0-7, PF2 = 8-15; color 0 de cada uno
  transparente; tramado 50% con `0xaaaa`/`0x5555`.

---

## 12. Qué ver al ejecutar

1. El **fondo (PF2)** recorre fases: horizontal, vertical, diagonal, círculo y
   seno, a 1 px/frame.
2. El **primer plano (PF1)** ondula con su propio movimiento (onda de Lissajous
   en `102`; en 104 comparte la cámara y difiere por su patrón de tiles
   transparente).
3. Cada ~16 px de scroll se produce el **wrap**: un pequeño salto de 16 px es
   invisible porque el contenido se desplaza y se recargan los tiles nuevos.
4. Cada 10 s cambian los **patrones** de ambos campos (nueva semilla del hash).

### Limitaciones conocidas

- El shift modifica el buffer mientras Denise lo está leyendo → puede verse un
  **tearing** de un frame en cada wrap. Es inherente a esta variante; los juegos
  reales evitan el shift usando buffers más grandes o la copper.
- En el emulador, el shift en 2 blits (72 K words) hace caer el fps a ~30;
  **en hardware real el Blitter lo hace en ~10 ms por wrap (cada 16 frames)** →
  sobra para 50 fps, como hace Mega Typhoon.
