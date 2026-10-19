# VALIDATION — 001_adv_walk

## 1. Qué se ha verificado (y con qué)

| Afirmación | Evidencia | Alcance |
|---|---|---|
| El actor camina por la **ruta de walkboxes** al destino del guion | Secuencia `out/run/adventure_amiga_001_adv_walk/A500_debug/sequence/` (12 frames): el actor cruza el mundo y `WP` avanza `0/2 → 2/2`; `FRM` progresa (29 → 259 en la secuencia de visión) | Mundo propio de 3 cajas; destino dentro de la unión |
| La **cámara** sigue con dead-zone y límites | `CAM` pasa de 0 a 2 al acercarse al este y el contenido se desplaza con el puntero pegado a pantalla | Eje X; clamp [0..32] (mundo 352, vista 320) |
| El **puntero** (bob) sigue al ratón y se desplaza con la cámara | Run con `--mouse-click-at`: la flecha aparece/mueve y mantiene su posición de pantalla tras el paneo | Inyección relativa del runner (deltas de `JOY0DAT`) |
| El **click** fija destino en coordenadas de mundo | El actor cambia de rumbo tras el click inyectado | Destino dentro de una caja; click fuera se ignora |
| El **HUD** (banda 1bpp, repintado por diferencias) y la **versión** se muestran | `src/VISION.md` (modelo) y capturas: `001 ADV WALK`, `v1.0.5103`, `FRM/CAM/WP/BF`, `MIRA:`/`PASEO...` | Texto por palabras; contador de blits fallidos `BF = 0` |
| **Sin artefactos** en el perfil de los bobs y el fondo | `src/VISION.md`: contornos limpios, «sin restos de su posición anterior», fondo «constante y sin artefactos» | 6 frames repartidos de la secuencia de 12 |
| **Presupuesto**: 1 frame por VBlank | `measure-fps` (build final, IRQ+espera activa): ventanas estables **48,0 / 49,5 / 48,0 / 48,7 / 49,1 fps**, ~1,0 campos/frame (227 k ciclos/frame de media con arranque) | A500 PAL, `A500_debug`, sin warp |
| **Flicker** | `tools/test-regression.sh --flicker --require-flicker-ok --demo demos/features/adventure/amiga/001_adv_walk --warp` → `Regression OK`; `flicker-baseline.json` con `max_candidates: 0`, `max_blocks_low: 0` | Secuencia estándar del gate |
| **Chip presupuestado** | `configure_memory({160 KiB, 8 KiB, 8 KiB})` + escena compuesta con los validadores del engine | A500 PAL |

## 2. Historia de los defectos encontrados y corregidos

1. **BOM en `build.args`** (`tools/build/build-demo.sh`): un `DEMO_FLAGS` guardado con BOM se ignoraba **en silencio** (builds «distintos», mismo binario; detectado por hash). Corregido en el parser; norma «sin BOM» en `BUILD_AND_RUN.md`.
2. **Carrera con el servicio de Blitter de fondo**: `engine.set_blit_service_enabled(false)` se había perdido; el servicio (IRQ) y los `blitter_submit(j, true)` síncronos comparten los registros del Blitter → filas del save-under perdidas (estela) y bloqueos. Restaurado.
3. **Ventanas de bob sin recortar**: con el actor en `y=186` (h=24) o el puntero al borde derecho, el blit escribía fuera del plano (corrupción/bus error). Clamp de la ventana en `blit_stash` (región e `px/py` compartidos); guion a `y=184`.
4. **Carrera de despertar del VBlank** (clase `212-vblank-irq-apagada-por-el-so`): con el bucle por señal puro (`run_frames`), ciertas fases del frame perdían el despertar → el CPU quedaba dormido (frames congelados, ciclos parados). Era la causa de TODOS los parones intermitentes y de las medidas contaminadas (de ahí la «contradicción» 42 vs 17,6 fps: distintas fases ⇒ distinta tasa de pérdidas). Corregido con **espera activa** (`backend.set_vblank_sleep(false)` + `run_frames`), la variante recomendada del engine.
5. **Coste del HUD**: redibujar las dos líneas completas cada frame cuesta > 1 campo (la cadencia cuantiza a 25/16,5 fps). Corregido en dos niveles: primitiva del engine `Playfield::store_plane_words_row` (trozo de línea, mapeo una vez; usada por `Surface::draw_text_words`, HOST-449) y **repintado por diferencias** en la demo (solo las 1-2 palabras que cambian por frame). El HUD completo deja de costar más de un campo.

## 3. Prompts de visión y respuestas crudas

- Evaluación canónica: `src/VISION.md` (generado por `tools/analyze/vision-final.mjs` con `tools/analyze/vision-prompt.txt`, 6 frames de la secuencia final).
- Nota de lectura: el modelo describe **dos figuras**; la segunda es la **flecha del puntero** del ratón (8×11), no un actor.

## 4. Exclusiones declaradas

- **SIN VERIFICAR**: 50,0 fps exactos (la medida estable es ~48-49, igual que el resto de demos del repo); comportamiento con ratón físico (se probó la inyección por deltas); destinos fuera de las walkboxes con varios saltos de caja; el coste por palabra del HUD por debajo del umbral de un campo (no se perfila por sección; con el repintado por diferencias el rol de coste deja de importar para esta demo).
