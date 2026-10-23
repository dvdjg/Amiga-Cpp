# VALIDATION — 121_proc_corner_layers

Informe de validación (F3/F4) de la demo del **mundo procedural cocido a tileset** y el **motor de tiles X-Limited**: estructuras multi-tile (río por esquinas, caminos con puente, castillo, casas 2×2 A/B, árboles 2×2), fondo pintado por el Blitter desde el banco de tiles y un **actor de 16×16** (Blitter, cookie-cut) que pasa por debajo de las copas. Todo lo medido es sobre `A500_debug` (warp); el alcance de cada afirmación está acotado en §5.

## HUD de host y medida pendiente

El HUD de depuración vive en el overlay del host (`HostHud`) y la franja de 48 px queda negra. En esta campaña `measure-fps` agotó el READY (detail `0xff6141`) en dos pasadas, aunque `run-demo` arranca y renderiza; no se declara una cifra nueva. La medida histórica de 47,67 fps del HUD en viewport se conserva como referencia, no como validación vigente.

## 1. Intención (F0)

Mundo de 320×208 (20×13 tiles de 16 px, 32 colores del atlas procedural) + HUD de depuración en el overlay del host (HostHud) y franja de 48 px negra reservada para la geometría. Contenido del mundo: río N→S con lago e isla (transiciones de esquinas), caminos (rectas/codos/curvas) con **puente** sobre el cauce, **castillo 3×3** (muralla por casación:
esquinas, torre, puerta sur), **dos casas grandes 2×2** (variantes A/B con la puerta espejada), **árboles grandes 2×2** (copa continua), arbolitos, flores (decal) y **copas (overhead)** sobre el carril. El fondo se **pinta una vez con el motor de tiles** (sin C2P ni composición por píxel); por frame solo hay Blitter para el actor (restaurador de ventana + dibujo enmascarado) (el HUD ya no se pinta en el viewport). Criterio de éxito visual: estructuras que casan (caminos encadenados, muralla cerrada, casas/troncos continuos), río y copas íntegros, y el bicho blanco (color 31 exclusivo) caminando el carril a 1 px/frame, **oculto bajo las copas**.

## 2. Gates técnicos (F3)

- `bash tools/test-regression.sh --flicker --require-flicker-ok --demo demos/techniques/amiga/effects/121_proc_corner_layers --warp`
  → **Regression OK** y **flicker OK** (`out/regression/20261016-183113/regression-report.md`;
  `flicker-baseline.json` con `max_candidates: 0`).
- `node tools/debug/measure-fps.mjs 121_proc_corner_layers A500_debug --json` → **3/3 pasadas limpias: 47,67 fps / 1,049 campos/frame / 148.798 ciclos/frame** (955 frames, 20 s;
  `detail` = actor x | dir, p. ej. `0xffff00c6` = x=198, dir=−1). El plan del frame es **uno solo** (restaurador + actor; el flake del plano 0 quedó resuelto en el motor por el contrato de espera por job, `blitter-busy-writes.md`) y la guarda de render relee un píxel por frame. Verificación de la vuelta al plan único: **7/7 arranques limpios** con telemetría viva; medida estable frente a los dos planes (el coste era la espera, no trabajo extra).
- **Presupuesto por elemento**: por frame solo hay (a) restaurador de **ventana de 2 palabras** × 16 filas × 5 planos desde el fondo del carril, (b) actor (16 bandas de subpíxel precomputadas; 2 palabras × 16 filas × 5 planos enmascarados con `actor & ~copas`) y (c) contador del HUD cada 64 frames (palabras, 1 plano). El fondo no se recompone.
- Tests host del dominio en verde: `HOST-442/443/444/445/448/450` (pintores de estructuras 2×2, cocido con dedup, banda planar, equivalencia `Span`).

## 3. Contratos objetivos

**Actor visible (campo abierto)**: en las capturas donde el actor está fuera de las copas, el color exclusivo `#eeeeee` (índice 31 del atlas) aparece con **552 px exactos** en un bbox de 16×16 en la fila del carril — medido en el render congelado (`K121_FREEZE_ACTOR_RENDER`, mismo camino de blits sin per-frame) y en capturas normales tras `READY`
(bbox campo `x 112..127`, `y 144..159`, con el ojo `#443300` índice 2 en su columna).

**Oclusión bajo copa**: con el actor bajo la celda de copa (p. ej. x=75, frame de settle), el blanco del actor es 0 px — la máscara combinada `actor & ~copas` no escribe bajo el follaje, por diseño.

**Auto-chequeo de init (hardware)**: 18 sondas de cuerpo (`probe_x × probe_sub`), las 16 subposiciones en columna limpia, integridad de las 16 bandas en Chip contra referencia recomputada, y la ruta exacta del bucle (restaurador + actor) con reintento único. Códigos de fallo documentados en `main.cpp` (`0x12100/0x12102/0x12106/0x12107/0x12109/0x12110-0x12114`).

**Guarda de render por frame**: relee el píxel del cuerpo cada frame (color 31; se salta bajo copa) con 3 strikes consecutivos → `detail=0x1212F | x<<8 | body<<24`. Es la guarda que localizó el defecto del plano 0 (DT-022).

## 4. Visión (Ollama local, `qwen3-vl:8b-instruct-q8_0`)

Captura del render congelado (actor estático en campo abierto, `vision-run` sobre `out/tmp/vision121_frozen/frame_000_f0043.png`; crudo en `121_proc_corner_layers_report.md`):

