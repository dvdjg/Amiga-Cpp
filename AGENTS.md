# AGENTS

## Regla de idioma (obligatoria)
- Toda documentación y texto en español debe escribirse con ortografía correcta: tildes, eñes y puntuación adecuadas.
- No introducir nuevas frases en español sin corregir (`ejecución`, `análisis`, `depuración`, `diseño`, etc.).

## Qué es este repositorio
- El repo mantiene un proyecto C Amiga legado en `legacy/` (`legacy/Makefile`, `legacy/out/a.exe`) y un flujo nuevo de demos del engine C++23 en `demos/` + `tools/`; no mezclarlos por error.
- Para trabajo del engine, usar los wrappers shell de `tools/` en vez de invocar el `legacy/Makefile`.

## Herramientas locales requeridas
- Windows + Git Bash + Node.js son obligatorios para el flujo de ejecución automatizada (`tools/run/run-demo.sh` -> `dist/tools/run/run-demo.js`). No usar el `bash.exe` de WSL para invocar los binarios `.exe` del toolchain; PowerShell solo se usa cuando el script o la integración con Visual Studio lo exige.
- El toolchain Amiga se resuelve en este orden: `AMIGA_BIN_PATH`, extensión de Cursor y luego extensión de VS Code `bartmanabyss.amiga-debug-*` (versión más alta instalada; el fork local es 1.8.1).
- `tools/run/run-demo.ts` importa dinámicamente `../mcp-winuae-emu/dist/winuae-connection.js` desde el repositorio hermano; si falta, el runner falla antes de abrir WinUAE.

## Comandos canónicos
- Compilar una demo: `bash ./tools/build/build-demo.sh demos/000_toolchain_cpp23 --debug --clean`
- Ejecutar una demo y capturar: `bash ./tools/run/run-demo.sh demos/000_toolchain_cpp23`
- Analizar una demo: `bash ./tools/analyze/analyze-demo.sh demos/000_toolchain_cpp23`
- Regresión completa: `bash ./tools/test-regression.sh`
- Bucle de regresión de una demo: `bash ./tools/test-regression.sh --demo demos/101_ehb_tile_scroll_driver --warp`

## Orden de verificación (no saltar)
- Orden por defecto: `build -> run -> analyze`; `analyze-demo.sh` espera `.exe/.elf/.map` y valida `out/run/<demo>/screenshot.png` si existe.
- `tools/test-regression.sh` ya impone ese orden por demo y ejecuta `analyze-sequence.sh` automáticamente cuando existe.
- La regresión usa build estilo debug por defecto (`--debug` interno). Usar `--release` solo cuando se necesite comportamiento de optimización release.

## Comportamiento clave de runner/emulador
- `run-demo` configura WinUAE con `warp=false` por defecto. Usar `-Warp` solo para throughput/diagnóstico rápido, no para evaluar suavidad visual.
- Las demos modernas deben exponer `g_eng_run_status`; el runner espera `READY` por canal lateral en `127.0.0.1:2346` y falla rápido si no llega.
- El fallback por timeout es opt-in (`--allow-timeout-fallback`) y es para diagnóstico.
- Las capturas de secuencia se limpian antes de cada ejecución (`out/run/<demo>/sequence`) para no mezclar frames antiguos.

## Depuración avanzada (WinUAE-DBG v2.x, vía MCP `winuae-emu`)
Servidor GDB (puerto 2345) + **canal lateral** (2346). Especificación canónica:
`WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md`. **Usar el build x86**
(`winuae-gdb.exe`); el x64 tiene un bug preexistente de boot congelado.

Herramientas MCP disponibles (todos vía `mcp-winuae-emu`):
- `winuae_emulator_status` — telemetría (ciclos, frame, vpos/hpos, warp, baseText, contadores). Para confirmar que el emulador corre y estado global.
- `winuae_watchpoint_set_ext` / `_list` / `_last` / `_clear_ext` — watchpoints con filtro por **origen** (`src=cpu|copper|blitter|bpl0-7|spr0-7|audio0-3|disk|dma`), `value=`, `mask=`, `must_change`, `nobreak`. `watch_last` reporta addr/src/valor/PC del último hit. Para "¿quién/cuándo toca X?" (p. ej. el copper escribe un registro custom).
- `winuae_protect` — `block`/`set` para congelar memoria o forzar valores. Para cheats y tests de estados.
- `winuae_rewind` — `start`/`stop`/`status` (captura); el restore **congela la emulación** pero deja el snapshot legible por canal lateral. Sólo para inspeccionar un estado pasado.
- `winuae_trace` — trazas de eventos watch/protect/rewind en `%TEMP%\winuae-gdb.log` (activo por defecto).
- `winuae_side_read` — canal lateral (`state`/`regs`/`mem <addr> <len>`/`runstatus <addr>`), independiente de GDB. Cuando GDB esté inerte o para observar sin intrusión.
- `winuae_debugperiph` — **periférico de depuración in-Amiga** en `0xB70000` (consola, checkpoints, contador de ciclos, debug args, breakpoints auto-dirigidos). Para telemetría del propio programa y profiling por checkpoints.

