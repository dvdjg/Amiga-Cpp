# Plan de porte 1:1 — `effects/fire-rgb`

> **Nota:** documento histórico (plan y bitácora del porte). Cita la API del **original** (`common.h`, `div16`/`mul16`); en el engine vigente esos nombres son `div_wide`/`mul_wide` (`docs/engine/architecture/SCALAR_LIBRARY.md`).

`fire-rgb.c` renderiza un **fuego** en un display **HAM6** de 320×256 con **cuadruplicado de líneas** por Copper, una simulación de fuego en **asm a mano** (`dualtab`) y un **C2P por Blitter** disparado por **interrupción de blit**. Es bastante más complejo que `wireframe`; el porte se hace por pasos, siempre contra el original.

Método: `docs/guides/roadmap/PORT_PROMPT_1A1.md`. Oráculo: `effects/fire-rgb/fire-rgb.exe`.

## La rebanada

```
effects/fire-rgb/fire-rgb.c
effects/fire-rgb/data/dualtab.c        (tabla de color del fuego, generada por gen-dualtab.py)
include/{effect.h, blitter.h, copper.h, pixmap.h, common.h}
include/system/{interrupt.h, memory.h}
lib/libgfx/*    (NewBitmap, NewCopList, CopSetupBitplanes, CopLoadColor, CopWaitSafe, CopMove16, SetupPlayfield)
lib/libmisc/*   (common.h: div16, sintab…)
system/*        (bucle de efecto, vectores de interrupcion/VBR, memoria)
```

## Hot vs fronteras

- **Hot (verbatim)**:
  - `MainLoop` (fuego): suma de 4 vecinos + doble lookup `dualtab[(int16)vl]` (asm con `%2:w`), salida a `chunky` y realimentación a `fire`. Mantener el `asm volatile` tal cual.
  - `RandomizeBottom` (2 líneas de semilla) + `fastrand` (asm).
  - `ChunkyToPlanar`: C2P de 4 bpp por Blitter, 13 fases (0..12) con `bltcon0/1`, `bltamod/bmod/dmod`, `bltcdat`, punteros y `bltsize` exactos (`ASHIFT(8)`/`ASHIFT(4)`, `BLITREVERSE`, minterms de swap 8x4 / 4x4).
- **Fronteras (re-expresar)**:
  - Display: `SetupPlayfield(MODE_HAM, 6/7, X(0), Y(0), 322, 256)` + `MakeCopperList` (256 `CopWaitSafe` + `bpl1mod/bpl2mod = -40 o 0` cada 4 líneas + `bplcon1 = 0x0022/0` alternando) + `CopLoadColor(0,15,0)` + `bpldat[4]=0x7777, bpldat[5]=0xcccc`.
  - Doble buffer (`screen[2]`, `chunky[2]`, `active`) con `CopInsSet32(bplptr…)`.
  - Interrupción de blit (`SetIntVector(INTB_BLIT, ChunkyToPlanar)`, `EnableINT(INTF_BLIT)`).
  - Memoria (`NewBitmap`/`MemAlloc`) → arenas del backend.

## Huecos en nuestro engine (a cubrir)

| Necesidad | Estado |
|---|---|
| Modelo/fuego (asm + `dualtab`) | portar verbatim (data + asm) |
| C2P 4bpp por Blitter (13 fases) | **falta**: método en `MinimalBackend` (secuencia de registros) |
| HAM6 (BPLCON0 = `MODE_HAM`, 6 planos, bits HAM) | **falta**: soporte HAM en el scene/scheduler |
| `bpldat[4/5]` (datos fijos de planos 4/5) | **falta**: escritura de `BLTxDAT` desde el backend |
| Cuadruplicado de líneas (`bpl1mod/bpl2mod` por línea) + `bplcon1` alterno | **falta**: bucle de Copper por línea (el `Scheduler` puede emitir `wait_line`+`move`, hay que generar 256) |
| Interrupción de blit (C2P en background) | **falta**: hook de interrupción en el backend (o C2P **síncrono** en el primer paso) |
| Doble buffer por parcheo de `BPLxPT` | reutilizar el patrón de 079 (ya hecho) |

## Orden de porte (por hitos verificables)

1. **Datos + fuego**: copiar `dualtab.c`; portar `fastrand`/`RandomizeBottom`/`MainLoop` (asm verbatim) → volcar `chunky` a pantalla para ver el fuego.
2. **C2P**: portar `ChunkyToPlanar` como método del backend (primero **síncrono**: llamar las 13 fases con `wait_blitter`), verificando las 4 planos.
3. **Display HAM + cuadruplicado**: `bplcon0` HAM6, `bpldat[4/5]`, 256 líneas de Copper con `bpl1mod/bpl2mod` y `bplcon1`, `CopLoadColor(0,15,0)`.
4. **Doble buffer + swap** (patrón de 079).
5. **Interrupción de blit** (opcional, para el 1:1 de rendimiento): hook en el backend que dispare la fase siguiente del C2P al terminar el blit.
6. **Diff 1:1** contra `fire-rgb.exe` (frames + `readPng` + visión).

