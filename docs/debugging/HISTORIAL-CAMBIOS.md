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

*Actualizado: 2026-05-16*
