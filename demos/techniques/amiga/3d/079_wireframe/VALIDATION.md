# 079 - wireframe: informe de validación

Informe de validación de la demo (procedimiento F0-F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de la modernización 2026-10-17.

## Estado

**VERIFICADA** según los gates de la pasada: build/run/analyze en verde, medida de fps, secuencia + visión (prompt canónico en `src/VISION.md`), comparación determinista A/B con el build anterior y revisión de la geometría de la franja. Alcance y límites: al final del informe.

## Medidas (`tools/debug/measure-fps.mjs`, 20 s)

| Build | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| HEAD (antes) | 6,86 | 7,29 | 1.034.723 |
| Modernizada (zona HUD por WAIT) | 35,72 | 1,40 | 198.612 |
| Modernizada (HUD de plano uniforme, fix gráfico) | 33,11 | 1,51 | 214.224 |
| `A500_debug` (HUD con fps del último segundo) | 32,73 | 1,53 | 216.733 |
| **`A500_release` (`-O2`)** | **37,04** | **1,35** | 191.512 |
| `A500_release` + HUD en **banda superior legal** (WAIT 44/58) | **39,72** | **1,26** | 178.617 |

Reparto por secciones (`tools/debug/profile.mjs`, secciones de la demo):

| Sección | ciclos/frame (aprox.) |
|---|---|
| transform (objeto + visibilidad + proyección) | ~78k |
| alambre (`wire_lines`, ~90 aristas) | ~107k |
| copper + pre-clear lanzado | ~0,3k + esperas |
| HUD | **0** por frame (plano de 1 bpp estático; campos dinámicos ~56 escrituras/segundo) |

Cambios que explican el delta (cada uno con su efecto medido):

- Trazado por lote (`retro::wire_lines`) en lugar del camino `Surface::draw_line` + `FramePlan`: el camino antiguo pagaba el informe de presupuesto O(N²) por línea (`add_line` reconstruía el informe al añadir) más `memset`/`memcpy` del `BlitJob` por arista (perfil HEAD: `add_line` 26%, `memset` 15%, `memcpy` 8%, `__mulsi3` 7% del update).
- Transform con `eng::lib3d::transform_vertices` (proyector del engine, `muls.w`/`divs.w` nativos) y visibilidad de caras con `mul_wide`: los macros del port emitían productos 32x32 (`__mulsi3`).
- `run_frames` (IRQ + `Wait()`) y `set_blit_service_enabled(false)` (la demo no usa cola de fondo).
- Pre-clear del plano oculto del anillo lanzado sin esperar (se solapa con el update siguiente).
- HUD de **plano uniforme** (5º plano 1 bpp activo todo el frame): el fetch extra cuesta ~0,11 campos (~-2,6 fps) y elimina la clase de fallo de la «zona» (ver abajo).

## Fix gráfico del HUD (2026-10-17, urgente)

El HUD original de esta pasada activaba el 5º plano con un WAIT `>255` del builder (`wait_line_safe`). **La lista de cobre terminaba ahí** y, con la semántica `>=` del comparador de WinUAE (`custom.cpp:4416` `coppercomp`), el disparo caía en la raster 255: el 5º plano quedaba ENCENDIDO más allá de su banda (hasta la reinstalación de la lista), mostrando franjas/basura según la fase del instalador. Arreglo estructural: **el plano del HUD va activo TODO el frame** (BPLCON0 de 5 planos en el header, sin WAITs), con el plano del HUD de tamaño completo (el fetch recorre 256 filas) y la **paleta espejo 16..31** (palette[16]=tinta, palette[16+k]=palette[k]) para que el +16 del 5º plano sea transparente fuera del texto. La franja queda en la banda superior (filas de ventana ≈16..23, libre del balón).

## Validación visual (secuencia + visión)

- Secuencia: `run-demo.sh --sequence-frames 3 --sequence-interval-ms 1500` tras 6 s de asentamiento; evaluación canónica en `src/VISION.md` (modelo `qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): esfera en alambre azul sobre fondo azul oscuro, girando (la orientación cambia entre frames), texto blanco `WIREFRAME v2.0.<build>` en la parte **superior** con contador `F <n>` y **fps del último segundo** (`33.5` → `31.9` entre frames); sin parpadeos ni recortes (la cruda anota una línea vertical fina en uno de los frames: es la arista casi vertical del balón en esa fase de rotación, presente también en el render anterior).
- Comprobación propia (no solo visión): capturas repetidas (varias fases del instalador) sin basura en ninguna zona; zooms de la franja (`tools/debug/hud-zoom.mjs`) con texto legible y **validación cruzada del fps del HUD**: `F198`/`32.5` y `F296`/`31.9` a 3 s de distancia ≈ 32,5 fps, contra 32,73 de `measure-fps` y el delta de `frame` del `run-report.json` (204→302 en 3,14 s = 31,2); y lectura directa de los planos en memoria (el texto vive en su plano de 1 bpp y el balón no lo pisa).

## Determinismo A/B (vs HEAD, `-DWIREFRAME_FREEZE_ANGLE=1000`)

`tools/analyze/freeze-diff.mjs out/tmp/ab079/old_freeze1000.png …/final_freeze1000.png` (build con la zona; el fix del HUD solo mueve la franja del borde inferior a la banda superior y no toca el balón: palette espejo verificada): **2608 px distintos (0,60%)** — HUD nuevo (~2,1k px) + ~0,5k px de ±1 píxel en extremos de aristas atribuidas al cambio de transform a `lib3d`. La forma, tamaño y rotación del balón coinciden; la visión no reporta artefactos.

## Coherencia de setup

- `g_eng_run_status` con `detail=0x3c` (60 vértices) y `mark_ready` tras init completo; `BPLCON0` de 5 planos por el constructor único (`bplcon0_for`).
- Geometría de la franja medida en emulador y documentada en `src/main.cpp`; el comparador de WAIT de WinUAE usa semántica `>=` sobre `V[7:0]` (`custom.cpp:4416` `coppercomp`) → **no** se usan zonas `>255` (no se pueden cerrar dentro de la lista).

## Límites del alcance (SIN VERIFICAR)

- La medida de fps cubre una ventana de 20 s por build (`A500_debug` y `A500_release`).
- El A/B de fase congelada cubre el ángulo 1000; no se barrió la rotación completa (el scatter ±1 px podría variar con el ángulo).
- La franja lleva 1 plano de HUD; el fps del HUD se contrastó en 2 ventanas (régimen estable) contra `measure-fps`; no en todas las fases.
- Flicker: no se declaró `flicker-baseline.json` en esta pasada (la secuencia por paso de frame queda como evidencia primaria; pendiente el gate formal de flicker si el flujo lo exige).
