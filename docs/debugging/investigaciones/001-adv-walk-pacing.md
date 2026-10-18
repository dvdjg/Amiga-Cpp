# Cadencia ~1/3 del VBlank en 001_adv_walk (bug abierto)

Síntoma, evidencia y descartes de la anomalía de cadencia de la demo `demos/features/adventure/amiga/001_adv_walk` (A0, capa `eng::adv`). Registrado aquí como error **ajeno al hilo** que lo encontró (turno del 2026-10-18); el objetivo siguiente es aislarlo con instrumentación de `hb.frames`/`frames_elapsed`.

## Síntoma

- `node tools/debug/measure-fps.mjs adventure_amiga_001_adv_walk A500_debug --json` → **~16,6 fps estables** (media con arranque 10,8), **~4,65-4,71 campos por frame** (~660.000 ciclos CPU por frame), CPU al 100 % en `winuae-profile` (142.102 ciclos ocupados por campo, 0 libres).
- El HUD de la demo muestra `FRM` avanzando a ese ritmo también en `--warp` (no es throttle del host).
- **Con el `update` vacío** (ablation `-DADVWALK_NO_UPDATE`, `update()` retorna sin hacer nada) la cifra es **idéntica** (4,65 campos/frame): la cadencia no depende del trabajo del frame.

## Control positivo (mismo entorno, misma herramienta)

| Demo | medida |
|---|---|
| `rpg_amiga_001_rpg_dungeon` (`eng::App` + `GameDisplay`) | **48,1 fps**, 1,04 campos/frame |
| `engine_amiga_064_shmup_wave` (`os::init` + `run_frames`) | **43,5 fps**, 1,15 campos/frame |

⇒ ni la herramienta ni el emulador están mal; la anomalía es específica de la composición/estructura de la demo afectada (usa `eng::Engine` + `eng::os::init` + `scene::compose`/`Scene::takeover` + parcheo de ventana propios, en vez de `eng::App`/`GameDisplay`).

## Descartes realizados (ninguno cambió la cadencia)

1. Sin dibujo de HUD (`-DADVWALK_NO_HUD`): 4,65 campos/frame.
2. Sin blits del actor/puntero (`-DADVWALK_NO_BLITS`): 4,65.
3. Sin la sección Copper de la banda HUD (`-DADVWALK_NO_BAND`): 4,71.
4. Ventana DDF estándar `$38..$d0` + MOD de fila (`-DADVWALK_PLAIN_DDF`): 4,69.
5. `run_frames_polling` en vez de `run_frames` (señal IRQ): 4,71.
6. Añadir `eng::os::init(engine, 0u)` (patrón de 064): 4,69.
7. Servicio de Blitter de fondo activado (por defecto) vs desactivado: igual.

## Hipótesis viva y siguiente paso

Con `update` vacío, el bucle del engine debería completar un frame por campo; se observan ~4,65. Sospechosos a instrumentar en el siguiente turno:

1. Contar `context.frame.frames_elapsed` (catch-up del bucle) por frame en el HUD para ver si el bucle **se pierde latidos** (esperado 1) o si `wait_vblank` se resuelve cada ~3 campos.
2. Leer `hb.frames` en un hook de VBlank propio y compararlo con el contador del bucle (¿la IRQ de VBlank se sirve a 1/3 de rate con esta lista de Copper? ¿el `level3_sync`/re-arme de `INTENA` de `wait_vblank_run` interactúa con el `takeover` de la escena propia?).
3. A/B decisivo: portar la demo a `eng::App`/`GameDisplay` (camino ergonómico vigente, gate `demo-ergonomics`) manteniendo el parcheo de ventana si el seam lo permite, y medir de nuevo.
