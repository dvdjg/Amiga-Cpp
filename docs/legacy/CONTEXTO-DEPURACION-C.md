# Contexto para Continuar: Depuración C en Amiga no Funciona

## Problema Principal

**Al depurar con F5 o MCP, los breakpoints en código C no funcionan. El debugger se detiene en desensamblado en lugar del código fuente C.**

## Estado Actual

- La calculadora Amiga se compila correctamente
- El programa se ejecuta en WinUAE
- Los breakpoints NO se detienen en código C
- Al pausar, siempre muestra desensamblado (PC en ROM 0xFC0F90)

## Causa Raíz Identificada

El problema es que `baseText` (dirección de carga del programa) siempre es 0:

1. **Con F5 (extensión Bartman)**: m68k-amiga-elf-gdb envía `qOffsets`, pero WinUAE-DBG no encuentra el proceso correcto para calcular baseText
2. **Con MCP**: El MCP no enviaba `qOffsets` (ya corregido pero requiere reiniciar Cursor)

### Flujo de Breakpoints (cómo debería funcionar)

```
1. Usuario pone breakpoint en main.c:10
2. GDB obtiene dirección ELF de esa línea (ej: 0x4b8)
3. GDB envía qOffsets a WinUAE-DBG
4. WinUAE-DBG busca el proceso, obtiene segList, calcula baseText
5. WinUAE-DBG responde con Text=<baseText>
6. GDB suma loadOffset: amigaAddr = elfAddr + (baseText - 0x400)
7. GDB envía breakpoint con dirección Amiga absoluta
8. Breakpoint funciona
```

### Lo que está fallando

En el paso 4, WinUAE-DBG no encuentra el proceso correcto. El log muestra:
```
GDBSERVER: DEBUG state::inited - debugging_trigger=':', processname set to ':runme.exe'
```

Pero cuando llega `qOffsets`, no encuentra el proceso o la `segList` está vacía, resultando en `baseText=0`.

## Archivos Clave Modificados

### WinUAE-DBG/od-win32/barto_gdbserver.cpp

Cambios realizados:
1. **Logging comprehensivo** - Version ID, breakpoint tracing, auto-log a `%TEMP%\winuae-gdb.log`
2. **monitor breakpoints** - Lista breakpoints activos y pendientes
3. **monitor offset set <addr>** - Permite establecer baseText manualmente
4. **monitor logfile** - Control de logging a archivo
5. **monitor findcode <pattern>** - Busca patrón en memoria (NO PROBADO, requiere recompilar)
6. **Auto-detección de baseText** - Intenta calcular baseText cuando PC entra en código de usuario (NO FUNCIONA porque PC siempre está en ROM)

**IMPORTANTE**: Estos cambios NO están compilados. El WinUAE que se ejecuta es el binario original de axewater.

### vscode-amiga-debug/src/amigaDebug.ts

Cambios realizados:
1. **Relocalización de breakpoints** - Guarda breakpoints pendientes y los re-establece con direcciones absolutas cuando llega loadOffset
2. **Flag breakpointRelocation** - Configurable en launch.json

**IMPORTANTE**: La extensión SÍ está compilada (`npm run compile` exitoso).

### mcp-winuae-emu/src/gdb-protocol.ts

Cambios realizados:
1. **Envío de qOffsets** - Añadido al proceso de conexión

**IMPORTANTE**: Compilado pero requiere reiniciar Cursor para que MCP use la versión nueva.

## Direcciones Conocidas

Del archivo `out/a.map`:
- `main` está en ELF 0x4b8
- `intuition_calc_open` está en ELF 0xe52
- `.rodata` empieza en ELF 0x39e2

Patrón de bytes de main: `4fefffe048e730024eb9` (LEA -32(SP),SP; MOVEM.L; JSR)

## Logs de Diagnóstico

El log de WinUAE-DBG está en: `C:\Users\dvdjg\AppData\Local\Temp\winuae-gdb.log`

Ejemplo de log mostrando el problema:
```
Z0: Received breakpoint request: ELF=0x4b8, baseText=0x0
Z0: NOT relocated (loadOffset=0x0, will defer)
```

## Intentos Fallidos

1. **Auto-detección por PC** - No funciona porque el PC está en ROM mientras el programa espera eventos
2. **Búsqueda de patrón en memoria** - No encontré el código del programa en Chip RAM ni Slow RAM
3. **Relocalización diferida** - El breakpoint se guarda como pendiente pero nunca se relocaliza porque baseText nunca se calcula

## Próximos Pasos Sugeridos

### Opción 1: Investigar por qué qOffsets no calcula baseText

En `barto_gdbserver.cpp`, el código de qOffsets busca el proceso por nombre:
```cpp
processname = ua(currprefs.debugging_trigger);  // ':runme.exe'
// Luego busca en la lista de procesos de Exec
// Si encuentra el proceso, obtiene segList y calcula baseText
```

Preguntas a investigar:
- ¿El proceso ':runme.exe' existe en la lista de Exec de AROS?
- ¿La segList del proceso está correctamente inicializada?
- ¿El debugging_trigger está configurado correctamente?

### Opción 2: Usar el workaround manual

Si se compila WinUAE-DBG con los cambios:
1. Ejecutar el programa
2. Buscar el patrón `4fefffe048e7` en memoria con `monitor findcode`
3. Calcular baseText = dirección_encontrada - 0x4b8 + 0x400
4. Establecer con `monitor offset set <baseText>`

### Opción 3: Modificar el programa para reportar su dirección

Añadir código al inicio del programa que escriba su dirección de carga en una ubicación conocida de memoria.

## Configuración Actual

### launch.json (Cursor-Amiga-C/.vscode/)
```json
{
  "type": "amiga",
  "request": "launch",
  "name": "Debug Amiga",
  "program": "${workspaceFolder}/out/a"
}
```

### debugging_trigger
Configurado como `:` en la configuración de WinUAE, lo que significa "detectar cualquier programa".

## Repositorios Involucrados

1. **Cursor-Amiga-C** - Proyecto de calculadora Amiga
2. **WinUAE-DBG** - Fork de WinUAE con servidor GDB (axewater/WinUAE)
3. **mcp-winuae-emu** - MCP para controlar WinUAE
4. **vscode-amiga-debug** - Extensión de VS Code de Bartman

## Comandos Útiles

```bash
# Ver dirección de main en el ELF
grep " main$" out/a.map

# Ver log de WinUAE-DBG
cat %TEMP%\winuae-gdb.log

# Compilar extensión VS Code
cd vscode-amiga-debug && npm run compile

# Compilar MCP
cd mcp-winuae-emu && npm run build
```

## Resumen para la Nueva Conversación

**El debugger no funciona porque `baseText` siempre es 0.** 

baseText se calcula en respuesta a `qOffsets`, pero el código que busca el proceso en la lista de Exec de AROS no lo encuentra o no obtiene una segList válida.

La solución más robusta sería:
1. Entender por qué qOffsets no encuentra el proceso
2. O implementar un mecanismo alternativo para determinar dónde AROS cargó el programa

Todos los cambios de código están en los repositorios pero WinUAE-DBG NO está recompilado.
