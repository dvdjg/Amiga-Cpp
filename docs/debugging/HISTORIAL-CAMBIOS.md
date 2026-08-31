# Historial de Cambios en el Sistema de Depuración

Este documento registra todos los cambios realizados a los componentes del sistema de depuración Amiga desde el estado original de Bartman.

---

## Estado Original (Extensión Bartman v1.7.9)

### WinUAE-DBG Original

El código original de `barto_gdbserver.cpp` manejaba:

```cpp
// Handler qOffsets original
} else if(request.substr(0, 8) == "qOffsets") {
    std::string response;
    for(int i = 0; i < numSections; i++) {
        if(!response.empty())
            response += ';';
        char hex[32];
        sprintf(hex, "%x", sectionBases[i]);
        response += hex;
    }
    SendPacket(response);
    
    // baseText se calculaba aquí
    baseText = sectionBases[0];  // Asumiendo CODE es primero
}

// Handler Z0 original
} else if(request.substr(0, 2) == "Z0") {
    uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
    // Sin relocalización - usaba dirección tal cual llegaba
    bpn.value1 = adr;
}
```

### VS Code Extension Original

**mi2.ts original**:
- Obtenía secciones con `info file`
- NO consultaba `qOffsets`
- Emitía secciones sin loadOffset

**symbols.ts original**:
- `relocate(sections)`: Buscaba secciones por nombre
- Sin método `relocateWithOffset`

**amigaDebug.ts original**:
- Usaba `symbolTable.relocate(sections)` directamente

---

## Cambios Realizados (2026-02-22)

### Fase 1: Intento con `info sections`

**Cambio**: Intentar usar `info sections` en lugar de `info file`.

**Resultado**: ❌ Fallido - El GDB de Bartman no soporta este comando.

**Revertido**: Sí, inmediatamente.

### Fase 2: Intento con `monitor offset`

**Cambio**: Añadir comando `monitor offset` a WinUAE que devuelva baseText.

```cpp
// En barto_gdbserver.cpp
} else if(request.substr(0, 14) == "qRcmd,6f6666736574") { // "offset" hex
    char response[64];
    sprintf(response, "baseText=%x\n", baseText);
    SendPacket(hexEncode(response));
}
```

**Resultado**: ❌ Parcialmente fallido - La salida de `monitor` no era capturada correctamente por `sendUserInput` en mi2.ts.

**Revertido**: Código dejado pero no usado.

### Fase 3: Uso de `maintenance packet qOffsets`

**Cambio en mi2.ts**:

```typescript
// Obtener loadOffset de qOffsets
const qOffsetsNode = await this.sendUserInput('maintenance packet qOffsets');
if (qOffsetsNode && qOffsetsNode.output) {
    const text = qOffsetsNode.output.join('');
    const match = /received:\s*"([0-9a-fA-F]+)/.exec(text);
    if (match) {
        const textBase = parseInt(match[1], 16);
        loadOffset = textBase - ELF_TEXT_BASE;
    }
}
```

**Resultado**: ✅ Funciona - loadOffset se calcula correctamente.

### Fase 4: Relocalización Diferida en WinUAE

**Problema identificado**: Los breakpoints se establecían ANTES de que `qOffsets` calculara `baseText`, resultando en direcciones ELF sin relocar.

**Cambio en barto_gdbserver.cpp**:

```cpp
// Nueva constante
constexpr uaecptr ELF_TEXT_BASE = 0x400;

// Almacenamiento de direcciones ELF
std::vector<uaecptr> breakpoint_elf_addresses;

// Nueva función de relocalización
void relocate_breakpoints() {
    if(baseText < ELF_TEXT_BASE) return;
    uaecptr loadOffset = baseText - ELF_TEXT_BASE;
    for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
        uaecptr elfAddr = breakpoint_elf_addresses[i];
        if(elfAddr >= ELF_TEXT_BASE && elfAddr < ELF_TEXT_BASE + 0x100000) {
            uaecptr relocatedAddr = elfAddr + loadOffset;
            for(auto& bpn : bpnodes) {
                if(bpn.enabled && bpn.value1 == elfAddr) {
                    bpn.value1 = relocatedAddr;
                    break;
                }
            }
        }
    }
}

// Modificación del handler Z0
} else if(request.substr(0, 2) == "Z0") {
    uaecptr adr = strtoul(...);
    uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
    uaecptr relocatedAdr = adr;
    if(loadOffset > 0 && adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000) {
        relocatedAdr = adr + loadOffset;
    }
    breakpoint_elf_addresses.push_back(adr);
    bpn.value1 = relocatedAdr;
}

// Llamada en qOffsets
} else if(request.substr(0, 8) == "qOffsets") {
    // ... cálculo de baseText ...
    relocate_breakpoints();  // NUEVO
}
```

