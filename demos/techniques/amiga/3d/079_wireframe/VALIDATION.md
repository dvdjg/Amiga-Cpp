# 079 - wireframe: informe de validación

Informe de validación de la demo (procedimiento F0-F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de la modernización 2026-10-17.

## Estado

**VERIFICADA** según los gates de la pasada: build/run/analyze en verde, medida de fps, secuencia + visión (prompt canónico en `src/VISION.md`), comparación determinista A/B con el build anterior y revisión de la geometría de la franja. Alcance y límites: al final del informe.

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Build | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| HEAD (antes) | 6,86 | 7,29 | 1.034.723 |
| Modernizada | **35,72** | **1,40** | 198.612 |

Reparto por secciones (`tools/debug/profile.mjs`, secciones de la demo):

| Sección | ciclos/frame (aprox.) |
|---|---|
| transform (objeto + visibilidad + proyección) | ~78k |
| alambre (`wire_lines`, ~90 aristas) | ~107k |
| copper + pre-clear lanzado | ~0,3k + esperas |
| HUD | **0** por frame (zona de 1 bpp estática; contador 24 escrituras/segundo) |

Cambios que explican el delta (cada uno con su efecto medido):

- Trazado por lote (`retro::wire_lines`) en lugar del camino `Surface::draw_line` + `FramePlan`: el camino antiguo pagaba el informe de presupuesto O(N²) por línea (`add_line` reconstruía el informe al añadir) más `memset`/`memcpy` del `BlitJob` por arista (perfil HEAD: `add_line` 26%, `memset` 15%, `memcpy` 8%, `__mulsi3` 7% del update).
- Transform con `eng::lib3d::transform_vertices` (proyector del engine, `muls.w`/`divs.w` nativos) y visibilidad de caras con `mul_wide`: los macros del port emitían productos 32x32 (`__mulsi3`).
- `run_frames` (IRQ + `Wait()`) y `set_blit_service_enabled(false)` (la demo no usa cola de fondo).
- Pre-clear del plano oculto del anillo lanzado sin esperar (se solapa con el update siguiente).

## Validación visual (secuencia + visión)

- Secuencia: `run-demo.sh --sequence-frames 8 --sequence-interval-ms 700`; evaluación canónica en `src/VISION.md` (modelo `qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): balón de fútbol en alambre azul sobre fondo oscuro, girando (la orientación cambia entre frames), texto blanco `WIREFRAME v2.0.4240 F <n>` en la parte inferior; **sin parpadeos, recortes ni artefactos visibles**.
- Comprobación propia (no solo visión): capturas de la franja (texto legible tras las iteraciones de geometría), diffs por bandas y lectura directa de los planos en memoria (el texto vive en su bloque de 1 bpp y las zonas superiores del balón no lo pisan).

## Determinismo A/B (vs HEAD, `-DWIREFRAME_FREEZE_ANGLE=1000`)

`tools/analyze/freeze-diff.mjs out/tmp/ab079/old_freeze1000.png …/final_freeze1000.png`:

- **2608 px distintos (0,60%)**. Composición de la diferencia: (a) la **franja de depuración** (nueva: ~2,1k px en la banda de ventana ≈216..224; HEAD no tenía HUD); (b) ~0,5k px de diferencias de **±1 píxel en extremos de aristas** atribuidas al cambio del transform a `lib3d` (proyector del engine vs los macros 1:1 del port: mismo algoritmo, con redondeo distinto en algún término). La forma, tamaño y rotación del balón coinciden; la visión no reporta artefactos.

## Coherencia de setup

- `g_eng_run_status` con `detail=0x3c` (60 vértices) y `mark_ready` tras init completo; fuente única de `BPLCON0` de la zona vía `bplcon0_for`.
- Geometría de la franja medida en emulador (documentada en `src/main.cpp`): el comparador de WAIT de WinUAE usa semántica `>=` sobre `V[7:0]` (`custom.cpp:4416` `coppercomp`), así que un WAIT `>255` dispara en la raster 255; la franja queda en las filas de ventana ≈216..224.

## Límites del alcance (SIN VERIFICAR)

- La medida de fps cubre una ventana de 20 s en `A500_debug`; no se midió `A500_release`.
- El A/B de fase congelada cubre el ángulo 1000; no se barrió la rotación completa (el scatter ±1 px podría variar con el ángulo).
- La franja lleva 1 plano de HUD; no se probó su lectura por OCR del modelo en todos los frames (la visión leyó `F <n>` en varias secuencias).
- Flicker: no se declaró `flicker-baseline.json` en esta pasada (la secuencia por paso de frame queda como evidencia primaria; pendiente el gate formal de flicker si el flujo lo exige).
