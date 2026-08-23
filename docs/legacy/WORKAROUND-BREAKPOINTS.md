# Solución de Breakpoints para Depuración C en Amiga

## Estado actual: validado con GDB y WinUAE-DBG

**Fecha:** 2026-02-22

La corrección de relocalización de WinUAE-DBG y la extensión Bartman personalizada están compiladas y desplegadas. Una prueba automática con GDB confirmó una parada real en `main.cpp:406`, la primera instrucción ejecutable después de establecer el breakpoint en la línea 404.

**Causa raíz:** `baseText` siempre es 0. El comando `qOffsets` no devuelve la dirección de carga del programa.

**¿Por qué?** WinUAE-DBG busca el proceso por nombre en la lista de Exec de AROS, pero no lo encuentra o la `segList` está vacía.

---

## Problema Original

Cuando AROS carga dinámicamente el ejecutable Amiga sin el `debugging_trigger` de Bartman, WinUAE-DBG no conoce la dirección real de carga del programa. GDB envía direcciones ELF (ej: `0x4b8` para `main`), pero el programa está cargado en memoria Amiga en direcciones como `0xC00000+`.

## Solución Implementada: Doble Estrategia Automática

Se han implementado **dos mecanismos complementarios** que trabajan juntos:

### Opción A: Relocalización en la Extensión VS Code

**Archivo:** `vscode-amiga-debug/src/amigaDebug.ts`

Cuando `loadOffset` se conoce (vía `qOffsets` de GDB), la extensión:
1. Guarda los breakpoints pendientes al establecerlos
2. Cuando recibe `sections-loaded` con `loadOffset > 0`:
   - Obtiene la dirección ELF de cada file:line usando `info line`
   - Calcula la dirección Amiga: `amigaAddr = elfAddr + loadOffset`
   - Re-establece los breakpoints con direcciones absolutas

**Configuración en launch.json:**
```json
{
  "type": "amiga",
  "breakpointRelocation": true  // activado por defecto
}
```

### Opción B: Auto-Detección en WinUAE-DBG

**Archivo:** `WinUAE-DBG/od-win32/barto_gdbserver.cpp`

Cuando el emulador entra en el debugger y `baseText` aún no está establecido:
1. Compara el PC actual con los breakpoints ELF pendientes
2. Si los bits bajos coinciden: `(PC & 0xFFF) == (elfAddr & 0xFFF)`
3. Calcula `baseText` automáticamente y relocaliza los breakpoints

**Log de auto-detección:**
```
AUTODETECT: PC=0xc004b8 matches ELF BP 0x4b8! Setting baseText=0xc00400
```

## Comandos Monitor Disponibles

Ejecutar desde la consola de depuración de GDB o vía MCP:

```gdb
# Ver estado de breakpoints
monitor breakpoints

# Establecer baseText manualmente (si la auto-detección falla)
monitor offset set 0xc00000

# Ver/configurar logging
monitor logfile            # Estado actual
monitor logfile c:\temp\debug.log  # Activar logging a archivo
monitor logfile off        # Desactivar
```

## Archivo de Log

WinUAE-DBG automáticamente crea un log en:
```
%TEMP%\winuae-gdb.log
```

El log incluye:
- Identificación de versión al iniciar
- Detalle de breakpoints recibidos y relocalizados
- Auto-detección de baseText
- Comprobaciones de breakpoints (cada 10000 ciclos)

## Flujo de Depuración

```
1. Usuario establece breakpoint en main.c:10
2. VS Code envía breakpoint a GDB (file:line)
3. GDB envía a WinUAE-DBG dirección ELF (ej: 0x4b8)
4. WinUAE-DBG guarda como pendiente si baseText=0

5a. [Opción A] loadOffset llega vía qOffsets:
    - Extensión re-establece breakpoints con direcciones absolutas

5b. [Opción B] loadOffset no disponible, programa ejecuta:
    - PC entra en código del usuario
    - WinUAE-DBG detecta coincidencia con BP pendiente
    - Calcula baseText y relocaliza breakpoints

6. Breakpoint funciona, debugger se detiene en código C
```

## Solución de Problemas

### Breakpoint no se detiene

1. Verificar log en `%TEMP%\winuae-gdb.log`
2. Ejecutar `monitor breakpoints` para ver estado
3. Si baseText=0, usar `monitor offset set <dirección>`

### Encontrar la dirección de carga

1. Ver PC cuando el programa crashea o se detiene
2. Restar la dirección ELF de `main` (del .map file)
3. Establecer con `monitor offset set <resultado>`

### Ejemplo práctico

```bash
# Ver dirección ELF de main
grep " main$" out/a.map
# Resultado: 0x4b8

# Si PC en WinUAE es 0xc004b8:
# baseText = 0xc004b8 - 0x4b8 + 0x400 = 0xc00400

# En GDB:
monitor offset set 0xc00400
```

## Cambios en WinUAE-DBG v2.0

1. **Auto-detección de baseText** cuando PC coincide con breakpoints pendientes
2. **Logging comprehensivo** a archivo para diagnóstico
3. **Comandos monitor nuevos**: `breakpoints`, `offset set`, `logfile`
4. **Identificación de versión** al iniciar el servidor GDB

## Cambios en vscode-amiga-debug

1. **Relocalización automática** de breakpoints después de `sections-loaded`
2. **Flag configurable** `breakpointRelocation` en launch.json
3. **Logging detallado** de breakpoints pendientes y relocalizados

---

## ⚠️ Estado de Compilación de los Cambios

| Componente | Modificado | Compilado | Funciona |
|------------|------------|-----------|----------|
| WinUAE-DBG (barto_gdbserver.cpp) | ✅ | ✅ | Pendiente de validar |
| vscode-amiga-debug (amigaDebug.ts) | ✅ | ✅ | ❌* |
| mcp-winuae-emu (gdb-protocol.ts) | ✅ | ✅ | ❌* |

La sesión anterior confirmó que `qOffsets` llega antes de que `:current.exe` esté cargado. WinUAE-DBG ahora resuelve `baseText` al detectar la entrada del proceso y no envía `S05` por esa parada interna.

La prueba automática también confirmó que, cuando el primer `qOffsets` devuelve `$0`, GDB envía inicialmente la dirección ELF restando `ELF_TEXT_BASE` (`0x12c2` en lugar de `0x16c2`). WinUAE-DBG ahora registra y corrige esa normalización antes de la relocalización diferida:

```text
Z0: normalized unresolved qOffsets address 0x12c2 -> ELF 0x16c2
RELOC: BP[0] 0x16c2 -> 0xc0e972
T05swbreak
```

## Problema Pendiente de Resolver

El código en `barto_gdbserver.cpp` que maneja `qOffsets`:

```cpp
// En handle_packet, caso 'q':
if (strncmp(packet, "Offsets", 7) == 0) {
    // Busca el proceso por debugging_trigger
    processname = ua(currprefs.debugging_trigger);  // ':runme.exe' o ':'
    
    // Busca en lista de procesos de Exec
    // Si encuentra, obtiene segList y calcula baseText
    // PERO: No encuentra el proceso o segList es NULL
}
```

**Investigación necesaria:**
1. ¿AROS expone los procesos de la misma forma que AmigaOS clásico?
2. ¿El `debugging_trigger` está correctamente configurado?
3. ¿La `segList` se inicializa correctamente en AROS?

Ver `doc/CONTEXTO-DEPURACION-C.md` para más detalles.
