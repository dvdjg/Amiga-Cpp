# Sistema de Depuración Amiga: Resumen Completo

## Arquitectura General

```
┌──────────────────────────────────────────────────────────────────────┐
│                      VS Code / Cursor IDE                            │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  Extensión amiga-debug (bartmanabyss.amiga-debug)            │   │
│  │  amigaDebug.ts  ─── DAP ──►  VS Code UI                     │   │
│  │  mi2.ts         ─── MI  ──►  m68k-amiga-elf-gdb.exe         │   │
│  │  symbols.ts     ◄── objdump ──  ELF                          │   │
│  └───────────────────────────┬──────────────────────────────────┘   │
│                              │                                       │
│  ┌───────────────────────────┴──────────────────────────────────┐   │
│  │  mcp-winuae-emu (MCP Server - para depuración por IA)       │   │
│  │  gdb-protocol.ts ── RSP ──►  WinUAE-DBG :2345              │   │
│  └──────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────┬───────────────────────────────────┘
                                   │ TCP :2345 (GDB RSP)
                                   ▼
┌──────────────────────────────────────────────────────────────────────┐
│  WinUAE-DBG (emulador Amiga fork)                                   │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  barto_gdbserver.cpp  (servidor GDB RSP)                     │   │
│  │  • init(): setup, listen, processname=debugging_trigger      │   │
│  │  • debug(): main loop, process entry detection, S05 send     │   │
│  │  • handle_packet(): qOffsets, Z0/z0, vCont, g/G, m/M        │   │
│  │  • refresh_process_offsets(): baseText desde segList         │   │
│  │  • relocate_breakpoints(): ELF addr → Amiga addr             │   │
│  └────────────────────────────┬─────────────────────────────────┘   │
│                               ▼                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  debug.cpp  (depurador interno del emulador)                 │   │
│  │  • trace_mode state machine (SKIP_INS, RANGE_PC, CHECKONLY) │   │
│  │  • Process detection por PC range + nombre                   │   │
│  │  • gdb_notify_process_entry flag                             │   │
│  │  • Breakpoint loop (bpnodes[])                              │   │
│  └──────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

## Modos de Depuración

### 1. F5 (Humano con GDB)

#### Lado Extensión VS Code
1. Lee `launch.json` → programa ELF a depurar
2. Crea SymbolTable con `objdump`
3. Lanza WinUAE-DBG con `debugging_trigger=:programa.exe`
4. Lanza `m68k-amiga-elf-gdb.exe` con el ELF
5. GDB conecta a WinUAE-DBG (target remote :2345)
6. Handshake: qSupported, qOffsets, etc.
7. Establece breakpoints (Z0) con direcciones ELF
8. `refreshRelocation()`: envía `maintenance packet qOffsets` → recibe baseText
9. Relocaliza SymbolTable: `relocateWithOffset(loadOffset)`
10. Envía continue (vCont;c)

#### Lado WinUAE-DBG
1. `init()`: `processname = ua(debugging_trigger)`, activa debugger
2. `barto_gdbserver::debug()` → state::inited → espera conexión → system BPs → state::debugging
3. GDB conecta, handshake: procesa qOffsets (busca proceso en listas Exec)
4. Breakpoints Z0: almacena dirección, relocaliza si baseText disponible
5. vCont;c: `deactivate_debugger_preserve_processname()`, trace_mode=TRACE_CHECKONLY
6. CPU ejecuta hasta breakpoint o detección de proceso
7. Proceso detectado en debug.cpp → `gdb_notify_process_entry` → `refresh_process_offsets()` → baseText calculado
8. S05 enviado a GDB, GDB detiene, usuario ve código fuente
9. Ciclo: GDB consulta estado → usuario avanza → GDB continua

### 2. MCP (IA con mcp-winuae-emu)

1. Cursor lanza WinUAE-DBG (sin debugging_trigger, o con trigger)
2. IA usa herramientas MCP: `connect`, `read-registers`, `read-memory`, etc.
3. `gdb-protocol.ts` maneja RSP directamente (sin GDB intermedio)
4. Flujo similar pero sin SymbolTable (la IA usa conocimiento propio)
5. Commands: step, continue, set breakpoints (`Z0`), read/write memory

## Estados del Depurador

```cpp
enum class state {
    inited,     // Servidor listo, esperando conexión GDB
    connected,  // Conectado, CPU ejecutando breakpoints activos
    debugging,  // Detenido en un breakpoint, procesando comandos GDB
    profile,    // Recolectando perfil de ejecución
};
```

Transiciones:
- `inited` → `debugging`: cuando GDB conecta (en `barto_gdbserver::debug()`)
- `debugging` → `connected`: por gdb_notify_process_entry o vCont;c
- `connected` → `debugging`: cuando un breakpoint se activa
- `debugging` → while loop: procesa paquetes GDB hasta que se envía 'c' o 's'

## Variables Globales Clave

### En debug.cpp
| Variable | Tipo | Propósito |
|----------|------|-----------|
| `processname` | `uae_char*` | Nombre del proceso objetivo (ej: ":a.exe") |
| `processptr` | `uaecptr` | Dirección del proceso objetivo (alternativa) |
| `trace_mode` | `int` | Estado de tracing (0=inactivo, 10=CHECKONLY) |
| `debugging` | `int` | Flag de depuración activa (0=no, 1/ -1=sí) |
| `gdb_notify_process_entry` | `bool` | Señal para barto_gdbserver: proceso detectado |
| `gdb_process_range_entered` | `bool` | Previene notificaciones repetidas |

### En barto_gdbserver.cpp
| Variable | Tipo | Propósito |
|----------|------|-----------|
| `processname` | (extern) | Compartido con debug.cpp |
| `saved_processname` | `std::string` | Copia de seguridad para deactivate_debugger |
| `baseText` | `uint32_t` | Dirección base del hunk CODE (Amiga) |
| `sizeText` | `uint32_t` | Tamaño del hunk CODE |
| `sections` | `vector<uint32_t>` | Lista de direcciones base de todos los hunks |
| `breakpoint_elf_addresses` | `vector<uaecptr>` | Direcciones ELF originales para relocalización |
| `debugger_state` | `state` | Estado actual de la máquina de estados |

## Breakpoints (Z0)

### Recepción
GDB envía `Z0,<addr>,<kind>` con dirección ELF (ej: `4c0`).

### Relocalización en Z0 handler
```cpp
uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
uaecptr relocatedAdr = adr;

