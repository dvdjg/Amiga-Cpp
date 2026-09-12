# Plan de porte 1:1 — `effects/fire-rgb`

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
4. ✅ **Escena HAM + cuadruplicado promovida al engine**: `drivers::HamScene`
   (`engine/include/eng/graphics/drivers/ham_scene.hpp`) + test host HOST-016. La demo
   ya no escribe DIW/DDF ni palabras de Copper. ✅ **C2P encadenado por la IRQ de blit**
   (`FireDemo::on_blit`), el mecanismo fiel del original.

## Abierto (rendimiento vs original)

El original se ve **mas fluido** que nuestro port. Nuestro frame es **~4 vblanks (~12.4 fps, sin tearing)**; el original declara **788–968 lineas de raster** en su `MainLoop` (~3 vblanks) pero **no hemos medido su fps real**: su `.exe` arranca por un **bootloader propio** (`.adf`) y no expone contador de frames que el GDB pueda resolver (la region de la CIA no es legible por GDB). Hipotesis a cerrar: (a) si ejecuta codigo/datos en **memoria mas rapida** (ojo: sus buffers de fuego/chunky piden `MEMF_CHIP`); (b) su **scheduling C2P/IRQ**; (c) el **display** (HAM + cuadruplicado). **Pendiente**: medirlo en igualdad (p. ej. *watchpoint* en su `MainLoop` para contar ciclos/pasadas) y comparar. Palanca de diagnostico disponible: `-DK_BLIT_NASTY=1` (`BLTPRI`) — ver `docs/reference/amiga/hardware/amiga-blitter-priority-bltpri.md`.

## Siguiente (para completar)

1. **Diff 1:1** contra `fire-rgb.exe` (frames + `readPng` + vision).
2. **Portar el siguiente efecto** y actualizar el indice de portes (`docs/guides/roadmap/`).