**Resultado**: ✅ Los breakpoints se relocalizan correctamente cuando baseText está disponible.

### Fase 5: Mejora del SymbolTable

**Problema identificado**: `symbolTable.relocate(sections)` era frágil porque dependía de coincidencia exacta de nombres de secciones.

**Nuevo método en symbols.ts**:

```typescript
public relocateWithOffset(loadOffset: number) {
    // Aplicar offset a todas las secciones ALLOC
    for(const section of this.sections) {
        if(section.flags?.find((v) => v === "ALLOC") && section.size > 0) {
            section.address = section.vma + loadOffset;
        }
    }
    // Actualizar bases de símbolos
    this.symbols.forEach((symbol) => {
        const section = this.sections.find((s) => s.name === symbol.section);
        if(section) {
            symbol.base = section.address;
        }
    });
}
```

**Cambio en mi2.ts**:

```typescript
// Aplicar loadOffset a las secciones para compatibilidad con relocate()
if (loadOffset > 0) {
    for (const section of sections) {
        section.address += loadOffset;
    }
}

// Emitir secciones (ya relocadas) Y loadOffset (para relocateWithOffset)
this.emit("sections-loaded", sections, loadOffset);
```

**Nota**: `mi2.ts` ahora aplica el offset a las secciones antes de emitirlas, proporcionando dos mecanismos de relocalización:
1. Las `sections` emitidas ya tienen direcciones relocadas (para uso con `relocate()`)
2. El `loadOffset` se emite por separado (para uso con `relocateWithOffset()`)

Esto proporciona compatibilidad hacia atrás y redundancia.

**Cambio en amigaDebug.ts**:

```typescript
this.miDebugger.once('sections-loaded', (sections: Section[], loadOffset?: number) => {
    if(loadOffset && loadOffset > 0) {
        this.symbolTable.relocateWithOffset(loadOffset);
    } else {
        this.symbolTable.relocate(sections);
    }
});
```

**Resultado**: ✅ La relocalización es más robusta y no depende de nombres de secciones.

---

## Resumen de Cambios por Archivo

### barto_gdbserver.cpp

| Línea (aprox) | Cambio | Motivo |
|---------------|--------|--------|
| ~100 | `constexpr uaecptr ELF_TEXT_BASE = 0x400;` | Constante para cálculos |
| ~110 | `std::vector<uaecptr> breakpoint_elf_addresses;` | Almacén de direcciones ELF |
| ~500 | `void relocate_breakpoints()` | Relocalización diferida |
| ~700 | Handler Z0 modificado | Almacenar y relocalizar |
| ~750 | Handler z0 modificado | Limpiar direcciones ELF |
| ~800 | Handler qOffsets modificado | Llamar relocate_breakpoints() |

### mi2.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| `connect()` | Añadido código para obtener loadOffset de qOffsets | Calcular relocalización |
| `connect()` | Emitir loadOffset en evento sections-loaded | Pasar offset a amigaDebug |

### symbols.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| `relocateWithOffset()` | Nuevo método | Relocalización más robusta |
| `relocate()` | Añadido logging | Diagnóstico |
| `getFunctionAtAddress()` | Añadido logging | Diagnóstico |

### amigaDebug.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| Handler `sections-loaded` | Usar loadOffset si disponible | Preferir nuevo método de relocalización |

---

## Código Original vs Código Modificado

### Handler Z0 - Comparación

**Original**:
```cpp
} else if(request.substr(0, 2) == "Z0") {
    auto comma = request.find(',', strlen("Z0"));
    if(comma != std::string::npos) {
        uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
        // Buscar bpnode libre y asignar
        for(auto& bpn : bpnodes) {
            if(bpn.enabled) continue;
            bpn.value1 = adr;  // Sin relocalización
            bpn.enabled = true;
            // ...
            break;
        }
        SendPacket("OK");
    }
}
```