if (adr ya en rango cargado [baseText..baseText+sizeText])
    relocatedAdr = adr;  // usar tal cual
else if (loadOffset > 0 && adr en rango ELF [0x400..0x100400])
    relocatedAdr = adr + loadOffset;  // relocalizar
else if (baseText > 0 && adr < ELF_TEXT_BASE)
    relocatedAdr = adr + baseText;  // -Ttext=0 ELF
else
    relocatedAdr = adr;  // dirección cruda Amiga
```

### Relocalización Diferida
Si `baseText` aún no se conoce, el breakpoint se almacena en `breakpoint_elf_addresses` y su dirección se relocaliza después cuando:
1. `refresh_process_offsets()` calcula baseText (por detección de proceso)
2. `relocate_breakpoints()` se llama, actualizando todos los bpnodes pendientes

## Detección de Proceso

### Flujo
```
debug.cpp ~line 7986:
  if (trace_mode == TRACE_CHECKONLY && (processptr || processname) && !isrom(pc)):
    1. Lee ThisTask de Exec (execbase + 276)
    2. Si es proceso (ln_Type == 13):
      a. Obtiene ln_Name
      b. Obtiene CLI command y segList
      c. Compara: ¿coincide con processname?
         - Si processname empieza con ':' → suffix match
         - Si no → exact match (stricmp)
      d. Si coincide:
         - Itera segList para encontrar cuál hunk contiene el PC
         - Si PC está en rango:
           gdb_notify_process_entry = true
           bp = -1  (stop!)
```

### Función `gdb_match_process_name()` (añadida 2026-05-16)
```cpp
// Soporta:
// 1. processname = "a.exe" → exact match contra ln_Name o CLI command
// 2. processname = ":a.exe" → suffix match (ln_NAME termina en "a.exe")
// 3. CLI BSTR command (byte 0 = length) con el mismo tratamiento
```

## Comandos Útiles para GDB

```gdb
# Diagnóstico
info breakpoints                    # Ver direcciones de breakpoints
info file                           # Ver secciones del ELF
maintenance packet qOffsets         # Ver direcciones de hunks Amiga
maintenance packet qRcmd,627265616b706f696e7473  # "breakpoints" hex

