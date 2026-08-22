# Entorno de desarrollo asistido por IA

Este árbol documenta las piezas que permiten trabajar sobre el engine Amiga 500 con una IA que compila, lanza, observa y depura una instancia viva de WinUAE. La fuente operativa sigue estando en los repositorios de código enlazados; este documento es el mapa de navegación y el registro de decisiones del entorno.

## Árbol

```text
ai-dev-environment/
├── README.md                 # mapa, contratos y estado resumido
├── project-map.md            # archivos y proyectos de alto valor
├── ollama-local.md           # Ollama, modelos, scripts y operación segura
└── session-evidence.md       # ciclo de sesión WinUAE y evidencias mínimas
```

## Flujo canónico

```text
editar código
  -> build-demo.sh
  -> run-demo.sh / demo-menu.sh
  -> WinUAE-DBG + GDB :2345
  -> canal lateral :2346
  -> memoria/registros/captura/postmortem
  -> análisis determinista
  -> Vision Review con Ollama (opcional)
  -> evidencia y actualización documental
```

## Contratos que no deben mezclarse

- El depurador GDB de VS Code/Bartman usa el puerto `2345` y es una conexión de un solo cliente.
- El canal lateral de WinUAE-DBG usa `127.0.0.1:2346` y permite observación concurrente sin robar el socket GDB.
- Un ejecutable AmigaDOS se carga por ADF/devfs/loader DOS; no debe tratarse como un payload bare-metal.
- Un payload `metal/direct` requiere una entrada, pila y contexto de registros controlados; para iteración gráfica es hoy la ruta más directa.
- La IA debe usar `observe` para leer, `assist` para entrada/perfilado y `takeover` solo para escrituras reversibles o pausa controlada.

## Estado comprobado en esta máquina

- Ollama `0.32.14` responde en `http://127.0.0.1:11434`.
- Modelo multimodal predeterminado: `qwen3-vl:8b-instruct-q8_0` (visión, 8.8B, Q8_0); la API muestra el modelo cargado en VRAM.
- `gemma3:12b` está instalado como alternativa para capturas estáticas; no será el modelo predeterminado para secuencias.
- El endpoint `http://127.0.0.1:11434/v1/chat/completions` responde y es compatible con `tools/vision-review/vision-review.ts`.
- `mcp-winuae-emu` ya expone conexión, carga Hunk, ejecución, breakpoints, stepping, memoria, registros, custom registers, Copper, capturas, perfiles, entrada, snapshots y postmortems.
- `Amiga-Cpp` ya tiene el runner `build -> run -> analyze`, FrameScope, aserciones de píxel y canal lateral con `poke`, `rollback`, auditoría, `pause` y `resume`.
- La extensión Bartman está recomendada en `legacy/.vscode/extensions.json` y sus perfiles A500 están en `legacy/.vscode/launch.json` y `settings.json`.

## Binarios activos

`WinUAE-DBG\build.bat` compila `bin\winuae-gdb.exe` y el target `CopyToAmigaDebug` ejecuta `deploy-winuae-to-amiga-debug.ps1`, que copia el binario construido a todas las instalaciones Bartman encontradas bajo `.cursor\extensions` y `.vscode\extensions`. En esta máquina la copia activa verificada es `C:\Users\dvdjg\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32\winuae-gdb.exe`; no se encontró una instalación Cursor equivalente. El hash de la copia activa es `191B301C38E41B47DDABE640DD8B9315D0068EC11C86C832525E727EC5BE8378`, distinto del binario antiguo que aún queda en `WinUAE-DBG\bin`; recompilar y ejecutar el target de despliegue es la forma correcta de sincronizarlos. La extensión y el ejecutable deben actualizarse como pareja cuando cambie la relocalización o el protocolo.

## Recursos A500

`C:\Amiga\KICK13.rom` es la ROM objetivo A500/Kickstart 1.3, mide 262144 bytes y tiene SHA-256 `EE05862D8102A0846AC4056DA7D549DB31625C7D47B24DFB7B3C9A5C113CA53`. La carpeta también contiene ADFs de juegos y capturas útiles como referencia visual, pero no se copiarán ROMs ni material de terceros al repositorio.

## Lectura inicial para una nueva IA

1. `AGENTS.md`
2. `docs/README.md`
3. `docs/CONTINUATION_CONTEXT.md`
4. `docs/build/BUILD_AND_RUN.md`
5. `docs/emulation/mcp-live-coding-workflow.md`
6. `docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md`
7. `docs/ai-dev-environment/project-map.md`
8. `docs/ai-dev-environment/ollama-local.md`
9. `docs/ai-dev-environment/session-evidence.md`

## Fuentes hermanas

- `../mcp-winuae-emu`: servidor MCP y cliente GDB RSP.
- `../WinUAE-DBG`: fork de WinUAE con stub GDB y comandos monitor.
- `../mcp-debug-tools`: diseños de MCP/DAP y herramientas de integración VS Code.
- `../Universal-Asset-Format`: perfiles OCS, documentación de assets y precedentes de evidencia reproducible.
- `../ACE`: técnicas y documentación de programación Amiga reusable como referencia.
- `D:/scripts`: patrón real de operación Ollama, reanudación, watchdog y panel.