**Modificado**:
```cpp
} else if(request.substr(0, 2) == "Z0") {
    auto comma = request.find(',', strlen("Z0"));
    if(comma != std::string::npos) {
        uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
        
        // NUEVO: Calcular loadOffset si baseText disponible
        uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
        uaecptr relocatedAdr = adr;
        if(loadOffset > 0 && adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000) {
            relocatedAdr = adr + loadOffset;
        }
        
        // NUEVO: Almacenar dirección ELF para relocalización diferida
        breakpoint_elf_addresses.push_back(adr);
        
        for(auto& bpn : bpnodes) {
            if(bpn.enabled) continue;
            bpn.value1 = relocatedAdr;  // Usar dirección relocada
            bpn.enabled = true;
            // ...
            break;
        }
        SendPacket("OK");
    }
}
```

---

---

## Fase 6: Fix Prefijo `:` en debugging_trigger (2026-05-16)

### Problema
La detección de proceso en `debug.cpp` no manejaba el prefijo `:` en `processname`, causando que `gdb_notify_process_entry` nunca se activara y `baseText` quedara siempre en 0.

### Causa Raíz
`debug.cpp:7965` comparaba:
```cpp
!stricmp(name, processname)  // "a.exe" vs ":a.exe" → DISTINTO
```

Mientras que `barto_gdbserver.cpp` tenía lógica de `:` en `find_process_by_name()` desde Fase 4:
```cpp
bool match_suffix = (target_name[0] == ':');
const char* match_pattern = match_suffix ? (target_name + 1) : target_name;
// Luego: ends-with match
```

### Cambios en debug.cpp
1. **Nueva función**: `gdb_match_process_name()` (estática) que maneja:
   - `processname` sin `:` → exact match contra `ln_Name` y CLI command (BSTR)
   - `processname` con `:` → suffix match: nombre termina con el patrón?
   
2. **Match condition modificada**: La línea original de 4 comparaciones anidadas se reemplaza por:
```cpp
if (activetask == processptr || gdb_match_process_name(name, command, processname)) {
```

### Verificación
- `find_process_by_name()` en barto_gdbserver.cpp ya funcionaba correctamente con `:` (usado en qOffsets y refresh_process_offsets)
- `debug.cpp` ahora usa la misma lógica
- `deactivate_debugger_preserve_processname()` ya preservaba processname via `saved_processname` (Fase 5)

### Archivos Modificados
| Archivo | Cambio |
|---------|--------|
| `WinUAE-DBG/debug.cpp` | Nueva función `gdb_match_process_name()`, match condition actualizada |

### Documentación Añadida
| Archivo | Contenido |
|---------|-----------|
| `doc/debugging/SUMMARY.md` | Documento completo de arquitectura y diagnóstico |
| `docs/WINUAE-DBG-ARCHITECTURE.md` | Añadida sección de detección de proceso, bug del `:`, tabla de errores |

---

## Fase 7: Shared API, PC-based Fallback y Relocalización de Watchpoints (2026-05-16)

### Mejora 1: Shared Process Detection API
Se movieron las declaraciones de `processptr`, `processname`, `gdb_notify_process_entry`, `gdb_process_range_entered` a `include/debug.h` como API compartida entre `debug.cpp` y `barto_gdbserver.cpp`.

**Nueva función**: `gdb_match_process_name()` declarada en header compartido.

### Mejora 2: PC-based Fallback en qOffsets
**Nueva función**: `gdb_find_process_for_pc(pc)` en `debug.cpp` — busca en las listas de Exec (TaskReady, TaskWait) el primer proceso cuyo `segList` contenga la dirección `pc`.

**Uso en qOffsets**: Si la búsqueda por nombre (`find_process_by_name` / `find_cli_with_module`) falla, se intenta encontrar el proceso por PC actual. Esto permite que `qOffsets` funcione incluso cuando `processname` es NULL o no coincide exactamente.

### Mejora 3: Relocalización Diferida de Watchpoints
**Nuevo vector**: `watchpoint_elf_addresses` almacena direcciones ELF de watchpoints Z2/Z3/Z4.

**`relocate_breakpoints()` extendida**: Ahora también itera `watchpoint_elf_addresses` y relocaliza las direcciones en los `mwnodes` correspondientes.

**Handler Z2/Z3/Z4 modificado**: Aplica la misma lógica de relocalización que Z0:
- Si `baseText` conocido → relocaliza inmediatamente
- Si no → guarda en `watchpoint_elf_addresses` para relocalización diferida