- Prompt (extracto): «Describe la escena completa… ¿Hay un sprite blanco de criatura (cuerpo blanco, dos ojos oscuros) sobre el carril central? … ¿Detectas algún defecto visual: planos desalineados, colores rotos, restos/rastros…? ¿El HUD se lee?».
- Respuesta (cruda, extracto): «…ríos azules, caminos marrones, árboles verdes, casas y un castillo en la distancia. Un actor congelado (sprite blanco con ojos oscuros) está en el centro del carril central. […]»; «**No se detectan defectos visuales**: planos alineados, colores consistentes, no hay rastros de frames anteriores ni partes duplicadas o faltantes en el sprite»; HUD: «PROC STRUCTURES v1.0.4061 F 0» y «X-LIMITED 20×13 PIECES».

## 5. Conclusiones y alcance

- **Mundo procedural cocido + motor de tiles VERIFICADO en hardware en su alcance**: el fondo (260 celdas deduplicadas) se pinta por Blitter y se describe por visión como río, caminos, castillo, casas y árboles coherentes; las estructuras multi-tile casan (puente sobre el cauce, muralla con puerta alineada al camino, casas 2×2 con puerta A/B, copas 2×2 continuas) — validación visual + tests host `HOST-448`.
- **Actor por Blitter VERIFICADO en su alcance**: 552 px `#eeeeee` exactos en bbox 16×16 con el ojo en su columna cuando el actor está en campo abierto; oculto bajo copa (0 px); visión sin defectos. La **intermitencia del plano 0** al mezclar copias + racha cookie-cut en el mismo plan (DT-022) está **mitigada** (planes separados + reintento + strikes) y quedó documentada con evidencia en `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`.
- **Evaluación de visión canónica** (`src/VISION.md`, prompt sin pistas): **re-ejecutada 2026-10-17** con capturas del build por defecto: el modelo **ve al personaje** (describe «un personaje blanco, de forma circular con dos puntos negros (ojos)») además del mundo y el HUD, y declara la escena coherente sin defectos. Las capturas de esta pasada son **idénticas entre sí** (el refresco de vídeo de este entorno automatizado queda parado a los pocos frames; DT-023d), así que el modelo correctamente dice «sin cambios temporales»: el movimiento **sigue SIN VERIFICAR por visión** (el contrato 1 px/frame se apoya en la telemetría/guarda). La evaluación NO se retoca a mano: se re-ejecuta `tools/analyze/vision-final.mjs` con una secuencia con movimiento cuando la captura lo permita (ventana visible o fix del fork).
- **A/B del sistema de actores** (DT-024, **cerrado**): el camino del SISTEMA (`ActorStore` + `Animation` + `SaveUnder` + pack con guarda sobre el puente del anillo) es el **default** de la demo; el camino propio queda tras `K121_ACTOR_LEGACY` como referencia (ambos arrancan limpios). Medida del A/B: **47,87 fps / 148,2k ciclos** (equivalente al propio) y captura estática determinista con los **552 px del cuerpo exactos**. **Golden del sistema**: 16 actores a la vez, **16/16 subposiciones EXACTAS y 0 px de daño colateral** (oráculo `verify-121-actor-golden.mjs`, que ancla el origen de la captura por el blanco identidad del índice 31 — el borde de ventana varía por run — y excluye la unión de patrones en ventanas solapadas). El golden del camino propio (10/16 + colateral) queda como referencia histórica. **Modo costura** (`K121_ACTOR_SEAM`): con el actor cruzando el fin del anillo (288), el reparto de `bob_ring.hpp` se auto-verifica **cada frame en hardware** leyendo del bitmap la parte visible (anillo 286 = 31) y la ENVUELTA (anillo 8, guarda = 31) con la esquina preservando el fondo; con cámara fija el cruce queda oculto por las guardas (evidencia = lecturas del bitmap, no píxeles en pantalla).
- **Marco**: 47,67 fps estables (1,049 campos/frame), 0 candidatos de flicker, regresión OK.
- **Golden por subposición (CERRADO con el sistema)**: el modo `K121_GOLDEN_ACTORS` sobre el camino del sistema (default) pinta los 16 actores a la vez con el pack compartido (máscara silueta en columnas limpias) y política `None`; el oráculo independiente `tools/analyze/verify-121-actor-golden.mjs` reconstruye cada banda en JS desde el patrón real de la demo y compara la captura (paleta identidad) → **16/16 subposiciones EXACTAS y 0 px fuera de ventanas**. El golden del camino propio (bandas por `add_world_bitmap_masked`, 10/16 + ~127 px de colateral por ventanas solapadas y desbordes) queda como referencia histórica; su residual es de ese camino, ya no del default.
- **SIN VERIFICAR**: (a-bis) el resto de subposiciones del golden (las 6 diagnosticadas) y el daño colateral de 127 px hasta el fix de DT-022; (b) la captura `--sequence-step-frames` de esta demo es poco fiable (pausa a mitad de frame; DT-023) y **no** se usa como evidencia de movimiento — el contrato de 1 px/frame se apoya en la telemetría (`detail` por frame) y en el frame-diff del flicker; (c) el camino async del `FramePlan` no se usa en esta demo.
- **Deuda abierta de la demo**: ninguna propia; las del hilo están en DT-022 (motor de blits) y DT-023 (herramientas), con recomendaciones priorizadas en el informe de hallazgo.
