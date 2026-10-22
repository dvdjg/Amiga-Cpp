# VALIDATION — 224_sprite_reuse_carousel

## Estado

**Verificada en los cuatro perfiles (A 3 planos, B 4, C 5, D 6 EHB)**: render, movimiento continuo, 20 s de ejecución estable y ~50 fps; secuencia revisada en A/B/C/D y visión canónica en A. Queda pendiente el gate formal de flicker. La demo se sincroniza con `VblankWait::Polling` (sondeo de VPOSR) porque bajo el runner AmigaDOS sigue vivo y reescribe `INTENA` (familia 212/DT-011); ver «Cambio de sincronización». El bootstrap del engine ahora ejecuta el takeover de sistema (`Forbid` + `LoadView(NULL)` + `OwnBlitter`) antes del display y garantiza `restore_system()` al salir.

## Matriz de ejecución

| Perfil | Build/run | fps / fieldsPerFrame | Ciclos/frame | Frames | Flicker | Placements | Canales | Reuso | Visión |
|---|---|---:|---:|---:|---|---:|---|---:|---|
| A — 3 planos | OK (`A500_release`) | **49,87 / 1,00259** | **142.243** | 1009 (20,25 s) | **OK** (regresión) | 40 | 0xff | 5 | canónica OK |
| B — 4 planos | OK (`A500_k224_planes4_release`) | **49,92 / 1,00159** | **142.102** | 1014 (20,35 s) | no medido por perfil | 40 | 0xff | 5 | secuencia revisada |
| C — 5 planos | OK (`A500_k224_planes5_release`) | **49,92 / 1,00159** | **142.102** | 1010 (20,26 s) | no medido por perfil | 40 | 0xff | 5 | secuencia revisada |
| D — 6 planos EHB | OK (`A500_k224_planes6_release`) | **49,87 / 1,00259** | **142.243** | 1011 (20,31 s) | no medido por perfil | 40 | 0xff | 5 | EHB, secuencia revisada |

`detail = 0xff0528`: 40 placements (`0x28`), máxima reutilización 5 (`0x05`) y máscara de canales `0xff`. Ambas medidas son `stable: true`, `quantizedFields: 1`, con ventanas entre 49,52 y 50,32 fps.

## Medidas

- `node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_release --json --duration-ms 20000` → 49,87 fps emulados / 49,83 host, 142.243 ciclos/frame, 1,00259 campos/frame, 1009 frames, `detail=0xff0528`, `stable: true`.
- `node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes4_release --json --duration-ms 20000` → 49,92 fps emulados / 49,84 host, 142.102 ciclos/frame, 1,00159 campos/frame, 1014 frames, `detail=0xff0528`, `stable: true`.
- `node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes5_release --json --duration-ms 20000` → 49,92 fps emulados / 49,86 host, 142.102 ciclos/frame, 1,00159 campos/frame, 1010 frames, `detail=0xff0528`, `stable: true`.
- `node tools/debug/measure-fps.mjs 224_sprite_reuse_carousel A500_k224_planes6_release --json --duration-ms 20000` → 49,87 fps emulados / 49,79 host, 142.243 ciclos/frame, 1,00259 campos/frame, 1011 frames, `detail=0xff0528`, `stable: true`.
- Secuencias: A 155, 212, 268, 325, 382, 439, 497, 554 (final 590); B 156, 214, 272, 329 (final 366); C 152, 209, 267, 324, 382, 440, 497, 553 (final 590); D 153, 211, 267, 327, 385, 443 (final 480). `frame-diff` con 16.864–20.239 píxeles cambiados por par (D: 18.394–19.431) y contornos limpios. `temporal-detect` marca candidatos asociados al movimiento de los sprites (72 en A, 43 en D); el gate formal de flicker sigue pendiente.
- Telemetría de la demo: `placements=40`, `channels=0xff`, `reuse=5`, `patched=160` words por frame.
- Flicker (canónico `A500_debug`): `tools/test-regression.sh --flicker --require-flicker-ok --demo ...` → informe `out/regression/20261022-213313/regression-report.md`, columna **Flicker = ok** y `Regression OK` (0 candidatos). El aviso `white=0 < 10` es del overlay genérico de análisis, que no aplica a esta demo (su HUD es overlay del host, no bitplanes); la columna `Analyze` del informe es `ok`.

## Cambio de sincronización (212/DT-011)

Con `VblankWait::Active`, la build de 5 planos con rotación se congelaba de forma determinista en el frame 198-200 (unos 4 s). Evidencia del parón: el frame de run-status dejaba de avanzar; `INTENAR` quedaba en `0x2000`/`0x7001` con el bit VERTB (`0x20`) **a 0**; `INTREQR=0x20` (VERTB pendiente); `g_vblank_seq` atascado en ~199-201; `g_vbl_task` no nulo; la CPU quedaba en el bucle de VPOSR de `wait_vblank_run`, con el DMA/Copper en estado válido. Bajo el runner, AmigaDOS sigue vivo y reescribe `INTENA`; el rearme del engine no recupera el latido. La demo usa ahora `run_frames_polling`, que sincroniza por VPOSR y no depende de esa IRQ: las dos builds miden 20 s estables.