### Archivos Modificados
| Archivo | Cambio |
|---------|--------|
| `include/debug.h` | Declaraciones compartidas + `gdb_find_process_for_pc()` |
| `debug.cpp` | Implementación `gdb_find_process_for_pc()` (recorre Exec lists) |
| `od-win32/barto_gdbserver.cpp` | `watchpoint_elf_addresses`, `relocate_breakpoints()` extendida, Z2/Z3/Z4 relocalización, qOffsets PC fallback |

## Notas para Futuras Modificaciones

1. **Si se modifica el toolchain amiga-gcc**: Verificar que `ELF_TEXT_BASE` siga siendo `0x400`.

2. **Si se añaden nuevos tipos de breakpoints**: Aplicar la misma lógica de relocalización diferida.

3. **Si se cambia el formato de qOffsets**: Actualizar el regex en mi2.ts y la lógica de parseo.

4. **Para debug de los cambios**: Buscar mensajes `barto_log` en la salida de debug y mensajes `MI2:` en la consola de VS Code.

5. **La detección de proceso ahora es triple**: `debug.cpp` detecta por PC en rango segList; `barto_gdbserver.cpp` busca por nombre en Exec lists; fallback por PC.

6. **Los watchpoints Z2/Z3/Z4 ahora también se relocalizan**: Tanto inmediata como diferidamente.

---

## Fase 8: Breakpoints que no se detienen y "pantalla en ensamblador" (2026-08-23)

### Síntoma

- F5 arranca WinUAE-DBG + GDB, pero los breakpoints no se detienen (o la parada no se muestra).
- Cuando se logra detener, el stack trace muestra desensamblado en vez de código C++.

### Causa raíz

GDB conecta **durante el arranque** de WinUAE, antes de que se cargue el proceso. En ese
momento `qOffsets` devuelve `0`, así que la tabla de símbolos de GDB queda sin relocalizar
(direcciones ELF con base `0x400`). Consecuencias:

1. GDB resuelve los breakpoints `file:line` a direcciones ELF y WinUAE-DBG las relocaliza
   en el servidor (por eso llegan a *romperse*), pero al parar GDB no puede asociar el PC
   de ejecución con su lista de breakpoints y reporta la parada como `SIGTRAP`
   (`signal-received`) en vez de `breakpoint-hit`.
2. La extensión solo refrescaba `loadOffset` en `breakpointEvent`. Como la parada llega como
   `signal-received`, `loadOffset` quedaba en 0, el fallback `addr2line` nunca se ejecutaba y
   el frame se mostraba como ensamblador.
3. `configurationDoneRequest` reanudaba cualquier parada, incluidas las reales, de modo que
   un breakpoint llegado antes de terminar la configuración se perdía ("no se detiene").

### Cambios en `vscode-amiga-debug` (`src/amigaDebug.ts`)

| Cambio | Efecto |
|--------|--------|
| `ensureLoadOffset()` llamado desde todos los manejadores de parada (`breakpointEvent`, `signalStopEvent`, `stopEvent`, `watchpointEvent`, `stepEndEvent`) y desde `stackTraceRequest` | `loadOffset` siempre disponible → el fallback `addr2line` resuelve la fuente y se muestra C++ |
| `signalStopEvent`: `SIGTRAP` se etiqueta como `breakpoint` | La parada por breakpoint de WinUAE-DBG se muestra como breakpoint real |
| `relocateBreakpoints()` se re-ejecuta desde `refreshLoadOffset()` cuando GDB quedó sin relocalizar (`gdbSymbolsUnrelocated`) | Los breakpoints pendientes se recrean como direcciones runtime (`break *0xc0f02a`) → paradas siguientes son `breakpoint-hit` de verdad y GDB resuelve la fuente |
| `gdbSymbolsUnrelocated` distingue qOffsets inicial = 0 (GDB sin relocalizar) | Evita el doble desplazamiento de direcciones al relocalizar breakpoints |
| `configurationDoneRequest`: solo continúa si el target está corriendo o la parada no se mostró (`stopSurfaced`) | No reanuda un breakpoint real; la parada inicial del servidor se sigue auto-continuando |
| `getAddressForFileLine`: regex acepta `is at address` (líneas "contains no code") | La relocalización funciona también en líneas sin instrucción propia |

### Archivos implicados

- `vscode-amiga-debug/src/amigaDebug.ts` (fork local, versión 1.8.1).

### Notas

