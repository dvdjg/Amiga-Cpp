# Órdenes de consola para WinUAE

Este documento ofrece una interfaz que puede usar tanto una persona como una IA sin pasar por MCP. El depurador integrado de VS Code conserva el socket GDB `127.0.0.1:2345`; las órdenes de observación y control cooperativo usan el canal lateral `127.0.0.1:2346`.

## Depurar el archivo actual

Con un archivo dentro de `demos/<nombre>/src` o `tests/<nombre>/src` activo en el editor:

```text
F5 -> Amiga 500: depurar archivo actual
```

La tarea compila el target padre, copia `.exe`, `.elf`, `.map` y `.s` a `out/debug-current/` y escribe `out/debug-current/session.json`. También crea `out/debug-current/current` sin extensión porque Bartman usa ese nombre base para localizar `current.elf`. La configuración Bartman lanza `out/debug-current/current` con `C:\Amiga\KICK13.rom`. El selector vuelve a aparecer en `Run and Debug` al abrir la carpeta raíz `Amiga-Cpp`.

También se puede ejecutar desde Git Bash:

```bash
./tools/debug/build-current-demo.sh "C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/demos/050_blitter_bobs/src/main.cpp"
./tools/debug/amiga-session.sh info
```

## Consultar la sesión

```bash
./tools/debug/amiga-session.sh state
./tools/debug/amiga-session.sh regs
./tools/debug/amiga-session.sh mem 0xdff000 32
```

`info` devuelve el target que se compiló, el archivo fuente, los artefactos y los puertos. La IA debe leer `out/debug-current/session.json` antes de responder preguntas sobre una sesión lanzada desde VS Code.

## Locks y cambios en caliente

```bash
./tools/debug/amiga-session.sh lock acquire usuario observe
./tools/debug/amiga-session.sh lock release usuario
./tools/debug/amiga-session.sh lock acquire usuario takeover
./tools/debug/amiga-session.sh pause
./tools/debug/amiga-session.sh mem 0xc0dde8 16
./tools/debug/amiga-session.sh resume
./tools/debug/amiga-session.sh lock release usuario
```

Usa `observe` para lectura, `assist` para entrada/perfilado y `takeover` para pausa o escritura. Las órdenes `poke`, `rollback` y `audit` están disponibles directamente con `winuae-side-channel.sh` y siempre deben dejar auditoría.

## Capturas, perfiles y entrada

```bash
./tools/debug/amiga-session.sh screenshot "C:/temp/amiga-shot.png"
./tools/debug/amiga-session.sh profile 1 "C:/temp/amiga-profile.bin"
./tools/debug/amiga-session.sh profile-status
./tools/debug/winuae-side-channel.sh lock acquire usuario assist
./tools/debug/winuae-side-channel.sh input mouse abs 160 128
./tools/debug/winuae-side-channel.sh lock release usuario
```

Después de capturar una secuencia, usa FrameScope y Vision Review con Ollama; el VLM es una segunda opinión y no sustituye las aserciones deterministas.

## Órdenes monitor compatibles

El stub de WinUAE-DBG recibe órdenes monitor por `qRcmd`, que `mcp-winuae-emu` expone como herramientas y el depurador puede mostrar en su consola: `screenshot <ruta>`, `disasm <dirección> [cantidad]`, `profile <frames> <unwind> <salida>`, `memcfg`, `input key <scancode> <0|1>`, `input event <id> [estado]`, `df0 insert <ruta>`, `df0 eject` y `reset`.

## Carga directa desde consola

Para depuración automática completa se recomienda VS Code/F5 o `run-demo.sh`. Para una sesión ya lanzada, `mcp-winuae-emu` ofrece `winuae_connect_existing`, `winuae_load`, `winuae_run_program`, `winuae_pause`, `winuae_continue`, `winuae_step`, `winuae_memory_read`, `winuae_registers_get`, `winuae_screenshot` y `winuae_postmortem_capture`. La misma sesión puede inspeccionarse desde consola o desde la IA, respetando que GDB admite un único cliente.

## Limpieza

```bash
./tools/debug/amiga-session.sh lock status
```

Al terminar una sesión F5, usa el botón Stop de VS Code. No mates globalmente `winuae-gdb.exe` si otra sesión del usuario puede estar activa; la IA debe usar primero `state`, liberar su lock y dejar constancia de cualquier operación.
