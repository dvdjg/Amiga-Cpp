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

## Rutas de alto valor
- Bucle de entrada del engine: `engine/include/eng/engine.hpp` (`update -> wait_vblank -> render`; `render` es el punto de commit).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Validación temporal fuerte por demo: `demos/101_ehb_tile_scroll_driver/analyze-sequence.sh`.
- Detalles operativos build/run: `docs/build/BUILD_AND_RUN.md`.

## Restricciones de código/diseño que hay que preservar
- Restricciones intencionales del engine: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay (`docs/architecture/CODING_STYLE.md`).
- La lógica de juego debe ser agnóstica del backend; registros/DMA específicos de Amiga van en capas backend/driver, no en lógica de alto nivel.