- WinUAE-DBG y el backend GDB+WinUAE ya funcionaban (relocalización y parada correcta);
  el fallo estaba en la capa DAP de la extensión.
- `debugAdapter.js` standalone está roto (`s.C is not a function`) porque `EMBED_DEBUG_ADAPTER=true`
  hace que VSCode use el servidor embebido; no afecta al flujo F5 actual.
- Tras desplegar el `dist` recompilado hay que recargar la ventana de VSCode.

---

## Fase 9: Causa raíz real — GDB auto-continúa breakpoints no relocalizados (2026-08-23)

### Corrección al diagnóstico de la Fase 8

Las trazas del adaptador (`%TEMP%\amiga-debug-trace.log`) y del RSP
(`%TEMP%\winuae-gdb.log`) mostraron la causa raíz definitiva:

- Cuando GDB conecta **antes** de que se cargue el proceso, `qOffsets` devuelve `0`
  y la tabla de símbolos de GDB queda sin relocalizar (direcciones ELF).
- WinUAE-DBG sí relocaliza los breakpoints y **se disparan** (`T05swbreak`), pero
  como GDB no puede asociar el PC de ejecución con su lista (sin relocalizar),
  **GDB no emite ningún `*stopped` y auto-continúa silenciosamente**.
- La extensión nunca recibe el evento de parada, por lo que los arreglos de la
  Fase 8 (`ensureLoadOffset`, `SIGTRAP→breakpoint`) no llegaban a ejecutarse.

### Solución (dos partes)

1. **WinUAE-DBG** (`od-win32/barto_gdbserver.cpp`): cuando en la entrada de proceso
   `offsets_unresolved` estaba activo (GDB conectó antes de cargar), se fuerza un
   `S05` (parada plana, `signal-received`) en lugar de `T05swbreak`. GDB sí
   superficie un `S05`, dando a la extensión el momento para relocalizar.
   Flag nuevo: `gdb_force_s05_at_entry`.

2. **Extensión** (`src/amigaDebug.ts`): en `signalStopEvent` (SIGTRAP) se hace
   `await ensureLoadOffset()` + `relocateBreakpoints()` y luego se decide:
   - Si el PC de la parada coincide con un breakpoint relocalizado → se muestra
     como breakpoint (con fuente C++ vía addr2line).
   - Si no → es la parada de entrada de proceso forzada → se auto-continúa
     después de relocalizar (para que los breakpoints del usuario se disparen
     como `breakpoint-hit` reales).
   Campos nuevos: `relocatedBreakpointAddresses`, `extractPcFromStop()`.

### Verificación

Con un cliente DAP autónomo (adaptador standalone, `debugAdapter.js` arreglado
quitando `dependOn` en `webpack.config.js`) y retardo en la startup-sequence para
forzar `qOffsets=0`:

- `qOffsets` inicial = `0`.
- Parada `*stopped,reason="signal-received",signal-name="SIGTRAP"`.
- `EVENT stopped reason=breakpoint` y `stackTrace` → `main.cpp:298` (fuente C++).

### Extra

- `debugAdapter.js` standalone estaba roto (`s.C is not a function`) por el
  multi-entry `dependOn` de webpack; se arregló el config (entradas independientes).
- El handshake GDB↔WinUAE puede fallar intermitentemente ("Invalid hex digit 79" /
  "Bogus trace status reply") cuando WinUAE-DBG emite salida de consola `O`
  durante la conexión; es un fallo pre-existente, pendiente de solución aparte.

---

## Fase 10: "Step over / step into" no avanza por líneas (2026-08-23)

### Síntoma

- Los breakpoints se detienen (Fase 9), pero al pulsar Step over / Step into el
  depurador avanza por **instrucciones** (ensamblador), no por líneas de fuente.

### Causa raíz

Cuando GDB conecta antes de cargar el proceso (`qOffsets=0`), su tabla de líneas
queda sin relocalizar. La extensión compensaba los breakpoints del usuario
relocalizándolos a direcciones runtime, pero **el step por líneas de GDB usa sus
propios breakpoints temporales** en la siguiente línea (dirección ELF): WinUAE-DBG
los relocaliza y se disparan, pero GDB no puede asociarlos (PC runtime ≠ su
tabla) y **auto-continúa silenciosamente**. Por eso `exec-next` no se detiene.

### Solución (no destructiva)

En la parada de entrada de proceso, la extensión ejecuta:

