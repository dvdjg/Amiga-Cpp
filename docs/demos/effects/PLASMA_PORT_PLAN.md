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

## 7. Estado (2026-09)

- ✅ **Capa Copper ampliada** (`eng/graphics/copper/copper.hpp`): `skip` (máscara `0xffff`),
  `move32` (orden `reg+2`/`reg`), `patch_move32`/`patch_data`, `instruction_address`,
  `wait_raw`/`wait_masked` (CopWait/CopWaitMask), `move_at`; `COP2LCH/L`/`COPJMP2`. Test
  host HOST-019.
- ✅ **Sintab exacta** (`eng/core/sintab.hpp`, constexpr) reutilizada por `math2d` (HOST-020).
- ✅ **Datos del plasma** (tablas `tab1/2/3` verbatim + paleta 256) — HOST-021.
- ✅ **Driver `CopperChunkyScene`** (`eng/graphics/drivers/copper_chunky.hpp`) y **demo
  `082_plasma`** (doble buffer, `set(row,col,color)`, `takeover/install`).
- ✅ **BUG RESUELTO — HP del label del bucle de fila**: el plasma se dibujaba pero con
  **franjas/zonas negras** porque `CopperChunkyConfig.label_hpos` estaba en `0x7d` en lugar de
  `0x84` (`X(-4)` con `DIWHP=0x88`). La HP del `WAIT` del label desplazaba el mapeo de la
  re-ejecución de la "línea de color" (4 veces por fila vía `COPJMP2`) respecto al barrido real,
  de modo que el patrón caía en zonas/franjas. Con `label_hpos = 0x84` el plasma llena el
  lienzo completo y es **1:1** con el original. Lección: en el copper chunky la HP del `WAIT`
  del label **forma parte de la geometría** (es la posición del bloque dentro de la línea); no
  es un valor libre. Ver `docs/reference/amiga/techniques/copper-chunky.md`.
- ✅ **RENDIMIENTO — bucle caliente portado a ASM** (`support/plasma_chunky.s`). El plasma iba
  a **12.7 fps** (573 338 ciclos/frame): el `draw_into` C hacía `scene.set(row,col)`/píxel, con
  **dos comprobaciones de rango + búsqueda en `m_slot[]` + multiplicación** por bloque (~18
  instrucciones/iteración) y el compilador además emitía un `andi.l #255` redundante y no
  generaba `dbra`. El original usa **asm con puntero incremental y direccionamiento `(An,Dn.w)`**
  (6 instrucciones) y escribe la fila de una pasada. Se sustituyó por un bucle gas idéntico al
  del original (`support/plasma_chunky.s`) alimentado con **punteros de fila** del driver
  (`CopperChunkyScene::chunky_row`, análogo a `HamScene::bitplanes()`), de modo que no hay
  indirección por píxel. Resultado: **36.5 fps (194 185 ciclos/frame)**, ~3x. El coste restante
  es el propio 68000 sobre RAM lenta (~78 ciclos/iteración con las 6 instrucciones); el original
  enlace a VMA 0x0 con `.datachip` aparte, pero en A500 sin fast RAM corre en el mismo tipo de
  RAM, así que queda a la par. Instrumentación de la medición: contador de ciclos del periférico
  (`0xB7E928`), expuesto temporalmente en `g_eng_run_status.detail` (retirado del código final).

## 6. Notas de fidelidad

- El `SKIP` debe colocarse **al final de la línea** (`Y(y*4+3)`, `LASTHP`) para que siempre dispare; el comentario del original explica el cruce 255→256.
- `CopMove32` del original escribe `reg+2` y luego `reg` (orden propio); copiarlo tal cual.
- La paleta es **RGB12** (`u_short`), no RGB444 crudo del engine: convertir al cargar.
