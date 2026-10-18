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

## 2. Presupuesto: NO VERIFICADA (bug de cadencia abierto)

Medida (`node tools/debug/measure-fps.mjs adventure_amiga_001_adv_walk A500_debug --json`):

```
emulado=10.8 fps (media con arranque) | estable ~16,6 fps | 659.000–668.000 ciclos/frame ≈ 4,65-4,71 campos/frame
```

- Objetivo del repo: **1 frame por VBlank (50 Hz), sin tirones**. La demo queda **NO VERIFICADA**: la cadencia estable (~16,6 fps ≈ 50/3) incumple el umbral por un factor ~3.
- **Síntoma**: con el `update` **vacío** (ablation `-DADVWALK_NO_UPDATE`) la medida es idéntica (~16,6 fps, 4,65 campos/frame, CPU 100% ocupada en el perfil `winuae-profile`). La cadencia no depende del trabajo del frame.
- **Descartes ya realizados** (todos sin efecto sobre la cadencia): quitar el dibujo del HUD; quitar los blits del actor/puntero; quitar la sección Copper de la banda HUD; ventana DDF estándar `$38..$d0`; `run_frames_polling` en vez de `run_frames`; añadir `eng::os::init`; activar/desactivar el servicio de Blitter de fondo.
- **Control positivo en el mismo entorno**: `rpg_amiga_001_rpg_dungeon` mide **48,1 fps (1,04 campos/frame)** y `engine_amiga_064_shmup_wave` **43,5 fps (1,15)** con el mismo `measure-fps` y emulador → el entorno y el mecanismo de medida funcionan; la anomalía es de esta demo (estructura `Engine` + `scene::compose`/`takeover` propias, frente a `eng::App`/`GameDisplay` de las dos de control).
- Hipótesis viva para el siguiente turno: el bucle de `run_frames` consume ~3 campos por iteración con esta composición (¿interacción del `wait_vblank` por señal/VPOS con la lista de Copper propia?); contrastar portando la demo a `eng::App`/`GameDisplay` (camino ergonómico, gate `demo-ergonomics`) o instrumentando `hb.frames`/`frames_elapsed` en el HUD.
- **Flicker**: no medido (una cadencia inestable no permite declarar `flicker-baseline.json` con garantías).

## 3. Prompts de visión y respuestas crudas

- Evaluación canónica (única que va al informe): `src/VISION.md` (generado por `tools/analyze/vision-final.mjs` con `tools/analyze/vision-prompt.txt`, 6 frames de la secuencia, modelo `qwen3-vl:8b-instruct-q8_0`).
- Preguntas de depuración (no entran en `VISION.md`): consulta puntual a un frame final — «¿hay un sprite de personaje en la playa? ¿escena con cielo/mar/arena? ¿barra de texto abajo?» → «Yes… Yes… Yes».
- Nota de lectura del informe: el modelo describe **dos figuras** porque la **flecha del puntero** (blob de 8×11) y el actor comparten aspecto a esa escala; es el puntero del ratón, no un segundo personaje.

## 4. Exclusiones declaradas

- **SIN VERIFICAR**: cadencia 50 fps, estabilidad de cadencia (tirones), flicker, comportamiento con ratón real (solo se probó la inyección por deltas), destino fuera de las walkboxes con varios saltos de caja (se probó el caso del guion de 2 waypoints), y coste medido por secciones (`ENG_PROF_*`) más allá del perfil de CPU agregado.