```
add-symbol-file <elf> <text_base_runtime>
```

GDB aprende las direcciones runtime (además de las ELF): los breakpoints `file:line`
resuelven ahora a **dos ubicaciones** (ELF + runtime), la runtime coincide con el
PC real → `breakpoint-hit` real, y el step por líneas (`exec-next`) funciona.

Se descartó la reconexión `target remote` porque mata el proceso (`vKill`/`k`).
`add-symbol-file` es no destructivo.

### Archivos

- `vscode-amiga-debug/src/amigaDebug.ts`: `relocateGdbSymbols()` ahora usa
  `add-symbol-file` (antes intentaba reconectar); se eliminan los breakpoints
  viejos y se re-añaden `file:line` (resuelven a runtime).

### Nota

- El handshake GDB↔WinUAE puede fallar intermitentemente ("Invalid hex digit 79" /
  "Bogus trace status reply") porque WinUAE-DBG emite salida de consola `O`
  durante la conexión; pendiente de solución aparte.

---

## Fase 11: Fix del handshake intermitente y de la relocalización de breakpoints (2026-08-23)

### 11.1 Handshake GDB↔WinUAE intermitente

**Síntoma**: al conectar GDB fallaba a veces ("Invalid hex digit 79", "Bogus trace
status reply") por la salida de consola `O` (mensajes de montaje de filesystems,
KPrintF) intercalada con el handshake inicial (qSupported, qTStatus, qOffsets…).

**Causa**: `barto_gdbserver.cpp` enviaba `$O` en cuanto había conexión, sin esperar
a que GDB terminara el handshake.

**Fix** (`WinUAE-DBG/od-win32/barto_gdbserver.cpp`): se difiere toda salida `O`
(buffer `pending_o_output`) hasta que GDB emite el primer `vCont` (continue/step),
y ahí se vacía. `KPutCharX` y `log_output` usan el mismo gateo.

### 11.2 Relocalización de breakpoints: bug de pendingBreakpoints + offset 0x400

**Síntoma**: tras el fix de `add-symbol-file` (Fase 10), los breakpoints no se
disparaban como `breakpoint-hit` en el flujo DAP.

**Causa**:
1. `refreshLoadOffset()` llamaba a `relocateBreakpoints()` que **limpiaba
   `pendingBreakpoints`** antes de que `relocateGdbSymbols()` los re-añadiera.
2. `relocateBreakpoints()` usaba `info line` (convención "text base 0" de GDB) y
   calculaba direcciones **0x400 cortas**.

**Fix** (`vscode-amiga-debug/src/amigaDebug.ts`):
- `refreshLoadOffset()` ya no llama a `relocateBreakpoints()`; la relocalización
  la hace `relocateGdbSymbols()` (add-symbol-file) que consume los pendientes.
- `relocateBreakpoints()` (fallback) suma `0x400` a la dirección de `info line`
  cuando `gdbSymbolsUnrelocated`.

**Verificado (DAP)**: breakpoint `main.cpp:296` → `breakpoint-hit` + stackTrace
`main.cpp:296` y `main.cpp:409`; Step over usa `exec-next` (por líneas) y avanza.

---

## Fase 12: Lecciones de inyección de entrada y teclado por automatización (2026-09-01)

### 12.1 Inyección de entrada por el monitor: lock obligatorio y canal correcto

**Síntoma**: inyectar `input mouse …`, `input key …` o `poke …` durante una ejecución
congelaba la emulación ~2 frames después (el `run-report` mostraba frames repetidos y
`pc=0xfe582c`), indistintamente del comando.

**Causa**: el canal lateral exige un **lock explícito** con modo; sin él, el stub responde
`{"ok":false,"error":"lock_required","required":"takeover"}` (o `assist`). El runner los
enviaba por **qRcmd del GDB (2345)**, cuyas órdenes con side-effects no se encolan a
`vsync_pre` como las del canal lateral (2346), y al rechazarse/colgarse nunca se liberaba
nada → emulación muerta en apariencia.

**Regla operativa** (replicada del CLI `winuae-side-channel.sh`):
`input`/`profile` requieren lock `assist` (la emulación sigue corriendo bajo asistencia) y
`poke`/`rollback` requieren `takeover`. Patrón correcto:

```text
lock acquire <owner> assist|takeover
<input|poke ...>                       # por el canal lateral 2346, NUNCA por qRcmd
lock release <owner>
```

