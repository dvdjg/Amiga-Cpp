# AGENTS

## Regla de idioma (obligatoria)
- Toda documentación y texto en español debe escribirse con ortografía correcta: tildes, eñes y puntuación adecuadas.
- No introducir nuevas frases en español sin corregir (`ejecución`, `análisis`, `depuración`, `diseño`, etc.).

## Qué es este repositorio
- El repo mantiene un proyecto C Amiga legado en `legacy/` (`legacy/Makefile`, `legacy/out/a.exe`) y un flujo nuevo de demos del engine C++23 en `demos/` + `tools/`; no mezclarlos por error.
- Para trabajo del engine, usar scripts PowerShell de `tools/` en vez de invocar el `legacy/Makefile`.

## Herramientas locales requeridas
- Windows + PowerShell + Node.js son obligatorios para el flujo de ejecución automatizada (`tools/run/run-demo.ps1` -> `tools/run/run-demo.mjs`).
- El toolchain Amiga se resuelve en este orden: `AMIGA_BIN_PATH`, extensión de Cursor y luego extensión de VS Code `bartmanabyss.amiga-debug-1.8.2`.
- `tools/run/run-demo.mjs` importa `../../../mcp-winuae-emu/dist/winuae-connection.js` (salida de repo hermano); si falta, el runner falla antes de abrir WinUAE.

## Comandos canónicos
- Compilar una demo: `powershell -ExecutionPolicy Bypass -File .\tools\build\build-demo.ps1 demos\000_toolchain_cpp23 -DebugBuild -Clean`
- Ejecutar una demo y capturar: `powershell -ExecutionPolicy Bypass -File .\tools\run\run-demo.ps1 demos\000_toolchain_cpp23`
- Analizar una demo: `powershell -ExecutionPolicy Bypass -File .\tools\analyze\analyze-demo.ps1 demos\000_toolchain_cpp23`
- Regresión completa: `powershell -ExecutionPolicy Bypass -File .\tools\test-regression.ps1`
- Bucle de regresión de una demo: `powershell -ExecutionPolicy Bypass -File .\tools\test-regression.ps1 -Demo demos\101_ehb_tile_scroll_driver -Warp`

## Orden de verificación (no saltar)
- Orden por defecto: `build -> run -> analyze`; `analyze-demo.ps1` espera `.exe/.elf/.map` y valida `out/run/<demo>/screenshot.png` si existe.
- `tools/test-regression.ps1` ya impone ese orden por demo y ejecuta `analyze-sequence.ps1` automáticamente cuando existe.
- La regresión usa build estilo debug por defecto (`-DebugBuild` interno). Usar `-ReleaseBuild` solo cuando se necesite comportamiento de optimización release.

## Comportamiento clave de runner/emulador
- `run-demo` configura WinUAE con `warp=false` por defecto. Usar `-Warp` solo para throughput/diagnóstico rápido, no para evaluar suavidad visual.
- Las demos modernas deben exponer `g_amg_run_status`; el runner espera `READY` por canal lateral en `127.0.0.1:2346` y falla rápido si no llega.
- El fallback por timeout es opt-in (`-AllowTimeoutFallback` / `--allow-timeout-fallback`) y es para diagnóstico.
- Las capturas de secuencia se limpian antes de cada ejecución (`out/run/<demo>/sequence`) para no mezclar frames antiguos.

## Rutas de alto valor
- Bucle de entrada del engine: `engine/include/amg/engine.hpp` (`update -> wait_vblank -> render`; `render` es el punto de commit).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Validación temporal fuerte por demo: `demos/101_ehb_tile_scroll_driver/analyze-sequence.ps1`.
- Detalles operativos build/run: `docs/build/BUILD_AND_RUN.md`.

## Restricciones de código/diseño que hay que preservar
- Restricciones intencionales del engine: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay (`docs/architecture/CODING_STYLE.md`).
- La lógica de juego debe ser agnóstica del backend; registros/DMA específicos de Amiga van en capas backend/driver, no en lógica de alto nivel.
