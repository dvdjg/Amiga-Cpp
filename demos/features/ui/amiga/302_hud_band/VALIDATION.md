# VALIDATION — demo 302 `hud_band`

Informe de validación de la fase **H1** del [`ROADMAP_HUD.md`](../../../../../docs/guides/roadmap/ROADMAP_HUD.md). Intención: [`src/INTENT.md`](src/INTENT.md) · Visión canónica: [`src/VISION.md`](src/VISION.md) · Crudos de visión y medida: [`ui_amiga_302_hud_band_report.md`](ui_amiga_302_hud_band_report.md).

## Qué se valida

Un juego mínimo (campo a 4 planos con bloques estáticos y un actor que rebota) con un **HUD de acción en franja propia de 48 líneas a 2 planos con paleta propia** (blanco/amarillo/rojo) compuesta con `ModeSwitchZone` en la línea de display 208: contador de puntos (6 dígitos), medidor de energía lineal (que pasa a rojo bajo el 25 %) y 4 *pips* de vidas, más la línea de depuración obligatoria `HUD BAND vX.Y.build F NNNNN FPS NN.N` (fps por `eng::debug::HudFpsWindow`).

El actor va por el **sistema de objetos del engine** (`World::add_actor` + `BackgroundPolicy::SaveUnder`): el Blitter restaura el fondo que tapa y lo vuelve a guardar en su nueva posición, de modo que los bloques estáticos no se dañan. El render pinta **primero la capa viva del juego** (blits) y **después la franja** por CPU (`HUD_SYSTEM.md` §9.3).

## Medidas (A500_debug, bucle por **sondeo**)