En `run-demo.ts` se implementó `sendSideChannelCommand()` (socket 2346, protocolo
saludar → orden → respuesta JSON) y `withSideChannelLock()` (acquire → fn → release). Para
una tecla sintética (`--automation-key <sc>`) el make y el clear se hacen en dos locks
`takeover` separados con un `sleep` entre medias: el demo ve el make durante la ventana
desbloqueada (`readbackAfterMake=05`, `readbackAfterClear=ff`).

### 12.2 `pc=0xfe582c` NO es señal de crush

El estado final del canal lateral (`sideChannel.state.pc`) suele valer `0xfe582c` (bucle de
idle de la ROM del Kickstart) incluso en ejecuciones sanas (frames avanzando hasta 140+).
No interpretarlo como crash; la señal fiable es si `g_eng_run_status.frame` avanza entre
muestras. El único «crash real» observado fue el de la entrada serie de teclado (§12.3).

### 12.3 El port serie del teclado (SDR/ICR) de CIAA crashea en WinUAE-DBG

**Síntoma**: leer SDR/ICR de CIAA-A (teclado por el flujo serial) al llegar el primer byte
inyectado lanzaba una excepción (bus/address error → vector a ROM, `pc=0xfe582c`), y el
demo quedaba en el bucle de boot del KS. Descartado que fuera el `apply_tech` (aislado).

**Conclusión**: para «simular una tecla» robusta se usa **automatización por memoria**,
la misma diseñada en el repo para entrada (`amiga-automation-input.ts`): la demo define
`extern "C" volatile eng::u8 g_automation_keycode` y `poll_keyboard()` detecta el edge de
valor; el host la escribe con `poke` bajo lock `takeover`. Ver `input_poll.hpp`. La vía
auténtica (CIAA serial + `input key`) queda como incidencia abierta: hay que re-probar con
lock `assist` antes de habilitarla.

### 12.4 Resolución de dirección runtime: fallback incorrecto para símbolos tempranos

`resolveRuntimeSymbolAddress()` cae a un fallback (base del primer hunk + `symbol-0x400`)
cuando el símbolo no cae en ninguna sección del `.map`; para `g_automation_keycode`
(0x009578) devolvía `0xc15b70` cuando la dirección real estaba junto a
`g_eng_run_status` (0x00957a → runtime `0xc15b72`). Escribir con `poke` a una dirección
errónea corrompe memoria del demo. Solución: anclar el símbolo al **runtimeAddress probado
de `g_eng_run_status`** (`report.sideChannel.runtimeAddress`) aplicando el **delta del
`.map`** (mismo hunk de datos para contiguos). No asumir que los `sections` del estado son
las bases de todo el espacio de datos.

### 12.5 `extern "C"` dentro del namespace anónimo se mangla

Una definición `extern "C"` colocada dentro de `namespace { }` del demo genera el símbolo
`_ZN12_GLOBAL__N_1….E` (mangado), y la referencia `extern "C"` global del header queda
como `U g_automation_keycode` sin resolver → `ld` undefined. Los globals que el runner busca
por nombre (`g_eng_run_status`, `g_tech_new`, `g_automation_keycode`) deben definirse en
**ámbito global real** (p. ej. dentro del bloque `extern "C" { }` superior del archivo) para
conservar el símbolo sin mangle en el `.map`/`nm`.

### 12.6 Edge espurio de arranque del joystick

El emulador deja las líneas del puerto sin joystick a 0 (=«pulsado» tras invertir) durante
los primeros frames, así que cualquier «avance en cualquier() con edge» dispara una vez al
arrancar (técnica 0→1). Regla: ignorar los bordes hasta tener una ventana de arranque
(`frame >= 2` o haber observado al menos un frame en reposo). En hardware real las líneas
van a +5V (1 = no pulsado), pero el emulador no.

### 12.7 Herramientas resultantes

- `run-demo --inject-commands "input key 2 1|sleep:60|input key 2 0" --inject-sample N`:
  envía órdenes del monitor por el canal lateral bajo lock `assist` (registradas en
  `report.inputInjection`).
- `run-demo --automation-key <scancode> --inject-sample N`: poke/takeover de
  `g_automation_keycode` (make/clear con `readbackAfterMake`/`readbackAfterClear` en el
  report y `report.automationKey`).
- El marcador 107 expone la técnica activa: byte superior `0x10|técnica`.

---

*Actualizado: 2026-09-01*