El bootstrap del engine ejecuta `takeover_system()` (`Forbid` + `LoadView(NULL)` + `OwnBlitter`, guardando DMACON/INTENA/ADKCON) **antes** de `set_display`, y `restore_system()` en un `scope_guard` al salir. Se aplica a `Active`/`Polling`; se excluye `Signal` porque `Wait()` con `Forbid` activo puede bloquear la tarea. Bajo AmigaDOS el servicio de nivel 3 usa ahora `AddIntServer(INTB_VERTB/BLIT)` (stubs ASM con Z activo), no el vector crudo: con `Active`, 3 y 5 planos miden 20 s estables (49,87/49,92 fps). La build de **4 planos** con `Active` sigue teniendo un parón tardío (~1150 frames) sin aislar, así que la demo usa `Polling`, que no depende de esa cadena y está verificada en los cuatro perfiles.

A/B realizado (una variable):

| Prueba | Resultado |
|---|---|
| 5 planos, rotación desactivada (1 lista) | estable 20 s, 49,97 fps |
| 5 planos, 5 listas, commit de COP1LC cada 50 frames | se congela en 198-200 |
| 5 planos, commit a la **misma** dirección | se congela en 198-200 (el contenido de la lista no importa) |
| 5 planos, commit al inicio del render (VPOS=311, margen antes del wrap) | sigue congelándose con Active (no es la carrera del wrap) |
| 3 planos, rotación, Active | 20 s estables en las ejecuciones buenas |
| 5 planos, sin GDB ni sesión MCP | también se congela (no es un halt del depurador) |
| 5 planos, `Active` + takeover + `AddIntServer` | **49,87–49,92 fps / 1 campo**, 20 s estable |
| 4 planos, `Active` + takeover + `AddIntServer`, **sin GDB** | **35 s estable**; servidor VERTB ≈1 llamada/frame (`adds=1`, `calls≈frames`) |
| 4 planos, `Active` + takeover + `AddIntServer`, con `measure-fps`/GDB | parón tardío; perturbación del cliente de medida, no del engine |
| 4 perfiles, `Polling` + takeover | **49,87–49,92 fps / 1 campo**, 20 s estable |
| Auditoría de rangos en runtime | planos `0x15070..0x21870`, sprites `0x2394c..0x245f0`, copperlists `0x258a0..0x260a0`: **sin solapes** |

El commit de COP1LC se publica al **inicio** de `render` (VPOS=311, una línea de margen) y se parchea después la lista siguiente, ya inactiva; así se evita la ventana del wrap (`WinUAE-DBG/custom.cpp:2921-2942`, `copper.md` §3). No fue la causa del bloqueo, pero es el orden correcto.

La hipótesis de que el engine pisaba memoria quedó **refutada** por la auditoría de rangos. `takeover_system()`/`restore_system()` existen en el engine, pero `run_demo` no los llama; solo las demos 066 y 119 lo hacen. La consulta externa abierta sobre la secuencia canónica de takeover en KS 1.3 está en `docs/debugging/investigaciones/consultas/consulta-224-intena-vertb-takeover-en.md`.

## HUD

- La franja inferior (bitplanes) muestra nombre y versión: `224 SPR` + `version_label`.
- El contador de frames y los FPS se dibujan en el **overlay de depuración del host** (`app.debug().text`), en la esquina superior izquierda: `F<frame> <fps>FPS` (p. ej. `F349 50.0FPS`). El overlay de WinUAE es aditivo (`debug.cpp:1796-1823`), así que se limpia con `app.debug().clear()` antes de cada actualización para que los dígitos no se superpongan.
- Contadores del periférico: `fps10` (slot 0) y `frame` (slot 7); la ventana de FPS se cierra con el reloj PAL y usa `frames * 10` (sin división 32-bit).
- Verificado en captura: `out/tmp/224_hud_overlay.png` y la secuencia de `A500_release`.

## Visión

`src/VISION.md` (prompt canónico, 2 frames de la secuencia actual). Respuesta limpia sobre contenido: fondo de cuadrícula azul/verde estable; sprites geométricos multicolor repartidos; contornos limpios, sin halos, sin píxeles cortados, sin restos de la posición anterior y sin rectángulos opacos tapando el fondo; leyó `F349 50.0FPS` y `224 SPR v1.0.5986` en la captura. El modelo afirmó que no había movimiento (falso negativo: trató las dos imágenes como una); el **frame-diff** y la secuencia lo refutan: frames de run-status 152→553 y 16.864–20.239 píxeles cambiados por par. Prevalece la medida objetiva (`DEMO_VISUAL_DEBUG.md` §6.4).

Nota: la lectura del texto del HUD por el modelo de visión no se usa como medida numérica (alucina/degrada los dígitos); la evidencia de fps es `measure-fps`. Con 8 imágenes el modelo degeneró en un bucle de texto; se reinició la instancia (`ollama stop`) y se repitió con 2 frames, que dio la respuesta limpia registrada.

## Sin verificar

- Flicker por perfil B (4), C (5) y D (6 EHB): el gate formal se ejecutó en el perfil canónico A (3 planos). `temporal-detect` no sustituye el gate y sus candidatos coinciden con el movimiento de los sprites.
- Contraste dirigido C contra `INTENT.md` por banda y lane en las cuatro profundidades.
- Uso total de CPU: el sondeo de VPOSR es una espera activa; no se ha medido la ocupación de CPU (solo el coste del frame).
- Secuencia de salida al SO (`restore_system`) en una demo que termine.