**Ejemplo real de periférico**: la demo `demos/101_ehb_tile_scroll_driver` está
instrumentada (`engine/include/eng/debug/peripheral.hpp`): en cada cambio de
tile-set escribe `TILE_CHANGE` a la consola y abre checkpoints 10→11 (coste del
upload). Consulta: `winuae_debugperiph checkpoints` / `console`. Verificación
de scroll en 4 direcciones + diagonal a 50fps (con ollama):
`tools/analyze/verify-scroll-directions.mjs`.

Guía "cuándo usar cada herramienta" e instrumentación de demos:
`docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`.

Fix de scroll fino de la demo 101 (2026-08): el puntero OCS con `DDFSTRT=$30`
debe apuntar UN WORD ANTES de la parte coarse (`fetch = coarse-16`) para que
`display_start = camera`; se añadió **doble buffer de la copperlist**
(`m_copper_block[2]`, rebuild en el bloque inactivo, install hace swap) para que
reconstruir la lista no corrompa el display activo. Pendiente: confirmar
visualmente si el salto en el cruce de tile (cada 16px) desaparece; el análisis
por capturas queda confundido por el timing.

Roadmap del port de features de engine9000 (hecho/pendiente, priorizado para
hilos nuevos): `WinUAE-DBG/docs/WINUAE-MONITOR-EXTENSIONS.md` → sección
"Roadmap del port desde engine9000". Hechos: punto 1 (periférico Amiga 1:1:
commit de secciones, descripciones de checkpoint, contadores, `0xDEAD`, smoke),
punto 2 (checkpoint profiler con stats seg/scan), punto 4 (`monitor train`).
Siguiente: punto 3 (timeline/rewind: `loop`, `diff` entre frames, frame-step).

## Rutas de alto valor
- Bucle de entrada del engine: `engine/include/eng/engine.hpp` (`update -> wait_vblank -> render`; `render` es el punto de commit).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Validación temporal fuerte por demo: `demos/101_ehb_tile_scroll_driver/analyze-sequence.sh`.
- Detalles operativos build/run: `docs/build/BUILD_AND_RUN.md`.
- Reinstalar el entorno en otro equipo: `docs/debugging/SETUP_NUEVO_EQUIPO.md` (repos, build de WinUAE-DBG, instalación del fork de la extensión, `.mcp.json`).
- Historial de fixes de depuración (relocalización de breakpoints, `-O0`, qOffsets): `docs/debugging/HISTORIAL-CAMBIOS.md`.
- Harness DAP sin VS Code (verifica breakpoints y fuente C++ de la capa DAP): `tools/dap-test/README.md`. Requiere el fork `vscode-amiga-debug` compilado y el stub de `vscode` (ver README). Trazas en `%TEMP%\amiga-debug-trace.log` y `%TEMP%\winuae-gdb.log`.
- Depuración interactiva: usa `tools/debug/build-current-demo.sh` (compila con `-O0` el archivo en primer plano a `out/debug-current/`) y F5 con la config "Amiga 500: depurar archivo actual".
- Captura y análisis de perfiles (frames de pantalla + análisis con Ollama local): `tools/profile/README.md` (`capture-profile.mjs`, `profile-extract.mjs`, `ollama-analyze.mjs`, `profile-analyze.sh`). Requiere WinUAE con `WINUAE_GDB_PERSIST_LISTENER=1`.
- Avanzar por breakpoints y leer memoria/frame buffer en caliente: `tools/debug/step-memory.mjs`.

## Restricciones de código/diseño que hay que preservar
- Restricciones intencionales del engine: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay (`docs/architecture/CODING_STYLE.md`).
- La lógica de juego debe ser agnóstica del backend; registros/DMA específicos de Amiga van en capas backend/driver, no en lógica de alto nivel.
