# Captura estática de las demos que usan el overlay `debug()`

## Síntoma

Las demos que dibujan con `backend.debug()` (overlay host-side del fork WinUAE-DBG) y se capturan
con el runner automático quedan **congeladas en el frame 0**: la captura principal y las de
secuencia salen idénticas, aunque el runner espere con `--settle-ms`/`--screenshot-settle-ms`
varios segundos. Inyectar input (`--joy 1:right --joy-hold-ms 1500`) tampoco cambia la imagen.

Afecta a **209_reactive_loop** y **212_message_loop** (ambas usan `app.debug()`).

## No afecta a las demos con display real

**213_bartman_abyss** usa el mismo mini-SO y el mismo runner, pero dibuja con una **copperlist
real** (no `debug()`): su secuencia **sí anima** (`md5sum` distinto entre frames) y su
run-status avanza (`frame=122` tras 2 s con `mark_frame`). **215_gui_widgets** (display real)
también anima.

## Evidencia

| Demo | Dibujo | Loop | Secuencia | run-status frame |
|---|---|---|---|---|
| 213_bartman_abyss | copperlist real | `run_frames` | **anima** | **122** |
| 215_gui_widgets | copperlist real | `run_frames` | **anima** | — |
| 209_reactive_loop | `debug()` overlay | `run_frames` | **idéntica** | — |
| 212_message_loop | `debug()` overlay | `run_frames_polling` | **idéntica** | 0 (no llama `mark_frame`) |

Comprobado con `git stash` de los cambios de M12 y recompilando 212: captura **idéntica** →
**pre-existente**, no una regresión de M12.

## Interpretación

`debug_clear`/`debug_text`/`debug_rect` llaman a un **trap host-side** del fork
(`support/gcc8_c_support.c`: `debug_cmd` → `UaeLib` en `0xf0ff60`). El emulador del fork dibuja ese
overlay en su ventana/panel de depuración, **no** en el framebuffer emulado. El comando `screenshot`
del runner captura el **framebuffer emulado** (video real de Agnus), de modo que las demos que solo
usan `debug()` no reflejan sus actualizaciones en la captura: lo que se ve es un estado temprano.

El bucle **sí corre** (213 lo demuestra con el mismo motor); lo que no se refleja es el overlay
`debug()` en la captura.

## Opciones para desbloquear la verificación visual

1. **Exportar el overlay** desde el runner (leer el estado del panel de depuración del fork) en
   lugar de fiarse de `screenshot`.
2. **Dibujar en un display real** (Chip RAM + copperlist) en vez de `debug()` para las demos del
   mini-SO que deban validarse por visión; `debug()` quedaría solo para diagnóstico interactivo.
3. **Añadir `mark_frame`** a las demos de `debug()` para que el **run-status** (canal lateral)
   confirme que el bucle avanza, aunque la imagen no lo muestre.

La opción 3 es barata y da evidencia de corrección por el canal lateral; la 1 o la 2 hacen falta
para la validación **visual** (regla de oro).

## Referencias

- `support/gcc8_c_support.c` (`debug_cmd`, trap `0xf0ff60`).
- `dist/tools/run/run-demo.js` (`captureScreenshot`, `captureFrameSequence`).
- `demos/techniques/amiga/os/213_bartman_abyss` (contraejemplo que anima).
- `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` (bucle reactivo).
