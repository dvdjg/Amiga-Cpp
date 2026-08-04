# Contexto para Nueva Conversación: Depuración C en Amiga

## TL;DR del Problema

**Al depurar con F5 en VS Code, los breakpoints en código C no funcionan. El debugger muestra desensamblado en lugar del código fuente.**

Causa: `baseText=0` siempre. WinUAE-DBG no calcula la dirección donde AROS cargó el programa.

## Arquitectura del Sistema

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│ VS Code         │────▶│ m68k-amiga-elf-  │────▶│ WinUAE-DBG      │
│ (F5 debug)      │     │ gdb              │ RSP │ (servidor GDB)  │
│ + extensión     │     │                  │     │ puerto 2345     │
│ vscode-amiga-   │     │ Envia qOffsets   │     │                 │
│ debug           │     │ para obtener     │     │ qOffsets debe   │
└─────────────────┘     │ baseText         │     │ devolver        │
                        └──────────────────┘     │ Text=baseText   │
                                                 └─────────────────┘
```

## El Flujo que Falla

1. Usuario pone breakpoint en `main.c:10`
2. GDB traduce a dirección ELF: `0x4b8`
3. GDB envía `qOffsets` a WinUAE-DBG
4. **WinUAE-DBG busca el proceso en Exec pero NO lo encuentra** → devuelve `baseText=0`
5. GDB calcula `loadOffset = baseText - 0x400 = 0`
6. Breakpoint se pone en dirección ELF `0x4b8` (incorrecta)
7. El programa está cargado en `~0xC44000` → breakpoint nunca se activa

## Archivos Clave

| Archivo | Propósito | Estado |
|---------|-----------|--------|
| `WinUAE-DBG/od-win32/barto_gdbserver.cpp` | Servidor GDB en WinUAE | Modificado, NO compilado |
| `vscode-amiga-debug/src/amigaDebug.ts` | Extensión VS Code | Modificado, compilado |
| `mcp-winuae-emu/src/gdb-protocol.ts` | Cliente GDB del MCP | Modificado, compilado |
| `Cursor-Amiga-C/out/a.map` | Mapa de símbolos del programa | Referencia |

## Cambios Realizados (no funcionales aún)

### En barto_gdbserver.cpp:
- Logging comprehensivo a `%TEMP%\winuae-gdb.log`
- `monitor breakpoints` - lista breakpoints
- `monitor offset set <addr>` - establecer baseText manual
- `monitor findcode <pattern>` - buscar código en memoria
- Auto-detección de baseText cuando PC entra en código usuario

### En amigaDebug.ts:
- Relocalización de breakpoints cuando llega loadOffset
- Flag `breakpointRelocation` configurable

### En gdb-protocol.ts:
- Envío de `qOffsets` al conectar

## Información de Debug

Dirección de `main` en ELF: `0x4b8` (ver `out/a.map`)
Patrón de bytes de main: `4fefffe048e730024eb9`

Log de WinUAE-DBG: `C:\Users\dvdjg\AppData\Local\Temp\winuae-gdb.log`

## Lo que Necesita Investigación

1. **¿Por qué qOffsets no encuentra el proceso?**
   - El código busca por `debugging_trigger` (`:runme.exe`)
   - Puede ser que AROS no expone el proceso de la misma forma que AmigaOS, pero ninguno de los dos funciona.

2. **¿Cómo saber dónde AROS/AmigaOS carga el programa?**
   - La memoria del programa está en algún lugar de Chip/Fast RAM
   - Necesitamos encontrar ese lugar para calcular baseText

## Próximo Paso Sugerido

Compilar WinUAE-DBG con los cambios para tener los logs y usar `monitor findcode 4fefffe048e7` para buscar el programa en memoria, luego establecer baseText manualmente.

## Documentación Detallada

Ver `doc/CONTEXTO-DEPURACION-C.md` para todos los detalles técnicos.
