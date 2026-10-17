# 116 - flatshade-convex: informe de validación

Informe de validación de la demo (procedimiento F0-F5). Pasada de modernización 2026-10-17. La demo queda **ABIERTA** por presupuesto (19,22 fps release < suelo 25 del efecto de relleno de polígonos): detalle y plan en la deuda **DT-026** (`docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`).

## Medidas (`tools/debug/measure-fps.mjs`, 15-20 s)

| Build | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| HEAD (antes) | 19,20 | 2,60 | 369.564 |
| Modernizada (zona HUD por WAIT) | 19,70 | 2,54 | 360.056 |
| Modernizada (HUD de plano uniforme, fix gráfico) | 18,05 | 2,77 | 392.909 |
| `A500_debug` (HUD con fps del último segundo) | 18,05 | 2,77 | 392.909 |
| **`A500_release` (`-O2`)** | **19,22** | **2,60** | 369.168 |

Referencia original medida en las mismas condiciones (ADF, ciclo-exacto, perfil con capturas por campo): **2,0 campos/frame (25 fps)** — parejas de campos idénticos; CPU saturada (142102/142112 ciclos/campo) y ~62k accesos DMA/campo. La brecha release vs original (~0,6 campos/frame) es **CPU +45 % de accesos/frame** y **fetch +~0,2 campos/frame**; el desglose y el plan de cierre (async + asm) están en el README §«Contra la referencia original» y DT-026.

Cambios de esta pasada: `run_frames` (IRQ + `Wait()`, sin sondeo de VPOSR), `set_blit_service_enabled(false)` (la demo no usa la cola de fondo) y **franja de depuración de coste cero**: **5º plano de 1 bpp activo todo el frame** (etapa de `scene::compose`; sin zonas de Copper — ver el fix gráfico abajo), texto estático + campos dinámicos (contador de frames + fps del último segundo) ~1 vez/segundo; el render por frame no cambia. El fetch del 5º plano cuesta ~0,23 campos (-1,65 fps) y es el precio de la corrección (el plano alternativo «zona por WAIT >255» no es cerrable). El **fps del propio HUD** se validó contra `measure-fps` en régimen estable (`F217 18.1` con 18,05 medido; delta de `frame` del `run-report.json`: 219−164 = 55 en 3,08 s = 17,9).

## Fix gráfico del HUD (2026-10-17, urgente)

El HUD de la primera pasada activaba el 5º plano con un WAIT `>255` al final de la copperlist **estática** del `Scene`: con la semántica `>=` del comparador de WinUAE (`custom.cpp:4416` `coppercomp`) el disparo caía en la raster 255 y la lista **terminaba ahí (STOP)**: el 5º plano quedaba **encendido para siempre** y la pantalla mostraba franjas verticales de basura (memoria del plano fuera de su banda). Arreglo estructural: **BPLCON0 de 5 planos en la cabecera, activo todo el frame** (sin WAITs/zona), plano del HUD de tamaño completo (8 KB, el fetch recorre 256 filas) y **paleta espejo 16..31** (palette[16] = tinta, palette[16+k] = palette[k]) para que el +16 del 5º plano sea transparente en la escena. La franja queda en la banda superior (filas de ventana ≈16..23, libre del balón).

## Experimentos acotados y descartados (medidos)

- **Fill XOR acotado a la caja del objeto** (soporte `bbox` nuevo en `flat_shade_xor`): 19,2 → **17,3 fps**. El fill acotado son 4 jobs (uno por plano) cuyo `wait_blitter` interno serializa la CPU con el Blitter dentro del update; el barrido completo es **un solo job** lanzado sin esperar. El recorte (~56k menos de fill DMA) solo paga con el referee async (DT-026).
- **Clear acotado por bbox del buffer** (ring de cajas por buffer): misma serialización por planos (4 jobs) → descartado por el mismo motivo y no medido por separado (el build que lo contenía es el de 17,3 fps junto al fill).
- Se conserva el **barrido completo** de fill y clear (single-job, `wait=false`), con comentarios de coste en `src/main.cpp`.

## Validación visual (secuencia + visión)

- Secuencia: `run-demo.sh --sequence-frames 2 --sequence-interval-ms 3000` tras 9 s de asentamiento; evaluación canónica en `src/VISION.md` (`qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): poliedro con **caras planas azules** de tonos variables (sombreado plano) girando sobre fondo oscuro, texto blanco `FLATSHADE v2.0.<build>  F<frames>  <fps>` en la banda **superior**; **sin artefactos, recortes ni superposiciones**; secuencia coherente de rotación.
- Comprobación propia: capturas separadas con el fps del HUD (`F217 18.1`, `tools/debug/hud-zoom.mjs` para leer los dígitos) que casa con `measure-fps` (18,05) y con el delta de `frame` del `run-report.json`; el render del balón no se ha tocado en esta pasada (misma llamada `flat_shade_xor` a barrido completo, mismos transforms).

## Fidelidad

- La pasada no modifica el render del sólido (la técnica de contorno EOR + area fill XOR, los transforms y la composición quedan igual); el único cambio de imagen es la **franja de depuración** (5º plano de 1 bpp, banda superior, filas ≈16..23) con sus campos dinámicos.
- El gate de fase congelada (`FLATSHADE_FREEZE_ANGLE` + `tools/analyze/freeze-diff.mjs`) sigue siendo el contrato determinista: aplica si se toca el dibujo (p. ej. al portar el fill al plan async).

## Límites del alcance (SIN VERIFICAR / abierto)

- **Presupuesto**: 19,22 fps release < 25 → demo **ABIERTA** (DT-026). La deuda no es de corrección visual sino de camino de ejecución (referee async del `FramePlan`; hay un prototipo async iniciado en el turno del 2026-10-17 que quedó **colgado** en la cadena de jobs y se retiró — la base del engine (`BlitJobKind::AreaFill`) quedó añadida para retomarlo).
- El fps del HUD se contrastó en régimen estable (una ventana tardía); no se verificó en las primeras ventanas tras READY.
- No se declaró `flicker-baseline.json` en esta pasada.
- La ruta asm (`K_FLATSHADE_ASM=1`) sigue sin ser válida (desfase de contorno, README): no se ha tocado.