# Monitor commands (qRcmd)
monitor breakpoints                 # Listar breakpoints activos
monitor offset set 0xc00000        # Establecer baseText manualmente
monitor logfile                     # Ver estado del log
monitor logfile on                  # Activar logging
monitor findproc :a.exe             # Buscar proceso manualmente
monitor screenshot c:\temp\shot.png # Capturar pantalla
monitor profile 10 unwind.out prof.out  # Perfil de CPU
monitor warp on                     # Turbo mode
monitor warp off                    # Modo normal
```

## Variables de Configuración WinUAE

```ini
debugging_features=gdbserver       # Activar servidor GDB
debugging_trigger=:a.exe           # Programa objetivo (':'' = suffix match)
use_gui=no                         # Sin GUI
start_gui=0                        # No mostrar config
quickstart=a500,1                  # Config rápida A500
```

## Problemas Conocidos (Resueltos)

### 1. Prefijo `:` en debugging_trigger (RESUELTO 2026-05-16)
**Síntoma**: `gdb_notify_process_entry` nunca se activaba, baseText=0
**Causa**: `debug.cpp` comparaba nombre literal ":a.exe" contra "a.exe"
**Solución**: `gdb_match_process_name()` con soporte de suffix match

### 2. processname perdido en deactivate_debugger (RESUELTO)
**Síntoma**: Tras continue, processname=NULL, detección de proceso no funcionaba
**Causa**: `deactivate_debugger()` liberaba processname con xfree()
**Solución**: `deactivate_debugger_preserve_processname()` guarda en `saved_processname`

### 3. Breakpoints con baseText=0 (RESUELTO)
**Síntoma**: Z0 almacenaba direcciones sin relocalizar
**Causa**: GDB enviaba breakpoints antes de que baseText estuviera disponible
**Solución**: `breakpoint_elf_addresses` + `relocate_breakpoints()` (relocalización diferida)

### 4. qOffsets no funcionaba sin nombre de proceso (RESUELTO 2026-05-16)
**Síntoma**: Si `processname` era NULL o no coincidía, qOffsets devolvía E01
**Solución**: `gdb_find_process_for_pc()` — búsqueda por PC en fallback

### 5. Watchpoints sin relocalización (RESUELTO 2026-05-16)
**Síntoma**: Z2/Z3/Z4 siempre usaban direcciones ELF sin relocalizar
**Solución**: `watchpoint_elf_addresses` + `relocate_breakpoints()` extendida

## Problemas Conocidos (Pendientes)

### 1. Primer qOffsets durante handshake
GDB envía qOffsets antes de que el programa se cargue → inevitablemente E01. PC-based fallback intenta encontrar proceso, pero durante boot puede no haber ninguno. La extensión maneja esto con `refreshRelocation()` que reintenta.

### 2. qOffsets en MCP
El MCP envía qOffsets en `getOffsets()` pero podría beneficiarse de reintentos automáticos.

### 3. Re-detección tras reinicio de programa
Si el programa termina y se lanza de nuevo, `gdb_process_range_entered=true` impide nueva detección. Se requiere `monitor reset` o reconexión.

## Mejoras Implementadas (2026-05-16)

### 1. Shared Process Detection API
- Declaraciones de `processptr`, `processname`, `gdb_notify_process_entry`, `gdb_process_range_entered` movidas a `include/debug.h`
- `gdb_match_process_name()` disponible como función compartida
- Archivos: `include/debug.h`, `debug.cpp`

### 2. PC-based Fallback en qOffsets
- `gdb_find_process_for_pc()` recorre Exec lists buscando proceso cuyo `segList` contenga el PC actual
- Se usa como fallback en el handler qOffsets cuando la búsqueda por nombre falla
- Archivos: `debug.cpp`, `od-win32/barto_gdbserver.cpp`

### 3. Relocalización Diferida de Watchpoints
- `watchpoint_elf_addresses` almacena direcciones ELF de Z2/Z3/Z4
- `relocate_breakpoints()` ahora también maneja watchpoints via mwnodes
- Handlers Z2/Z3/Z4 actualizados con relocalización inmediata/diferida
- Archivo: `od-win32/barto_gdbserver.cpp`

---

*Última actualización: 2026-05-16*
