# Cadencia de 001_adv_walk: causas encontradas y estado (abierto)

Historia de depuración de la cadencia de `demos/features/adventure/amiga/001_adv_walk` (A0). Contiene **hallazgos corregidos** (una hipótesis inicial resultó ser un artefacto del build) y lo que queda **abierto**.

## Hecho 1: las ablaciones iniciales eran inválidas (BOM en `build.args`) — CORREGIDO

`tools/build/build-demo.sh` leía `build.args` tal cual: un fichero guardado con **BOM UTF-8** (p. ej. `Set-Content -Encoding utf8` de PowerShell) hacía que la primera clave fuese `\xEF\xBB\xBFDEMO_FLAGS` y se ignorase **en silencio**. Se detectó porque dos builds con `DEMO_FLAGS` distinto produjeron el **mismo hash de .exe**. Consecuencia: la primera tanda de ablaciones (quitar HUD/blits/banda/DDF/polling) midió **siempre el mismo binario**; la afirmación inicial «la cadencia es 1/3 del VBlank incluso con `update` vacío» era **falsa**.

Corrección aplicada: el script elimina el BOM al parsear (`build-demo.sh`, comentario junto al bloque). Las ablaciones rehechas con flags efectivos:

| Configuración | Resultado (medido con `measure-fps`, `--json`) |
|---|---|
| `update` vacío (`-DADVWALK_NO_UPDATE`) | **~49,5 fps estables** (1,0 campos/frame) |
| Sin dibujo de la HUD dinámica | **~49,4 fps** |
| Sin los blits del actor/puntero | ~17,7 fps (los blits **no** son el coste) |
| Completa (HUD redibujando líneas enteras) | **~10–17 fps** (~4,6 campos/frame) |

## Hecho 2: el coste es el redibujo del HUD (≈1 500 ciclos por palabra)

La HUD se repintaba **entera cada frame** (2 líneas × 19 palabras × 8 filas de stores al plano por la vía virtual del `Playfield`: `draw_text_words` → `store_plane_word` con `planeline_for`/`byte_for` virtuales). Medido por ablación: una línea ≈ **230 000 ciclos** (~2 campos); los blits del actor/puntero son despreciables al lado.

- La demo 080 pagaba lo mismo (su techo de 25 fps con HUD de 2 líneas es coherente).
- **Abierto**: por qué el intento de HUD **por diferencias** (redibujar solo las palabras cambiadas), que en teoría cuesta ~2 palabras/frame, degradó la cadencia a ~0,1 fps en vez de arreglarla. El experimento se revirtió para no dejar una regresión; el perfilado por función de `draw_text_words`/`store_plane_word` queda como siguiente paso (posible coste oculto por llamada más allá de las 8 filas: comprobar con `ENG_PROF` alrededor de cada llamada y con el `.amigaprofile`).

## Hecho 3: servicio de Blitter de fondo vs `blitter_submit` síncrono — CORREGIDO

El estado commiteado (tras el refactor de ergonomía) había perdido `engine.set_blit_service_enabled(false)`. Con el servicio de fondo (IRQ) activo, sus blits y los `blitter_submit(j, true)` síncronos de la demo comparten registros del Blitter entre jobs del mismo frame: carreras → **filas del save-under perdidas** (estela de pies/restos del puntero sobre el fondo) y **bloqueos intermitentes** (CPU parada en `wait_blitter`). El slot es exclusivo por contrato (`engine.hpp`). Restaurado con comentario que cita el motivo.

## Hecho 4: ventanas de bob sin recortar (escrituras fuera del plano) — CORREGIDO

La ventana de un bob (`words`=2 palabras × `h` filas) se escribía completa aunque el actor/ratón estuviera junto al borde inferior/derecho: con el actor en `y=186` y `h=24`, el blit tocaba filas 208–209 (fuera de las 208 del bitmap, rebasando al plano siguiente o al bloque vecino); con el puntero pegado al borde derecho, `px+32 > 368`. Resultado: corrupción intermitente y bus error. **Clamp aplicado** en `blit_stash` (la región de stash, y `px/py` que usan restore y draw) y el draw usa `b.py`; el guion se ajustó a `y=184`.

## Estado

- La demo vuelve a ser la versión commiteada (HUD de línea completa) **más** los fixes 3 y 4: escena limpia, sin cuelgues, visión OK (`src/VISION.md`).
- **NO VERIFICADA por presupuesto**: ~10–17 fps (objetivo 50). El siguiente paso medible es arreglar el coste por palabra de la HUD (perfil por función + primitiva de escritura por filas de una sola llamada, o HUD por diferencias con el coste medido y explicado) y volver a medir con `measure-fps` (sin warp).
