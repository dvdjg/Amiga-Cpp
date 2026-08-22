# Emulación (WinUAE, MCP, canal lateral)

Todo lo relacionado con el emulador WinUAE, la extensión `amiga-debug`, el MCP `winuae-emu`,
el canal lateral del runner y la automatización del emulador (ratón, capturas, hot-reload,
perfiles). Es el soporte del flujo `build -> run -> analyze`.

> **Procedencia:** la mayoría de estos documentos proceden del repo hermano `Cursor-Amiga-C`
> y se incorporan aquí por tema. Los del canal lateral y el ratón son propios de este repo.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [WINUAE_SIDE_CHANNEL_DEBUG.md](WINUAE_SIDE_CHANNEL_DEBUG.md) | **Especificación del canal lateral TCP** (`127.0.0.1:2346`): comandos, modos y política de seguridad. |
| [MOUSE_AUTOMATION.md](MOUSE_AUTOMATION.md) | Automatización del ratón emulado sin capturar el puntero de Windows. |
| [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md) | Protocolo del flujo MCP `winuae-emu`: compilar, desplegar, verificar. |
| [ia-hot-reload-winuae.md](ia-hot-reload-winuae.md) | Bucle IA -> C -> build -> despliegue en caliente sobre WinUAE. |
| [winuae-extension-internals.md](winuae-extension-internals.md) | Cómo la extensión accede a WinUAE: GDB server (puerto 2345) e IPC. |
| [winuae-y-adf.md](winuae-y-adf.md) | Formas de cargar binarios en WinUAE y generación de ADF. |
| [amigaprofile-format.md](amigaprofile-format.md) | Formato `.amigaprofile` del Frame Profiler de la extensión. |
| [evidence-sequence-and-profiling.md](evidence-sequence-and-profiling.md) | Pipeline de evidencia temporal de frames y profiling. |
| [coppenheimer-ui.md](coppenheimer-ui.md) | UI alternativa Coppenheimer (vAmigaWeb) y su uso por la IA. |
| [instalar-mcp-debug-tools-vsix.md](instalar-mcp-debug-tools-vsix.md) | Instalación de MCP Debug Tools desde VSIX. |
| [WINUAE_CONSOLE_COMMANDS.md](WINUAE_CONSOLE_COMMANDS.md) | Flujo F5 sobre el archivo actual y órdenes de consola reutilizables por personas e IA. |

## Enlaces relacionados

- Operativa build/run/analyze: [../build/BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md).
- Depuración con gdbserver: [../debugging/](../debugging/README.md).
