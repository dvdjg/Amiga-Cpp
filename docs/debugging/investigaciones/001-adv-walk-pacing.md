# Cadencia de 001_adv_walk: investigación (RESUELTA)

Historia de depuración de la cadencia de `demos/features/adventure/amiga/001_adv_walk` (A0). **Resuelta**: la demo cierra con ~48-49 fps estables (~1,0 campos/frame), flicker OK y visión canónica OK (`VALIDATION.md` de la demo). Este documento conserva los hechos que costaron la investigación.

## 1. El bug de herramienta que falseó las primeras ablaciones (BOM)

`tools/build/build-demo.sh` leía `build.args` tal cual: un fichero guardado con **BOM UTF-8** hacía que la primera clave se leyese como `\ufeffDEMO_FLAGS` y se ignorase **en silencio**. Se detectó porque dos builds con `DEMO_FLAGS` distinto daban el **mismo hash de .exe**. Consecuencia: la primera tanda de ablaciones (quitar HUD/blits/banda/DDF/polling) midió **siempre el mismo binario**; la afirmación inicial «la cadencia es 1/3 del VBlank incluso con `update` vacío» era falsa. El parser elimina el BOM desde el fix (`build-demo.sh`); la norma «sin BOM» quedó en `BUILD_AND_RUN.md`.

## 2. La causa de los parones: carrera de despertar del VBlank

Con el bucle por señal (`run_frames`), ciertas fases del frame perdían el despertar: el CPU quedaba dormido y el contador de frames se congelaba (evidencia por canal lateral: PC fijo y ciclos parados). La tasa de pérdidas dependía del trabajo por frame, así que **contaminaba todas las medidas**: explica la «contradicción» 42 vs 17,6 fps con los mismos stores (variantes con distinta fase) y el 0,1 fps del HUD por diferencias. Clase de fallo documentada en `212-vblank-irq-apagada-por-el-so.md`.

**Fix:** `backend.set_vblank_sleep(false)` + `run_frames` (latido por IRQ + espera activa, la variante recomendada del engine). El sondeo puro (`run_frames_polling`) también eliminaba los parones, pero la espera activa conserva el modelo por mensajes.

## 3. El coste real del HUD

Sin HUD: ~49,4 fps. Con las dos líneas completas redibujadas por frame: la cadencia cuantizaba a 25 fps (2 campos) o 16,5 (3): el trabajo del HUD supera un campo. Añadida la primitiva `Playfield::store_plane_words_row` (trozo de línea con el mapeo resuelto una vez) y el **repintado por diferencias** (solo las 1-2 palabras que cambian), el frame vuelve a caber en un campo: ~48-49 fps.

## 4. Otros defectos corregidos en el camino

- `engine.set_blit_service_enabled(false)` perdido: el servicio de Blitter de fondo competía con los `blitter_submit(j, true)` síncronos (filas del save-under perdidas = estela; bloqueos).
- Ventanas de bob sin recortar: con el actor en `y=186` (h=24) o el puntero al borde derecho se escribía fuera del plano (corrupción/bus error). Clamp en `blit_stash` (región e `px/py` compartidos) y guion a `y=184`.

## 5. Medidas finales (A500_debug, sin warp)

- `measure-fps`: ventanas estables **48,0 / 49,5 / 48,0 / 48,7 / 49,1 fps**; ~1,0 campos/frame.
- Flicker: `tools/test-regression.sh --flicker --require-flicker-ok --demo demos/features/adventure/amiga/001_adv_walk --warp` → `Regression OK`; `flicker-baseline.json` `max_candidates: 0`, `max_blocks_low: 0`.
- Visión: `src/VISION.md` (contornos limpios, movimiento y HUD coherentes; la segunda «figura» es la flecha del puntero).

## Lección de método

Toda medida de cadencia de una demo con `run_frames` debe declarar el **modo de espera de VBlank**: los parones por despertar perdido falsean medias y comparaciones A/B (se vieron ratios de 2,4× entre variantes equivalentes). Antes de perseguir un coste, comprobar que el guest no se ha dormido.
