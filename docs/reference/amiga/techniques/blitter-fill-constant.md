# Relleno de rectángulo por Blitter con valor constante

Cómo rellenar un rectángulo axis-aligned con un color y dos trampas que han roto este motor en el engine (`engine/src/platform/amiga/amiga_internal.hpp` `blit_fill_region`, consumido por el `RectFillSink` de `playfield`).

## La regla (AHRM 3.ª, cap. 6)

Para rellenar un rectángulo con un valor constante **preservando los píxeles fuera del rect** (borde parcial) se usa el **cookie-cut** `$CA` con la máscara de borde como canal A:

- `BLTCON0` = minterm `$CA` (`D = (A & B) | (~A & C)`) + `USEC|USED`; **NO** activar `USEA`/`USEB` (canales A y B **deshabilitados**).
- A = `BLTADAT` = `$FFFF`, recortado por `BLTAFWM`/`BLTALWM` (la ventana de borde aplica al data register de un canal deshabilitado).
- B = `BLTBDAT` = la constante (`$FFFF` para poner el plano a 1, `$0000` para ponerlo a 0).
- C = **destino** (`BLTCPT` = `BLTDPT`, realimentado).

El AHRM lo dice explícitamente: «When disabled, no memory cycles will be executed for that channel and, for a source channel, the constant value stored in the data register of that channel will be used for each blitter cycle» (`…cat.md` §Blitter basics). Es decir, `BLTxDAT` es una **fuente constante solo si el canal está deshabilitado** (`USEx = 0`).

Con A = máscara: donde A = 1 la salida toma B (el color); donde A = 0 toma C (la D previa) → los bits enmascarados de la primera/última palabra **conservan el fondo**. Un rect **alineado a palabra** (`AFWM == ALWM == $FFFF`) hace A = `$FFFF` en todo el rect, de modo que `D = B`: el mismo código sirve para los dos casos.

## Trampa 1 — `USEA` activo con `BLTADAT`

Si se activa `USEA` a la vez que se precarga `BLTADAT`, el Blitter **lee A de memoria** en `BLTAPTR`, que en un relleno no se inicializa: el resultado es basura (franjas/noise). Síntoma observado: el fondo del panel salía con ruido en vez de un color plano.

## Trampa 2 — `D = A` + `AFWM`/`ALWM` **no** preserva la D

Con `D = A` (`$F0`) y una constante, `AFWM`/`ALWM` recortan la A de la primera/última palabra, de modo que la salida es `D = A & mask`: los bits enmascarados quedan a **0**, no conservan el valor previo de D. En un rectángulo **no alineado a palabra** (p. ej. un botón en `x=24`) eso **borra el fondo** de los pocos píxeles que quedan a los lados, dejando una banda oscura alrededor de cada widget. Por eso el relleno con borde parcial usa `$CA` (con C realimentado), no `D = A`.

## Ruta **síncrona** (`blit_fill_region` del `RectFillSink`)

`blit_fill_region` usa el cookie-cut `$CA` descrito arriba en los dos casos: alineado (`AFWM`/`ALWM` completos → `D = B`) y borde parcial (los bits fuera del rect se preservan vía C). Un solo blit, sin read-modify-write por CPU.

## Ruta **asíncrona** (`BlitJobKind::FillRect` del `FramePlan`)

La ruta diferida (`Screen::fill_box` → `FillRect`, `AmigaBackend::submit_blit_job`) programa exactamente el mismo cookie-cut (`$CA`, A = máscara de borde en `BLTADAT`, B = `BLTBDAT`, C = D = destino). El job lleva `words_per_row` = ancho del rect en palabras y `fill.afwm`/`fill.alwm`; el destino apunta a la **primera palabra del rect** (`x & ~15`).

Verificado en WinUAE: con `$CA` un rect en `x=11` deja el fondo intacto a ambos lados. (El minterm `$E2` = `(A&B)|(C&~B)` con A/B constantes **no** funcionó: la C parecía no contribuir; `$CA` con A = máscara sí.)

## Dónde está

- `engine/src/platform/amiga/amiga_internal.hpp` — `blit_fill_region` (ruta **síncrona**): cookie-cut `$CA` para alineado y borde parcial.
- `engine/src/platform/amiga/amiga_blitter.cpp` — `submit_blit_job`: ruta **asíncrona** `FillRect` con el mismo cookie-cut `$CA`.
- Self-test en hardware: demo `215_gui_widgets` — `verify_blitter_fill` (síncrono: alineado, borde parcial, multi-plano) y `verify_async_fill` (asíncrono: preserva el borde parcial); si alguno falla, la demo va a `Failed`. Gate visual de bandas: `tools/analyze/verify-gui-widgets.mjs` (fuga de fondo dentro del panel).