**Modo de espera (familia 212/DT-011, 2026-10-19).** La demo usa `VblankWait::Polling`: con la espera por señal el guard `0xE212` la rechaza bajo el runner, y con la **espera activa moría de forma determinista a los ~2,5 s** (`vbl=124` congelado, idéntico en dos corridas). Con sondeo la app sobrevive la ventana completa: `vbl=584` avanzando en 12 s. Detalle y tabla de migración en `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.

| Medida | Valor | Herramienta |
|---|---|---|
| fps / cadencia | **47,9-49,9 fps** por **contador** (`fps10`; lectura por el periférico de depuración: fiable, no depende del vídeo) y `vbl` (latidos del mini-SO) avanzando en toda la ventana; la ventana del perfilador da **1 campo** (142.102 ciclos). La cifra en pantalla capturada puede quedar vieja (DT-023/DT-035) | contador `fps10`/`vbl` de la demo (`--read-debugperiph counters`) + `node tools/debug/winuae-profile.mjs ui_amiga_302_hud_band A500_debug 30` |
| Secciones por frame (media 21 frames, `ENG_PROF`) | HUD `apply` **~18k** (tras el arreglo; antes ~208k), `draw_world` 26,5k, `present` 34,4k | contadores del periférico de depuración (`--read-debugperiph counters`, instrumentación temporal) |
| Presencia del sprite | **124 px** del blob leídos por la demo en **sus propios bitplanes** (self-check, evidencia de memoria) + el modelo de visión lo describe con su contorno en cada frame | self-check del framebuffer (contadores) + `vision-final`/`vision-run` |
| Integridad del prop de prueba (120,168,40,12) | sin daño durante el cruce (ver cookie-cut) | verifier + self-check |
| Cookie-cut del sprite | **`cut_bad = 0`** en ~300 frames (varios cruces del prop): la demo lee sus bitplanes y cuenta píxeles AJENOS dentro del solape sprite/prop (una guarda o banda daría >0). Evidencia de **memoria**, no de vídeo | contador `cut_bad` (`--read-debugperiph counters`) |
| Presencia por color (franja) | blanco ~7.200 px; amarillo 3.260 → 2.612 (barra vaciándose); rojo 0 en esa ventana | `node tools/analyze/check-elements.mjs out/run/.../sequence "#FFFFFF,#FFFF00,#FF0000"` |
| Verificador de la franja | texto/marco y amarillo presentes; pares consecutivos con cambio (barra de energía por deltas) | `node tools/analyze/verify-hud-band.mjs --sequence-dir out/run/.../sequence` (los tramos de captura congelada son de la herramienta: DT-035) |
| Flicker | **gate OK**: 0 candidatos en la ventana capturada, `blocks_low` máx. 7; el peor caso observado (4 candidatos / 10 bloques) es siempre la zona del actor en movimiento y el modelo lo descarta; `flicker-baseline.json` declarado (`max_candidates: 4`, `max_blocks_low: 11`) | `tools/test-regression.sh --flicker --require-flicker-ok --demo demos/features/ui/amiga/302_hud_band` |

### Arreglo del parpadeo y del coste (A/B de una variable)

1. **Fondo del actor.** Con el borrado por relleno negro (`ClearRect`), el bloque estático quedaba dañado (768 de 2304 px: el erase le borraba las 16 líneas superiores). Con `BackgroundPolicy::SaveUnder` (restaurar→guardar→dibujar) el bloque queda intacto (2304/2304) y el actor se ve completo en todos los frames.
2. **Orden de pintado (carrera con el haz).** Con el HUD pintado primero, los blits del actor caían a ~0,6 de campo (justo cuando el haz pasaba por y=150): el actor se veía completo en **22 de 40** frames (alternando). Con la capa viva primero y la franja después: **40/40** (y 12/12, 16/16 en capturas posteriores). Regla en `HUD_SYSTEM.md` §9.3 y `ATRIBUCION_DE_CICLOS.md` (antipatrón 4).
3. **Rellenos del HUD por CPU.** El `apply` del HUD costaba ~208k ciclos/frame: cada `fill_rect` de la franja (pips 8×8, deltas, marcos) superaba el umbral de 64 px de `AccelMode::Auto` y caía al Blitter **inmediato** (submit+wait ~10-14k por rect). `HudStrip::bind` fija ahora `AccelMode::Cpu` en el lienzo de la franja (la ruta CPU de referencia del HUD); el frame pasó de 269k (2 campos, ~26 fps) a 142k (1 campo, 49,9 fps). El caso medido queda en `ATRIBUCION_DE_CICLOS.md`.

### Cookie-cut del actor (defecto reportado por el usuario y corregido)

**Síntoma (reportado).** «El sprite que se mueve no recorta bien su perfil (no hace un buen cookie-cut)»: al cruzar los bloques estáticos, el actor dejaba una **banda negra** de 16 px a su derecha (invisible sobre el fondo negro, visible sobre los bloques de color). El modelo de visión **no lo detectó** porque las capturas empezaban en f≈26 y el cruce ocurre en f≈12..24: la fase crítica no estaba muestreada.

**Causa.** El actor se dibujaba `BobDraw::Opaque` (`D = A`) con hoja de 2 palabras por fila (arte + guarda): con `x` no alineada a 16 el blit escribe la ventana completa y la **guarda** (ceros) se pinta como banda del color 0. Es una trampa del API que ahora documenta `BobDraw::Opaque` (`bob.hpp`).

**Arreglo.** Hoja **par** de cookie-cut (`BobMaskPack::InterleavedPair`: `[imagen][guarda][máscara][guarda]` por fila y plano), `BobDraw::CookieCut`, `TransparencyMode::Mask1Bit` y el **Bob del actor fijado explícito** tras `add_actor` (el camino `bob_from_visual` reconstruye el Bob desde el `Visual` y **no puede expresar la hoja par**: dejaba `SeparatePlane` sin máscara y el ejecutor rechazaba el dibujo **en silencio** — el actor no aparecía; patrón de la 121, documentado en `OBJECT_SYSTEM.md`).

**Evidencia objetiva (cruce completo, 16 frames con settle 150 ms).** Dentro del bloque (40,150,24,24) en cada frame del cruce: `intacto + actor = 576/576` y **NEGRO = 0** (antes: banda negra); al terminar el cruce el bloque queda **576/576** (save-under). El gate `verify-hud-band.mjs` lo comprueba de forma determinista («sin banda negra en el bloque durante el cruce (14 frames con el actor encima)» + «bloque intacto al final (576/576)»).

**Visión dirigida al contorno** (frames f0015/f0018/f0021, sobre el bloque):

> «Contorno limpio: **Sí**. Banda o halo: **No**. Restos: **No**. […] el cuadrado claro tiene un contorno definido y limpio, sin bordes difusos, píxeles colgantes o artefactos […]; no hay ningún resto de la posición anterior […].»

**Prevención para futuros hilos.** El prompt canónico (`tools/analyze/vision-prompt.txt`) pide ahora examinar explícitamente el contorno de los objetos en movimiento y el fondo inmediatamente alrededor; el procedimiento (regla 21) exige que las capturas incluyan las **fases críticas** (objeto sobre fondos con color) y no solo sobre negro.

## Visión (protocolo A/B/C, `DEMO_VISUAL_DEBUG.md` §6.4)

### A — Inventario sin contexto (prompt canónico, 6 frames; `src/VISION.md`)

> «Fondo: negro sólido. […] bloque rectangular vertical más alto de color azul oscuro, y un bloque cuadrado más pequeño de color azul claro a su derecha. […] SCORE […] “000001” en amarillo. […] barra de progreso horizontal […] parcialmente llena de amarillo (aproximadamente un 20-30%). […] LIVES […] cuatro cuadrados amarillos. […] “HUD BAND v1.0.4638 F 00000 FPS 00.0”. No hay ningún cambio, movimiento, parpadeo o artefacto entre las imágenes.»

### B — Lectura de la línea de depuración (frame de un run normal, 8 s)

> «HUD BAND v1.0.4637 F 00400 FPS 49.9» — versión 1.0.4637, 400 frames transcurridos, **49.9 fps**.

### C — Verificación dirigida de la dinámica del actor (frames f0033–f0035, una pasada)

> El modelo respondió que el cuadrado azul claro «ha desaparecido» en f0035. **El modelo erró**: el conteo de píxeles de esa misma captura da 1024 px (actor completo) en x86..101, y en las 12/12 capturas el actor está presente en la posición esperada. Se registra el crudo porque un fallo del modelo de visión es evidencia relevante del proceso; la capa determinista (conteo de píxeles + verificador de franja + flicker) es la que manda.

## Conclusiones

- El frame entra en **1 campo** (49,9 fps medidos en pantalla) con el HUD por deltas, el actor por el sistema de objetos (SaveUnder) y los rellenos de la franja por CPU.
- **Sin parpadeo ni daño de fondo**: actor presente en todos los frames capturados (16/16, 12/12) y bloques estáticos intactos; el gate de flicker queda OK y el modelo canónico no ve artefactos.
- **Decisión H1** (actualiza `HUD_SYSTEM.md` §6.3): la franja se escribe *in-place*; la estabilidad depende del **orden de pintado por banda** (capa viva primero) y de la **política CPU de sus rellenos**, no de doble buffer. El doble buffer por punteros queda para contenido de banda escrito a media pantalla o con más carga.
- El claim de «desbordamiento de pila» de versiones anteriores de este informe era **una diagnosis errónea**: el runner da 128 KB de pila y un local en `main` funciona; el estado del juego vive en BSS por convención (no depender del lanzador), no por un desbordamiento. El fallo real era el repintado sin restaurar el fondo y la carrera haz/Blitter.

## SIN VERIFICAR

- Build **release** (solo se midió `A500_debug`).
- Estabilidad en ejecución larga (>20 s) y con Fast RAM presente.
- La ruta del atlas de iconos (`IconAtlas`/`Blit`) no se usa en esta demo (llega en H6); el estado `warn` rojo se validó por color en la secuencia de 250 ms (rojo 256–976 px), no en la ventana frame a frame.
- Interacción (H2/H3): la demo no consume ratón.
- `measure-fps.mjs` resultó **no fiable** en este entorno (abortaba con «dFrame=0» o daba valores incoherentes); la medida de fps se tomó con la ventana `HudFpsWindow` visible en pantalla y con la ventana del perfilador.
- **Capturas por paso**: en este entorno el vídeo se congela a tramos (DT-023(d)/DT-035) y las capturas pueden mostrar frames viejos (sprite "ausente" con el framebuffer correcto). Por eso la evidencia principal de render es de **memoria** (self-check de bitplanes + contadores); la visión se tomó sobre las capturas disponibles (mayoritariamente correctas) y el contorno del sprite se validó además contra el framebuffer.
