# Relleno de rectángulo por Blitter con valor constante

Cómo rellenar un rectángulo axis-aligned con un color y dos trampas que han roto este motor en el engine (`engine/src/platform/amiga/amiga_internal.hpp` `blit_fill_region`, consumido por el `RectFillSink` de `playfield`).

## La regla (AHRM 3.ª, cap. 6)

Para rellenar con un valor constante `D = A` sin canal fuente:

- `BLTCON0`: minterm `D = A` (`$F0`) y **solo** `SRCD` activo (`USED`); **NO** activar `USEA`.
- `BLTADAT` = la constante (`$FFFF` para poner el plano a 1, `$0000` para ponerlo a 0).
- `BLTAFWM`/`BLTALWM` recortan la primera y última palabra.

El AHRM lo dice explícitamente: «When disabled, no memory cycles will be executed for that channel and, for a source channel, the constant value stored in the data register of that channel will be used for each blitter cycle» (`…cat.md` §Blitter basics). Es decir, `BLTxDAT` es una **fuente constante solo si el canal está deshabilitado** (`USEx = 0`).

## Trampa 1 — `USEA` activo con `BLTADAT`

Si se activa `USEA` a la vez que se precarga `BLTADAT`, el Blitter **lee A de memoria** en `BLTAPTR`, que en un relleno no se inicializa: el resultado es basura (franjas/noise). Síntoma observado: el fondo del panel salía con ruido en vez de un color plano.

## Trampa 2 — los bordes con `AFWM`/`ALWM` **no** preservan la D

Con `D = A` y una constante, `AFWM`/`ALWM` recortan la A de la primera/última palabra, de modo que la salida es `D = A & mask`: los bits enmascarados quedan a **0**, no conservan el valor previo de D. En un rectángulo **no alineado a palabra** (p. ej. un botón en `x=24`) eso **borra el fondo** de los pocos píxeles que quedan a los lados (primera/última palabra), dejando una banda oscura alrededor de cada widget.

## Ruta **síncrona** (`blit_fill_region` del `RectFillSink`)

- **Rect alineado a palabra** (`AFWM == ALWM == $FFFF`): blit `D = A` con `BLTADAT` constante (canal A deshabilitado). Un solo paso.
- **Rect con borde parcial**: rellenar por CPU con read-modify-write (`w = (w & ~mask) | (fill & mask)` para la primera/última palabra; las palabras interiores completas se ponen directas). El coste es despreciable para los rects de UI.

## Ruta **asíncrona** (`BlitJobKind::FillRect` del `FramePlan`) — cookie-cut `$CA`

La ruta diferida (`Screen::fill_box` → `FillRect`, `AmigaBackend::submit_blit_job`) preserva el borde con el **cookie-cut** con la máscara de borde como canal A:

- `BLTCON0` = minterm `$CA` (`D = (A & B) | (~A & C)`) + `USEC|USED` (A y B deshabilitados).
- A = `BLTADAT` = `$FFFF` **recortado por `AFWM`/`ALWM`** (la máscara de borde aplica al data register de un canal deshabilitado, AHRM §5290); B = `BLTBDAT` = color (`$FFFF`/`$0000`); C = **destino** (`BLTCPT`=`BLTDPT`).
- Los bits fuera del rect se **preservan** vía C (realimentado), así que **no hace falta alinear a 16 px**.

Verificado en WinUAE: con `$CA` un rect en `x=11` deja el fondo intacto a ambos lados. (El minterm `$E2` = `(A&B)|(C&~B)` con A/B constantes **no** funcionó: la C parecía no contribuir; `$CA` con A = máscara sí.)

## Dónde está

- `engine/src/platform/amiga/amiga_internal.hpp` — `blit_fill_region` (ruta **síncrona**): Blitter para el caso alineado y CPU read-modify-write para el borde parcial.
- `engine/src/platform/amiga/amiga_blitter.cpp` — `submit_blit_job`: ruta **asíncrona** `FillRect` con cookie-cut `$CA` en los tres casos (alineado y borde parcial).
- Self-test en hardware: demo `215_gui_widgets` (`verify_blitter_fill`) cubre rect alineado, borde parcial (preservación) y multi-plano; si alguno falla, la demo va a `Failed`. Gate visual de bandas: `tools/analyze/verify-gui-widgets.mjs` (fuga de fondo dentro del panel).