## Notas de fidelidad

- El C2P y el fuego son **asm a mano**. El bucle de fuego va en `support/fire_loop.s` (GAS), no como `asm volatile` en C++: GCC-15 **ignora los pins** `register asm("aN")` y no puede asignar los 7 registros de dirección que exige el `MainLoop` original. La ruta C++ (`MainLoopC`) queda como respaldo y se selecciona con `-DK_FIRE_ASM=0`.
- La display HAM + cuadruplicado es la parte más delicada; conviene construirla desde `MakeCopperList` (256 waits) y validar por contraste/visión, no por "hay píxeles".
- Primer paso verificable: **el fuego en chunky** (sin HAM ni C2P), para validar la simulación antes de meter display.

## Estado (hito actual)

- ✅ **Datos**: `dualtab` generado en **C++23 `constexpr`** (`data/dualtab.hpp`), elegante y documentado; verificado **byte a byte** contra la tabla de `gen-dualtab.py` (0 diferencias). Sustituye al `.c` copiado.
- ✅ **Backend**: `c2p_4bpp_step` (C2P 4bpp, 13 fases) y `set_bitplane_dat` (HAM). Reutilizables.
- ✅ **Fuego**: la simulacion (`MainLoop`/`fastrand`/`RandomizeBottom`) produce una llama correcta. **Clave**: el indice de `dualtab` debe ser la **media** de los 4 vecinos (`suma>>2`); la suma cruda llega a ~992 y la tabla tiene 256 entradas.
- ✅ **C2P**: corregido el **minterm** `(A&C)|(B&~C)` = **`0xE4`** (estaba `0xE2`, que escribia la mascara `bltcdat` donde la fuente era 0 -> patron de rayas).
- ✅ **Display** (HAM `bpldat[4/5]` + cuadruplicado + `bplcon1`): verificado correcto (con los planos a 0 la pantalla es negra).
- ✅ **Resultado**: `080_fire_rgb` muestra el **fuego** (gradiente naranja/amarillo sobre negro, confirmado por vision); `detail` = suma del buffer de fuego.

## Pendiente (pulido y fidelidad)

1. **Fluidez / rendimiento**:
   - ✅ `FIREITER` recuperado en **asm** (`support/fire_loop.s`): reproduce el bucle del original (4 vecinos B/C/D/E, doble lookup `(a5,d1.w)` = media, realimentacion a `fire`). Medido: **asm ≈ 12.4 fps vs C++ ≈ 7.2 fps** (~1.7×) en el emulador.
   - ✅ **C2P encadenado por IRQ de blit (fiel)**: `MinimalBackend::set_blit_service` (nivel 3, mismo autovector que el VBlank) + `FireDemo::on_blit` programan la fase siguiente al terminar cada blit (mecanismo del original). El fuego crece correctamente. Medido: suelo sin C2P **16.6 fps (3.0 vblanks)**; cadena **13.55 (3.7)**; con **BLTPRI** (*nasty*) **12.44 (4.0)** → el C2P cuesta ~0.7 vblanks y BLTPRI **empeora** (el bus de Chip es el cuello).
   - **Diagnostico (corregido)**: la caida a ~11 fps que se observo **no** era la cadena por IRQ, sino el `BackgroundPump` llamando a la cola de fondo en **cada iteracion** del bucle de espera (vacia). Ahora solo bombea si hay tareas → el suelo vuelve a 3.0 vblanks. **Medir antes de atribuir.**
   - ✅ **Tearing (corregido)**: se instalaba la copperlist del buffer convertido en el `update` **siguiente** (latencia 2 frames) → con 2 buffers el display mostraba el que el C2P estaba escribiendo (borde dentado + manchas en la zona caliente). Fix: instalar la copperlist **al completar el C2P** (en la IRQ de blit).
   - **Causa raiz del "cuelgue" del asm** (no era un crash): `fire_loop.s` usaba `.cfi_startproc/.cfi_endproc`, que generan una seccion **`.eh_frame` no vacia**. El canal lateral enumera todas las secciones del hunk (`text, rodata, .eh_frame, data, bss`), pero el `.map` del runner filtraba `.eh_frame`, asi que los indices se desplazaban y `g_eng_run_status` se resolvia a una direccion equivocada (la demo "no alcanzaba READY" pese a ejecutarse bien). Arreglado en dos frentes: (a) `support/fire_loop.s`/`fire_asm.s` sin CFI; (b) `tools/run/run-demo.ts` incluye `.eh_frame` en las secciones del `.map` para que el orden coincida siempre.
