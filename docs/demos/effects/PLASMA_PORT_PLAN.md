# Plan de porte 1:1 — `effects/plasma`

`plasma.c` dibuja un **plasma 2D** (suma separable de tres senoides 1D) sobre un display
**copper chunky** de 288×256: **sin bitplanes**, el Copper escribe `COLOR00` por bloque de
**8×4** a lo largo de cada línea (36 bloques × 64 filas), re-ejecutando la misma "línea de
color" 4 veces con `COP2LC`/`COPJMP2`, y un `SKIP` para evitar el cruce de línea 255→256.

Método: `docs/guides/roadmap/PORT_PROMPT_1A1.md`. Técnica: `docs/reference/amiga/techniques/copper-chunky.md`. Oráculo: `effects/plasma/plasma.exe` (`.map`/`.dbg`).

## 1. Inventario de la rebanada (alcance del porte)

| Fichero | Uso |
|---|---|
| `effects/plasma/plasma.c` | el efecto (toda la lógica) |
| `effects/plasma/data/plasma-colors.c` | **paleta 256** (`u_short colors_pixels[256]`) + `PixmapT colors` (PM_RGB12) |
| `include/effect.h`, `blitter.h`, `copper.h`, `fx.h`, `pixmap.h`, `system/memory.h` | contratos + `fx4i`/`SIN`/`COS` (fixed-point 4.12) |
| `lib/libgfx` | `NewCopList`, `CopWait`/`CopWaitH`/`CopSkip`/`CopMove16`/`CopMove32`/`CopInsSet32`/`CopSetColor`, `CopListFinish`/`Activate`/`Run` |
| `lib/libmisc` | `fx4i`, `SIN`, `COS` |
| runtime `system/` | `EFFECT(Plasma, Load, NULL, Init, Kill, Render, NULL)`, `TaskWaitVBlank`, `PROFILE` |

## 2. Hot (verbatim) vs fronteras (re-expresar)

- **Hot (verbatim)**:
  - `GeneratePlasmaTables`: `tab1[i]=fx4i(3*47)*SIN(rad*2)>>16`, `tab2[i]=fx4i(3*31)*COS(rad*2)>>16`, `tab3[i]=fx4i(3*37)*SIN(rad*2)>>16`, `rad=i*16`. **Fixed-point exacto**.
  - `UpdateXBUF`/`UpdateYBUF`: sumas separables con los incrementos (`a0+=1,a1+=2,a2+=3` en x; `a3+=2,a4+=3` en y) y el avance por frame (`a0+=1,a1+=3,a2+=2,a3+=1,a4-=1`).
  - `UpdateChunky`: `v = xbuf[x] + ybuf[y]` → escribe `cmap[v]` (RGB12) en el `data` de la instrucción `COLOR00` de esa fila. El **modo de direccionamiento** del asm (`moveb`, `addw d0,d0`, índice por word) se conserva.
  - La **estructura de la copperlist**: `CopWait(Y(0))`, por fila `CopMove32(cop2lc)` + label `CopWaitH(Y(y*4), X(-4))` + `CopInsSet32` + 36× `CopSetColor(0)` + `CopSkip(Y(y*4+3), LASTHP)` + `CopMove16(copjmp2)`. La codificación (`WAIT`=máscara `0xfffe`; `SKIP`=máscara `0xffff`; `CopMove32` = `reg+2` y luego `reg`) se copia tal cual.
- **Fronteras (re-expresar)**:
  - `NewCopList`/`CopListActivate`/`CopListRun` → arena Chip + `install_copper_list`/`takeover_display`.
  - Doble buffer `cp[0]`/`cp[1]` + `active` → patrón del engine.
  - `TaskWaitVBlank` → `wait_vblank`; `PROFILE` → telemetría del engine.

## 3. Tabla de mapeo de fronteras

| Original | Engine |
|---|---|
| `NewCopList(n)` / `CopListFinish` | arena Chip + `copper::ListBuilder` (extendido) |
| `CopListActivate` / `CopListRun` | `MinimalBackend::takeover_display` / `install_copper_list` |
| `CopSetColor`/`CopMove16`/`CopMove32`/`CopInsSet32`/`CopSkip`/`CopWaitH`/`cop2lc`/`copjmp2` | **ampliar `copper::ListBuilder`/`Scheduler`** (ver §4) |
| `TaskWaitVBlank` | `MinimalBackend::wait_vblank` |
| `colors_pixels[]` | `constexpr` inmutable (como `dualtab` de fire-rgb) |
| `EFFECT(...)` | `struct Plasma { init/update/render }` + `Engine` |

## 4. Huecos del engine a cubrir

| Necesidad | Estado |
|---|---|
| **`SKIP`** (WAIT con mascara `0xffff`) | **falta** en `ListBuilder` |
| **`COP2LC` + `COPJMP2`** (bucle de 4 líneas por fila) | **falta** en `ListBuilder`/`Scheduler` |
| **Instrucción direccionable/parcheable** (`CopInsSet32`/`CopSetColor` sobre un puntero) | **falta**: el builder debe poder devolver el índice de la instrucción y parchear su `data` |
| `CopWaitH` (mascara de VP con el bit 7) | **falta** (el engine tiene `wait_line_safe`, otra variante) |
| Driver de display copper-chunky reutilizable | **falta**: `eng/graphics/drivers/copper_chunky.hpp` |

## 5. Orden de porte

1. **Ampliar `copper`**: `skip()`, `move32_ptr()`, `cop2lc`/`copjmp2`, e **instrucción parcheable** (índice + escritura del `data`) + test host de la codificación.
2. **Driver `CopperChunkyScene`**: construye la lista (WAIT/label/36 colores/SKIP/copjmp2) con doble buffer y expone los punteros de color por fila (para parchear).
3. **Plasma**: `data/plasma-colors.hpp` (`constexpr`), tablas (`fx4i`/`SIN`) y `UpdateChunky` verbatim → escribe los colores en las instrucciones.
4. **Demo `082_plasma`**: READY, doble buffer, `install_copper_list` cada frame; `analyze` + visión.
5. **Diff 1:1** contra `plasma.exe` (mismos frames + `readPng` + `ollama-desc`); ajustar hasta 1:1.

## 6. Notas de fidelidad

- El `SKIP` debe colocarse **al final de la línea** (`Y(y*4+3)`, `LASTHP`) para que siempre dispare; el comentario del original explica el cruce 255→256.
- `CopMove32` del original escribe `reg+2` y luego `reg` (orden propio); copiarlo tal cual.
- La paleta es **RGB12** (`u_short`), no RGB444 crudo del engine: convertir al cargar.
