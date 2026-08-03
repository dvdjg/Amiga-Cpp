# Bug: Breakpoints desde C no funcionan

## Estado: SIN RESOLVER (actualizado 2026-02-22)

**Última sesión de depuración extensa realizada. Ver:**
- `doc/CONTEXTO-DEPURACION-C.md` - Resumen completo
- `doc/NUEVO-CHAT-CONTEXTO.md` - Archivo para nueva conversación

## Descripción del problema

Los breakpoints establecidos desde código fuente C no se activan cuando el programa se ejecuta. El depurador se detiene en desensamblado en lugar de en código C.

## Análisis técnico

### Causa raíz identificada

GDB envía direcciones ELF (sin relocalizar) a WinUAE:
- Breakpoint en `main.c:15` → GDB traduce a `0x000004c0` (dirección ELF)
- GDB envía `Z0,4c0,1` a WinUAE
- Pero el código está cargado en `0x00c45010` (dirección Amiga)
- El breakpoint nunca se activa porque las direcciones no coinciden

### Valores observados

```
baseText (qOffsets) = 0x00c44f50
ELF_TEXT_BASE = 0x400
loadOffset = baseText - ELF_TEXT_BASE = 0x00c44b50

main() en ELF: 0x4c0
main() en Amiga: 0x4c0 + 0x00c44b50 = 0x00c45010
```

### Intentos de solución

1. **Relocalización diferida en WinUAE-DBG** (`barto_gdbserver.cpp`):
   - Almacenar direcciones ELF en `breakpoint_elf_addresses`
   - Relocalizar cuando `qOffsets` proporciona `baseText`
   - **Resultado**: No funciona - el programa se detiene constantemente después de cada `continue`

2. **Modificación de la extensión vscode-amiga-debug**:
   - `getSourceInfoFromAddress()` usando `addr2line`
   - `relocateWithOffset()` en symbolTable
   - **Resultado**: Algunos frames del stack muestran código fuente, pero el frame actual sigue en desensamblado

### Comportamiento observado

- Con WinUAE original: El programa corre pero los breakpoints no se activan
- Con WinUAE modificado: El programa se detiene después de casi cada instrucción
- addr2line funciona correctamente cuando se prueba manualmente

### Archivos modificados

#### WinUAE-DBG/od-win32/barto_gdbserver.cpp
- `ELF_TEXT_BASE = 0x400`
- `breakpoint_elf_addresses` vector
- `relocate_breakpoints()` función
- Handler Z0 con relocalización condicional
- Handler z0 con relocalización para eliminar breakpoints

#### vscode-amiga-debug/src/amigaDebug.ts
- `loadOffset` propiedad
- `addr2linePath` propiedad
- `getSourceInfoFromAddress()` método
- Modificación de `stackTraceRequest`

#### vscode-amiga-debug/src/backend/mi2.ts
- Obtención de loadOffset via `maintenance packet qOffsets`
- Emisión de `sections-loaded` con loadOffset

#### vscode-amiga-debug/src/backend/symbols.ts
- `relocateWithOffset(loadOffset)` método

## Próximos pasos sugeridos

1. **Investigar por qué el programa se detiene constantemente** con WinUAE modificado
   - Puede ser un `trace_mode` que no se resetea
   - Puede ser un problema con la detección de breakpoints

2. **Considerar enfoque alternativo**: Modificar la extensión para enviar direcciones relocadas directamente
   - Interceptar `addBreakpoint` en `mi2.ts`
   - Calcular dirección relocada antes de enviar a GDB

3. **Verificar el fork correcto**: Estamos usando https://github.com/axewater/WinUAE (no BartmanAbyss)

## Estado del MCP winuae-emu (2026-02-22)

El MCP funciona correctamente y puede usarse para depuración autónoma:

### Tests verificados ✓
- `winuae_connect` - Conexión al servidor GDB
- `winuae_status` - Estado de conexión
- `winuae_screenshot` - Capturas de pantalla
- `winuae_registers_get/set` - Lectura/escritura de registros
- `winuae_memory_read/write/dump` - Operaciones de memoria
- `winuae_breakpoint_set/clear` - Breakpoints
- `winuae_watchpoint_set/clear` - Watchpoints
- `winuae_continue/pause` - Control de ejecución
- `winuae_disassemble_full` - Desensamblado
- `winuae_copper_disassemble` - Desensamblado de Copper
- `winuae_profile` - Profiling de frames
- `winuae_input_*` - Input de teclado/joystick/ratón
- `winuae_insert_disk` - Inserción de discos

### Tests con problemas
- `winuae_custom_registers` - Error E01 al leer $DFF000
- `winuae_step` - Timeout en vCont;s

### Lo que SÍ funciona
La depuración a nivel de ensamblador mediante el MCP es totalmente funcional:
- Cargar juegos/programas
- Pausar/continuar ejecución
- Leer registros y memoria
- Establecer breakpoints en direcciones absolutas (Amiga)
- Obtener información de profiling
- Tomar screenshots

### Lo que NO funciona
La depuración desde código fuente C debido al problema de relocalización de direcciones
descrito arriba.

## Referencias

- WinUAE fork: https://github.com/axewater/WinUAE
- Documentación en `WinUAE-DBG/docs/`

---

## Resumen de Sesión de Debug Extensa (2026-02-22)

### Conclusiones Principales

1. **El problema está en `qOffsets`**: WinUAE-DBG no calcula `baseText` correctamente
2. **El proceso no se encuentra**: El código busca el proceso por nombre en la lista de Exec de AROS pero no lo encuentra
3. **La auto-detección no funciona**: Se implementó auto-detección por PC pero el PC siempre está en ROM (0xFC0F90) mientras el programa espera eventos

### Modificaciones Realizadas (código, no compiladas en WinUAE-DBG)

| Archivo | Cambio | Compilado |
|---------|--------|-----------|
| `barto_gdbserver.cpp` | Logging, monitor commands, auto-detect | ❌ |
| `amigaDebug.ts` | Relocalización de breakpoints | ✅ |
| `gdb-protocol.ts` | Envío de qOffsets | ✅ |

### Log de WinUAE-DBG

Ubicación: `%TEMP%\winuae-gdb.log`

Ejemplo de log mostrando el problema:
```
Z0: Received breakpoint request: ELF=0x4b8, baseText=0x0
Z0: NOT relocated (loadOffset=0x0, will defer)
```

### Información del Programa

- `main` está en ELF `0x4b8`
- Patrón de bytes: `4fefffe048e730024eb9`
- El programa probablemente está cargado en `~0xC44000`

### Próximo Paso

Compilar WinUAE-DBG con los cambios y usar:
1. `monitor findcode 4fefffe048e7` para encontrar el programa
2. `monitor offset set <addr>` para establecer baseText manualmente