2. **"Pantalla dividida"**: era el desfase vertical — se usaba `wait_line(i)` (VPOS 0..255) en vez de `CopWaitSafe(Y(i))` con `Y(i)=i+0x2c`; corregido con `wait_line_safe(i+0x2c)`. El cuadruplicado ya cuadra (angosto del original: DDFSTOP `0xD1`, DIWSTOP `0x2CC3` por el `+2`).
3. **Diff 1:1** contra `fire-rgb.exe` (frames + `readPng` + vision). Nota: el original usa un **bootloader propio** (`.adf` con `addchip.bootblock`), no corre como `a.exe` bajo AmigaDOS.
4. ✅ **Escena HAM + cuadruplicado promovida al engine**: `drivers::PlanarScene`
   (`engine/include/eng/graphics/drivers/planar_scene.hpp`) + test host HOST-016. La demo
   ya no escribe DIW/DDF ni palabras de Copper. ✅ **C2P encadenado por la IRQ de blit**
   (`FireDemo::on_blit`), el mecanismo fiel del original.

## Rendimiento vs original: análisis medido (2026-09-16)

**Cómo late el original** (`effects/fire-rgb/fire-rgb.c` + `system/effect.c`):

- `EffectRun` llama a `effect->Render()` **una vez por tick del contador de frames**
  (VBlank): es **cuantizado a campos**, igual que nuestro engine.
- Su **propio profiler** (`ProfilerStart/Stop` sobre `RandomizeBottom + MainLoop`,
  `system/profiler.c`) declara en el comentario del `MainLoop`:
  **788–968–976 líneas de raster** (min-avg-max). Una línea PAL son ~454 ciclos, y un
  campo 312.5 líneas → **2.5–3.1 campos por frame ≈ 16–20 fps**. Es decir, **25 fps
  (2 campos) no es alcanzable con este algoritmo en un A500**, ni por el original.
- Su C2P (13 blits) va **encadenado por la IRQ de blit**: la CPU no lo espera. Y su
  `dualtab[256]` se indexa con la **palabra cruda como offset en BYTES** (`(dt, idx.w)`),
  que equivale a la media de los 4 vecinos — exactamente lo que hace nuestro
  `support/fire_loop.s`.

**Cuánto tardamos nosotros** (`node tools/debug/measure-fps.mjs 080_fire_rgb`, A500
`-O1`, ciclo-exacto):

| variante | fps | campos/frame | ciclos/frame |
|---|---|---|---|
| 080 baseline | **12.4–12.6** | 4.0 | ~570.000 |
| 080 `-DK_DIAG_SKIP_C2P=1` | **16.70** | 3.0 | 424.899 |

425k ciclos = **~936 líneas de raster** → **nuestro bucle de fuego ya está en el rango
del original** (788–968): el port del asm no es el problema. Lo que añade el 4.º campo
es el **C2P**: sus 33 KB de DMA (~13 blits de 2560 B) compiten por el bus de Chip, que
ya soporta el display HAM6 (61 KB/frame). `-DK_BLIT_NASTY=1` (BLTPRI) **empeora**
(12.44 fps / 4.0): el bus de Chip es el cuello, no la prioridad del Blitter.

**Hallazgo nuevo**: el `.map` muestra que **todo el programa se enlaza por debajo de
0x80000**, o sea en **Chip RAM** (`.text` en 0x400, `.rodata` en 0x253a — ahí vive
`kDualTab` —, `.data`/`.bss`), con los **512 KB de Slow RAM sin usar**. Se probó y se
mantiene (inocuo, y en hardware real descarga el bus):

- **`fire` (10 KB) y `dualtab` (1 KB) a Slow RAM** (sólo los toca la CPU).
- *(Experimentado y **revertido**)* copiar la propia rutina `fire_loop` a Slow
  (~100 KB/frame de fetches de instrucciones desde Chip): **sin ganancia medible** y
  añade un invariante frágil (el bucle debe seguir siendo relocalizable).

Medición tras los cambios: **sin cambio a nivel de campo** (12.44–12.56). El frame está
cuantizado a campos y el trabajo cae **justo en el límite 3/4**, así que una mejora
sub-campo no se ve en el fps.

**Instrumento y medidas de ciclos NO cuantizados.** La demo publica el tramo medido en
`g_eng_run_status.detail` (campo que `measure-fps` ya imprime):
`-DK_FIRE_PROF=1` → coste de `RandomizeBottom+MainLoop`; `=2` → coste del `update`
completo. (Los `debugperiph counters/checkpoints` **no reportan** en esta ruta de
lanzamiento — sale `no counters` —; el camino del `detail` sí funciona.)

