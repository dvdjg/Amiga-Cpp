# VALIDATION — 001_adv_walk

## 1. Qué se ha verificado (y con qué)

| Afirmación | Evidencia | Alcance |
|---|---|---|
| El actor camina por la **ruta de walkboxes** al destino del guion | Secuencia `out/run/adventure_amiga_001_adv_walk/A500_debug/sequence/` (12 frames): el actor cruza el mundo y `WP` avanza `0/2 → 1/2 → 2/2` en el HUD | Mundo propio de 3 cajas; destino dentro de la unión |
| La **cámara** sigue con dead-zone y límites | `CAM` pasa de 0 (actor dentro de la zona muerta) a 2 al acercarse al este y el contenido se desplaza 2 px con el puntero pegado a pantalla | Eje X; clamp [0..32] (mundo 352, vista 320) |
| El **puntero** (bob) sigue al ratón y se desplaza con la cámara | Run con `--mouse-click-at -80,-40`: la flecha aparece en el mar y se mueve; tras el paneo se mantiene en su posición de pantalla | Inyección relativa del runner (deltas de `JOY0DAT`) |
| El **click** fija destino en coordenadas de mundo | El actor cambia de rumbo tras el click inyectado (frames posteriores) | Destino dentro de una caja; click fuera se ignora |
| El **HUD** (banda 1bpp) y la **versión** se muestran | `src/VISION.md` (modelo) y capturas: `001 ADV WALK`, `v1.0.4891`, `FRM/CAM/WP/BF`, `MIRA: PUERTA`/`PASEO...` | Texto por palabras; contador de blits fallidos `BF = 0` |
| **Sin artefactos** en el perfil de los bobs y el fondo | Respuesta cruda en `src/VISION.md`: «contornos limpios… sin bandas, halos ni píxeles cortados», «el fondo inmediatamente alrededor… permanece igual» | 6 frames repartidos de la secuencia de 12 |
| **Chip presupuestado** | `configure_memory({160 KiB, 8 KiB, 8 KiB})` + escena compuesta con validadores del engine (`scene::compose` rechaza lo que no cabe) | A500 PAL, debug |

## 2. Presupuesto: NO VERIFICADA (coste de la HUD abierto)

Medida con la demo commiteada + fixes de este turno (measure-fps, sin warp): **~10–17 fps** (~4,6 campos/frame). Objetivo del repo: 1 frame por VBlank (50 Hz).

Estado de la investigación (detalle en docs/debugging/investigaciones/001-adv-walk-pacing.md):

- Una tanda inicial de ablaciones fue **inválida**: uild.args con BOM UTF-8 hacía que DEMO_FLAGS se ignorase en silencio (builds «diferentes» con el mismo binario). **Corregido** en uild-demo.sh.
- Ablaciones rehechas con flags efectivos: sin update → **~49,5 fps**; sin HUD dinámica → **~49,4 fps**; sin blits → ~17,7 fps. El coste es el **redibujo completo del HUD** (~230 000 ciclos/línea, ~1 500 por palabra por la vía virtual del playfield); los blits son despreciables.
- **Corregido**: engine.set_blit_service_enabled(false) (el servicio de Blitter de fondo competía con los litter_submit síncronos: estela y bloqueos intermitentes).
- **Corregido**: clamp de la ventana de los bobs al bitmap (con el actor en y=186 el blit escribía filas fuera del plano; el puntero podía salir por la derecha; corrupción/bus error).
- **Abierto**: el intento de HUD por diferencias (redibujar solo palabras cambiadas) degradó la cadencia de forma no explicada (~0,1 fps) y se revirtió; falta perfil por función de draw_text_words/store_plane_word y una primitiva por filas de una sola llamada.
- **Flicker**: no medido (cadencia fuera de presupuesto).

## 3. Prompts de visión y respuestas crudas

- Evaluación canónica (única que va al informe): `src/VISION.md` (generado por `tools/analyze/vision-final.mjs` con `tools/analyze/vision-prompt.txt`, 6 frames de la secuencia, modelo `qwen3-vl:8b-instruct-q8_0`).
- Preguntas de depuración (no entran en `VISION.md`): consulta puntual a un frame final — «¿hay un sprite de personaje en la playa? ¿escena con cielo/mar/arena? ¿barra de texto abajo?» → «Yes… Yes… Yes».
- Nota de lectura del informe: el modelo describe **dos figuras** porque la **flecha del puntero** (blob de 8×11) y el actor comparten aspecto a esa escala; es el puntero del ratón, no un segundo personaje.

## 4. Exclusiones declaradas

- **SIN VERIFICAR**: cadencia 50 fps, estabilidad de cadencia (tirones), flicker, comportamiento con ratón real (solo se probó la inyección por deltas), destino fuera de las walkboxes con varios saltos de caja (se probó el caso del guion de 2 waypoints), y coste medido por secciones (`ENG_PROF_*`) más allá del perfil de CPU agregado.
