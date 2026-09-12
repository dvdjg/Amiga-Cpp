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

- El C2P y el fuego son **asm a mano**: se portan tal cual (los `asm volatile` compilan en el toolchain m68k).
- La display HAM + cuadruplicado es la parte más delicada; conviene construirla desde `MakeCopperList` (256 waits) y validar por contraste/visión, no por "hay píxeles".
- Primer paso verificable: **el fuego en chunky** (sin HAM ni C2P), para validar la simulación antes de meter display.

## Estado (hito actual)

- ✅ **Datos** `dualtab.c` copiados tal cual.
- ✅ **Backend**: `MinimalBackend::c2p_4bpp_step` (C2P 4bpp de 13 fases, portado de `ChunkyToPlanar`) y `set_bitplane_dat` (BLTxDAT para los bits HAM de planos 4/5). Son reutilizables.
- ✅ **Demo `080_fire_rgb`**: `MainLoop`/`fastrand`/`RandomizeBottom` copiados (se quitaron los `register asm("aN")`, que no compilan con `-O1` por presión de registros en `ADDR_REGS`; mismo algoritmo, menos "a mano"). Compila y arranca (READY).
- ❌ **Display**: sale un bloque relleno con colores erróneos, **no fuego**. Faltan: (1) el **bucle de Copper de 256 líneas** (`bpl1mod/bpl2mod = -40/0` cada 4 + `bplcon1` alterno) para el cuadruplicado; (2) revisar el HAM (BPLCON0/base/paleta y el orden de `BPLxPT`). Sin eso, no hay 1:1.

## Siguiente (para completar)

1. Emitir el **bucle de 256 líneas** de Copper (cuadruplicado + `bplcon1` alterno) tras `emit_planes_display`.
2. Revisar el **HAM**: BPLCON0 (`0x7A00`), `bpldat[4/5]`, `CopLoadColor(0,15,0)` y el orden `bpl[3..0]` de `BPLxPT`.
3. Verificar **contraste/visión** (fuego rojo/naranja/amarillo en la parte inferior), no "hay píxeles".
4. C2P en **interrupción de blit** (rendimiento) y **diff** contra `fire-rgb.exe`.
5. Portar al engine lo reaprovechable: escena **HAM + cuadruplicado** y el hook de interrupción de blit.