```
080_fire_rgb (A500 -O1, ciclo-exacto, measure-fps):
  frame                        570.303-572.197 ciclos  (4.03 campos, 12.4 fps)
  RandomizeBottom+MainLoop     436.202 ciclos  (3.07 campos)   [-DK_FIRE_PROF=1]
  update completo              376.710 ciclos  (2.66 campos)   [-DK_FIRE_PROF=2]
  sin C2P                      424.899 ciclos  (3.00 campos)   [-DK_DIAG_SKIP_C2P=1]
```

**Conclusión: estamos en paridad con el original, no por debajo.** Nuestro bucle de fuego
(376-436k ciclos) cae **dentro del rango que el propio original se atribuye** (788-968-976
líneas = **357-439k**). Y el original, con su propio contador, **tampoco llega a 25 fps**:
su `Render` medio son 3.09 campos → cuantizado a ticks de VBlank son **4 campos ≈ 12.5 fps**
(sólo su mínimo de 2.52 campos daría 3 campos = 16.7). Es decir, el "~25 fps" del original
**no sale de sus cifras**: es lo que se ve en un emulador **no ciclo-exacto** (o en AGA),
donde los accesos al bus de Chip se cobran más baratos.

**Inconsistencia de medida a cerrar**: el `update` (2.66 campos) no puede ser menor que el
tramo del fuego (3.07). Lo más probable es que el tramo corto se mida con la **cola del C2P
del frame anterior aún corriendo** (sus blits estorban a la CPU) mientras el `update` largo
arranca ya tras la salvaguarda `FinishC2p`. Se cierra midiendo los dos tramos en el **mismo
build** (p. ej. alternando el `detail` por frame).

**Palancas que quedan** (para *superar* al original, ya no para igualarlo):

- El bucle gasta ~4.5 accesos a memoria por celda (4 lecturas de vecinos, 2 de tabla y 3
  escrituras por cada 2 celdas); con `cpu_cycle_exact` cada acceso al bus se cobra caro, así
  que el cuello es **memoria, no ALU**. Reutilización clásica: el long de `D` de una
  iteración **es** el long de `B` de la siguiente (los punteros van desfasados una palabra)
  → llevarlo en un registro ahorra 1 de las 4 lecturas por iteración (~25 % del tráfico del
  fuego).
- La sincronía de frame deja ~1.4 campos de espera (4.03 de frame para 2.66 de trabajo):
  revisar el `wait_vblank`/`render` del bucle del engine.
- El **C2P** (~1 campo: 13 blits, 33 KB de DMA) decide entre 3 y 4 campos; arrancar su fase 0
  antes (solapada con la cola del fuego) puede valer el campo que falta → **16.7 fps**.

Objetivo realista: **≤ 3 campos = 16.7 fps**, que es el presupuesto del propio original.



## Siguiente (para completar)

1. **Diff 1:1** contra `fire-rgb.exe` (frames + `readPng` + vision).
2. **Portar el siguiente efecto** y actualizar el indice de portes (`docs/guides/roadmap/`).

### Estado de la comparación de rendimiento con el oráculo (2026-09-16)

La comparación **está cerrada en las mismas unidades y sobre la misma región de código**,
sin necesidad de arrancar el oráculo: el profiler del original loguea
`FireRGB took %d-%d-%d (min-avg-max) raster lines` para `RandomizeBottom + MainLoop`
(`system/profiler.c`, `ReadLineCounter()`), y sus cifras son **788 / 968 / 976**, mientras
que nuestro tramo equivalente mide **436.202 ciclos = 961 líneas** (436.202 / 454):
**dentro del rango y a la altura de su media**. Es decir, el bucle del port ya rinde como
el original; lo que marca el fps es el frame completo (C2P incluido) y su cuantización.

Arrancar el oráculo en vivo **se intentó y queda a medias**:
- Con una config derivada del runner (A500 ciclo-exacto, KS 1.3, sin DH0/DH1) y el
  `fire-rgb.adf` en DF0, **WinUAE corre** (verificado: sigue vivo a los 12 s), así que el
  disquete es booteable.
- El **adjunte automático del GDB falló** (el fork no expuso el puerto con esa config), así
  que no hubo captura ni lectura. Quedan además los bloqueos ya conocidos: la imagen del
  original es **relocalizable** (`.text` en 0x0 → los *watchpoints* necesitan la base del
  hunk en runtime) y su `Log()` hay que capturarlo por su propia vía.
- El ratio, en cambio, **no necesita esa medida**: las cifras del original y las nuestras
  son la misma magnitud (líneas de raster de la misma función).


