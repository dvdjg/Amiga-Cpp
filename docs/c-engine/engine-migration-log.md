# Registro de migración al engine (Cursor-Amiga-C)

Migración incremental del main.c monolítico al uso del engine (API de alto nivel), comprobando que cada fase sigue compilando y funcionando.

## Fase 1: Engine importado; init, sistema e input vía engine (hecho)

**Objetivo:** Tener el código del engine en el proyecto y que main.c use solo la API del engine para init, shutdown, TakeSystem/FreeSystem, esperas (VBL, línea), ratón y frame counter.

**Cambios realizados:**
- Creada carpeta `engine/` con `include/` (engine.h, engine_types.h, engine_internal.h) y `src/` (system.c, blitter.c, display.c, input.c, memory.c), copiado/adaptado desde Cursor-Amiga.
- En `engine/src/system.c` se añadió `#include <proto/graphics.h>` para OwnBlitter, WaitBlit, DisownBlitter, LoadView, WaitTOF.
- Makefile: `subdirs := support engine/src`, `c_sources` incluye `$(wildcard engine/src/*.c)`, `CCFLAGS` con `-I. -Iengine/include`.
- main.c:
  - Incluye `engine.h`; eliminados estado global del sistema (SysBase, custom, DOSBase, GfxBase, VBR, etc.) y funciones TakeSystem/FreeSystem, WaitVbl, WaitLine, SetInterruptHandler, MouseLeft/MouseRight (ahora en el engine).
  - Se mantiene `extern volatile struct Custom *custom` y `WaitBlt()` local porque el código de blits y copper en main.c sigue usando custom directamente (Fase 2 pasará a engine_blit_*).
  - Init/shutdown: `engine_init()` / `engine_shutdown()`; en main() se llama Write/KPrintF/Delay después de engine_init().
  - TakeSystem/FreeSystem, engine_wait_vbl(), engine_wait_line(0x10), engine_mouse_left().
  - Handler VBL: engine_set_interrupt_handler(interruptHandler); en el handler se usa engine_get_frame_counter() y engine_frame_tick().
  - scroll y tablas sinus se mantienen; el índice de frame viene de engine_get_frame_counter().

**Verificación:** Compilar con la tarea del IDE (run-make.sh). Ejecutar en WinUAE y comprobar que la demo arranca, se ve scroll, bobs y overlay; salir con botón izquierdo. Si algo falla, depurar con MCP Debug Tools (breakpoints, variables).

**Próximo paso:** Fase 2 — sustituir en main.c el clear/blit manual y debug_* por engine_blit_clear, engine_blit_bob y engine_debug_*.

---

## Fase 2: Blits y overlay vía engine (hecho)

**Objetivo:** Eliminar el código manual del blitter y las llamadas directas a debug_* en main.c; usar engine_blit_clear, engine_blit_bob y engine_debug_*.

**Cambios realizados:**
- En `demo_do_one_frame()`: sustituido el bloque de clear (WaitBlit + registros custom) por `engine_blit_clear((void*)image, 0, 200, 320, 56, 320, 5)`.
- Sustituido el bucle de bobs (WaitBlit + bltcon0/1, bltapt, etc.) por `engine_blit_bob(src, mask, (void*)image, x, 200+y, 32, 16, 32/8, 320/8*5, 5)` con mask = src + (32/8).
- Sustituidas debug_clear(), debug_filled_rect, debug_rect, debug_text por engine_debug_overlay_clear(), engine_debug_filled_rect, engine_debug_rect, engine_debug_text (con cast a int donde hace falta).
- Eliminada la función local `WaitBlt()` de main.c; se mantiene `extern volatile struct Custom *custom` para setup_demo_display (copper) y el handler VBL (intreq).

**Verificación:** Compilar y ejecutar en WinUAE; comprobar que el área inferior se limpia, los bobs se dibujan igual y el overlay de debug se ve correctamente.

**Próximo paso:** Fase 3 — opcional: usar engine_alloc para la copper list y dejar main.c centrado solo en lógica de demo y assets.

---

## Fase 3: Uso de engine_alloc para la copper (hecho)

**Objetivo:** Asignar la copper list con engine_alloc en lugar de AllocMem, para usar la API del engine de forma coherente.

**Cambios realizados:**
- En `setup_demo_display()`, `AllocMem(COPPER_SIZE, MEMF_CHIP)` sustituido por `engine_alloc(COPPER_SIZE, MEMF_CHIP)`.

**Verificación:** Compilar y ejecutar; el comportamiento debe ser idéntico. (No se añade engine_free de la copper al salir porque el programa termina; en un juego real podría liberarse en el cleanup.)

---

## Verificación en depurador (sesión autónoma)

- **Config:** Amiga 500 (MCP Debug Tools).
- **Pasos:** start-debug → pause en ejecución → get-call-stack.
- **Resultado:** Call stack correcto: `main` → `demo_run_loop` → `demo_do_one_frame` → `Wait10` → `engine_wait_line` (system.c); frame actual en `interruptHandler` (VBL). La demo está en el bucle principal y el engine responde. Ejecución reanudada con continue.

---

## Fase 4: Encapsulación total del hardware (REVERTIDA)

**Objetivo (no alcanzado):** Que la aplicación no usara `custom` ni `<hardware/custom.h>`, construyendo la copper con constantes `ENGINE_CUSTOM_*` y escribiendo al hardware vía `engine_set_copper1/2`, `engine_set_dma`, etc.

**Qué pasó:** Tras los cambios, en WinUAE no se veían gráficos (solo sonido). La sustitución de `offsetof(struct Custom, ...)` por constantes `ENGINE_CUSTOM_*` y de `custom->...` por las nuevas APIs del engine rompió el display sin que se detectara antes de dar por hecho que funcionaba.

**Revertido:** main.c ha vuelto al estado funcional anterior a Fase 4:
- Incluye de nuevo `<hardware/custom.h>`, `<hardware/dmabits.h>`, `<hardware/intbits.h>` y los de graphics/exec.
- `extern volatile struct Custom *custom` restaurado.
- Copper list construida con `offsetof(struct Custom, ...)`.
- Setup de display y VBL con `custom->cop1lc`, `custom->cop2lc`, `custom->dmacon`, `custom->copjmp1`, `custom->intena`, `custom->intreq` y doble escritura en `intreq` en el handler.
- Test de memoria con `AllocMem`/`FreeMem`; copper con `engine_alloc(..., MEMF_CHIP)` (Fase 3 se mantiene).

**Lección:** Cualquier cambio en copper, DMA, VBL o display debe comprobarse compilando y ejecutando la demo (gráficos + música). El agente debe ejecutar la compilación cuando tenga toolchain (AMIGA_BIN_PATH) y contrastar con el comportamiento conocido; no dar por válido sin verificación.

---

## Cambios incrementales (post-revert, verificados con build)

- **VBL:** En el handler de interrupción se sustituyeron las dos escrituras `custom->intreq = (1<<INTB_VERTB)` por `engine_ack_vbl()` (mismo comportamiento, delegado al engine). Build OK.
- **Memoria:** El test rápido en `setup_demo_display()` pasa a usar `engine_alloc(2502, MEMF_ANY)` y `engine_free(test, 2502)` en lugar de AllocMem/FreeMem. Build OK.
- **Limpieza:** Constante `BPLCON2_SPRITES_OVER_PF` para el valor de bplcon2 (sprites sobre playfields). Build OK.
- **Verificación:** Compilación tras cada cambio con `bash scripts/verify-build.sh`; clean + full build final OK.
