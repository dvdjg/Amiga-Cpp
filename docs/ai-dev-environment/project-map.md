# Mapa de proyectos y archivos útiles

## `Amiga-Cpp` (fuente de trabajo)

| Ruta | Utilidad |
|---|---|
| `tools/build/build-demo.sh` | Compila una demo C++23 para el target Amiga; busca primero `AMIGA_BIN_PATH` y después las extensiones Bartman. |
| `tools/run/run-demo.sh` | Lanza WinUAE, espera `READY`, captura y genera `run-report.json`. |
| `tools/run/demo-menu.sh` | Operación humana: ejecutar, analizar, secuencias o dejar WinUAE abierto. |
| `tools/debug/winuae-side-channel.ts` | CLI TCP para estado, memoria, registros, captura, perfil, input y hot patch. |
| `tools/debug/build-current-demo.sh` | Deriva la demo del archivo activo, compila y publica `out/debug-current/`. |
| `tools/debug/amiga-session.sh` | Consulta la sesión actual y delega órdenes laterales para usuario o IA. |
| `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md` | Protocolo, locks, niveles de riesgo y rollback. |
| `tools/debug/verify-side-channel-*.ts` | Contratos de convivencia GDB/canal lateral y takeover reversible. |
| `tools/framescope/` | Análisis temporal de secuencias y hojas de contacto. |
| `tools/vision-review/` | Selección de frames, prompts, empaquetado y proveedor visual. |
| `tools/test-regression.sh` | Regresión `build -> run -> analyze` por demos. |
| `engine/` y `demos/` | Engine activo y demostraciones; no mezclar con `legacy/`. |
| `legacy/.vscode/` | Configuración Bartman A500 del proyecto C histórico. |
| `out/` | Capturas, perfiles, informes y evidencias generadas. |

## `mcp-winuae-emu`

Servidor MCP recomendado para el control profundo. Su `README.md` es la referencia operativa de variables y herramientas. Las capacidades de mayor valor son:

- `winuae_connect` / `winuae_connect_existing` / `winuae_status`;
- `winuae_load` / `winuae_run_program` / `winuae_exec_chunk`;
- `winuae_pause` / `winuae_continue` / `winuae_step`;
- `winuae_breakpoint_set` / `winuae_watchpoint_set`;
- `winuae_memory_read` / `winuae_memory_write` / `winuae_registers_get`;
- `winuae_machine_snapshot` / `winuae_postmortem_capture`;
- `winuae_custom_registers` / `winuae_copper_disassemble` / `winuae_bitmap_decode`;
- `winuae_screenshot` / `winuae_profile` / `winuae_input_*`.

Limitaciones importantes: GDB acepta un cliente, el acceso CIA no está expuesto, la escritura depende de que el stub soporte `M` o `X`, y la carga directa Hunk sigue siendo más frágil que ADF/devfs. Para una sesión visible persistente es preferible lanzar WinUAE externamente y usar `winuae_connect_existing`.

## `WinUAE-DBG`

- `od-win32/barto_gdbserver.cpp`: servidor GDB/RSP y comandos monitor.
- `doc/BARTMAN-VSCODE-Y-EVOLUCION.md`: atribución, compatibilidad Bartman/fork/MCP y reglas para no romper `qOffsets`, `Z0/z0` ni `qRcmd`.
- `build.bat`: build del ejecutable `bin/winuae-gdb.exe` con Visual Studio/NASM.
- `od-win32/winuae_msvc15/deploy-winuae-to-amiga-debug.ps1`: despliegue automático a las extensiones instaladas.
- `doc/RELOCATION-FIX.md` y `doc/DEBUGGING-ARCHITECTURE.md`: relocalización y cadena DAP -> MI -> RSP.

## `mcp-debug-tools`

Es útil como referencia de integración VS Code/MCP, DAP y orquestación de varias operaciones. No sustituye al servidor `mcp-winuae-emu` ni al canal lateral existente. Antes de implementar un nuevo adaptador, comparar con `docs/plan/dap/` y `docs/plan/unified-tool-design.md`.

## `Universal-Asset-Format` y `ACE`

- `Universal-Asset-Format/doc/Contexto OCS para IA.md`, `Referencia_Tecnicas_Amiga500.md`, `Modos_Graficos_y_Zonas_OCS.md`: conocimiento de hardware y perfiles de assets.
- `Universal-Asset-Format/AGENTS.md`: reglas de documentación y alineación con A500.
- `ACE/docs/programming/`: técnicas de sprites, blitter, tilebuffer, audio y view.

Son fuentes de conocimiento y formatos, no dependencias runtime del engine.

## Orden recomendado de operación

1. Compilar con `tools/build/build-demo.sh`.
2. Ejecutar con `tools/run/run-demo.sh` sin `--warp` para validar timing visual.
3. Observar por canal lateral o MCP sin abrir una segunda conexión GDB.
4. Capturar pantalla, registros, custom registers y snapshot/postmortem.
5. Ejecutar FrameScope/pixel assertions antes de llamar al VLM.
6. Enviar solo los frames relevantes a Ollama y conservar JSON bruto y Markdown.

## Automatización ya avanzada

`run-demo.ts` no solo abre WinUAE: localiza la extensión Bartman, prepara `dh0`/`dh1`, escribe `debugging_trigger=:a.exe`, desactiva la captura del puntero del host, fuerza `win32.absolute_mouse=yes`, mantiene `warp=false` salvo petición explícita, espera `g_eng_run_status=READY`, limpia secuencias antiguas y guarda telemetría por frame. Las pruebas laterales lanzan el runner como proceso hijo, esperan el handshake y verifican que GDB y el canal TCP siguen siendo utilizables a la vez.

`mcp-winuae-emu` añade autodetección de sesiones existentes, carga de AmigaHunk con relocalizaciones `RELOC32`, inserción/eyección de ADF, captura interna o de ventana, perfiles compatibles con Bartman y postmortems con registros, stack y desensamblado. La carga directa sigue siendo diagnóstica; para ejecutables DOS la ruta ADF/devfs conserva mejor el contexto de Kickstart 1.3.

`D:/scripts/16_clasificar_videos.py` ya implementa un flujo reanudable por vídeo: deduplicación, extracción de frames con FFmpeg, llamada VLM con tres reintentos y backoff, saneado de campos booleanos, escritura SQLite por worker, renombrado atómico, metadata validada con FFprobe, JSON lateral y movimiento por tema. Este patrón es reutilizable para evidencias Amiga, pero no debe compartir su base de datos ni sus tareas programadas con el engine.
