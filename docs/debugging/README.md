> **Procedencia:** esta carpeta y sus documentos proceden del repo hermano `Cursor-Amiga-C`
> (engine en C) y describen la arquitectura y los arreglos del sistema de depuración
> WinUAE-DBG (gdbserver), que es el mismo que usa este repo a través del canal lateral
> (`tools/debug/`) y del runner (`tools/run/`).
>
> Son **bitácoras** (post-mortems, consultas, historial): pueden citar nombres de API de
> versiones anteriores (`div16`/`mul16`). El estado vigente está en `docs/engine/`.

# Documentación del Sistema de Depuración WinUAE-DBG

Esta carpeta contiene documentación técnica sobre el sistema de depuración Amiga basado en WinUAE.

## Documentos

| Documento | Descripción |
|-----------|-------------|
| [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md) | Arquitectura general del sistema: componentes, protocolos, flujos de datos |
| [RELOCATION-FIX.md](RELOCATION-FIX.md) | Explicación detallada del problema de relocalización de direcciones y su solución |
| [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md) | Registro de todos los cambios realizados desde el código original de Bartman |
| [DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md](DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md) | Diagnóstico y RESOLUCIÓN de los artefactos de arranque de las demos 060/201: doble texto (desbordamiento de `draw_text` por falta de corte en NUL), banda cian 0x0AA (sprite DMA del sistema vivo, causalidad probada A/B), instalación de copperlist fuera de VBL (COPJMP1 a media pantalla) y paleta del pie. Incluye fixes aplicados, sondas y evidencia de verificación (§11). |
| [AUDIO_DEBUG.md](AUDIO_DEBUG.md) | Procedimiento de depuración de sonido: analizar la onda en host (generador/analizador), puente a C++, y verificación del hardware por canal lateral (DMACONR, registros AUDx, volcado de muestra). |
| [LECCIONES-PORTE-BLITTER-DEMOSCENE.md](LECCIONES-PORTE-BLITTER-DEMOSCENE.md) | Post-mortem del porte del Blitter de `flatshade-convex`: la raya por vértice, por qué se "normalizó" un truco de registro (`BLTDPTR`), el papel del AHRM y el checklist para no repetirlo al importar efectos. |
| [LECCIONES-ENGINE-CPP23.md](LECCIONES-ENGINE-CPP23.md) | Lecciones de proceso del engine C++23 (math/util/efectos): casos de test que escondían bugs, `-Werror=narrowing`, medir A/B antes de afirmar mejoras, no duplicar `constexpr`, convención `runtime_palette()`/`apply_into`, numeración de tests y separar referencia de bitácora. |
| [CONSULTA-OPTIMIZACION-BLITTER-DEMOSCENE.md](CONSULTA-OPTIMIZACION-BLITTER-DEMOSCENE.md) | Consulta técnica autocontenida para una IA externa: el port C++ de `flatshade-convex` es 2.4x más lento que el original (670k vs 287k ciclos/frame) pese a hacer los mismos blits; incluye el código original, el port, el desglose medido por secciones y las preguntas sobre el modelo de coste del Blitter y la transformación. |
| [SCENE_REBUILD_EFECTOS.md](SCENE_REBUILD_EFECTOS.md) | Investigación abierta: migrar una demo al modelo de efectos con **reconstrucción por frame** del `Scene` descoloca la lista de Copper (la demo validada se revirtió). Incluye lo descartado, las hipótesis y cómo atacarlo (test host comparativo). |
| [CONSULTA-GROK-DISCO-Y-LOADER.md](CONSULTA-GROK-DISCO-Y-LOADER.md) | Consulta técnica autocontenida para una IA externa (Grok): acceso a disco a bajo nivel (`df0:` bloqueado sin Workbench, `trackdisk.device`, buffers DMA en Chip RAM), carga dinámica `.englib`+HUNK, inyección de teclado, E/S async de `dos.library` y ADF datos/arranque. Incluye el estado verificado, la evidencia y las preguntas. |
| [captura-negra-intermitente-050-051.md](captura-negra-intermitente-050-051.md) | Investigación abierta: las demos 050/051 producen a veces una captura 100 % negra pese a READY. No es el fuente de la demo (051↔050), ni el config (solo cambia la ruta `dh1`), ni los blits; es intermitente/por estado. Incluye lo descartado y el siguiente paso (leer registros reales por GDB en la captura). |
| [octamed-startmusic-hang.md](octamed-startmusic-hang.md) | Investigación abierta (A1): el playroutine OctaMED (KONEY) arranca con `jsr _startmusic` pero **cuelga** bajo el engine. Infra validada (VASM ensambla/enlaza el playroutine + el módulo `INCBIN`; 0 indefinidos; módulo MMD1); se probaron VBlank/CIA, sin mixer y `a6`. Incluye hipótesis y pasos (GDB sobre `_startmusic`). |
| [audio-stream-irq-rate.md](audio-stream-irq-rate.md) | Investigación abierta (A5): en `272_audio_stream` la IRQ de audio (nivel 4) dispara ~34× más rápido que `AUDxPER * AUDxLEN` (~517/s vs ~15/s) → *underruns* y comportamiento errático (a veces informa en frame 20, a veces cuelga). Incluye el mecanismo (AHRM `:4378`; WinUAE `audio.cpp:2523/2631/2722`), lo descartado (`ADKCON`, INTREQ de otro canal, `period_for_hz`, leer registros write-only) y dos bugs reales corregidos de paso (el estado compartido con la IRQ sin `volatile`; contadores de 32 bits que se desgarran). |

## Resumen del Sistema

El sistema permite depurar código C/C++ compilado para Amiga ejecutándose en el emulador WinUAE, usando VS Code como IDE.

```
VS Code Extension ←→ GDB (Bartman fork) ←→ WinUAE-DBG (GDB Server)
      ↓                    ↓                       ↓
   DAP Protocol        MI Protocol            RSP Protocol
```

## Problema Principal Resuelto

El GDB modificado de Bartman no aplica correctamente la relocalización de símbolos. Esto causaba:
- Breakpoints que no se activaban (direcciones incorrectas)
- Stack traces mostrando desensamblado en lugar de código fuente
- Variables no disponibles

La solución implementada:
1. **WinUAE-DBG**: Relocalización diferida de breakpoints en `barto_gdbserver.cpp`
2. **VS Code Extension**: Relocalización del SymbolTable usando `loadOffset` en `symbols.ts`

## Para Desarrolladores

### Compilar WinUAE-DBG

```bash
cd WinUAE-DBG
./build.bat   # o usar Visual Studio
```

### Compilar Extensión VS Code

```bash
cd vscode-amiga-debug
npm install
npm run compile
```

### Instalar Extensión Modificada

```bash
cp dist/extension.js ~/.cursor/extensions/bartmanabyss.amiga-debug-1.7.9/dist/
# Reiniciar VS Code/Cursor
```

### Diagnóstico

1. **Logs de WinUAE**: Buscar mensajes `GDBSERVER:` en la consola de debug de Visual Studio
2. **Logs de la extensión**: Buscar mensajes `MI2:`, `SymbolTable.`, `amigaDebug:` en la consola de VS Code
3. **Comandos GDB útiles**:
   - `info breakpoints` - Ver direcciones de breakpoints
   - `maintenance packet qOffsets` - Ver direcciones de hunks
   - `info file` - Ver secciones del ELF

## Fix: Prefijo `:` en debugging_trigger (2026-05-16)

### Problema
`debugging_trigger=:a.exe` no coincidía con el proceso `a.exe` en la detección automática de `debug.cpp`. El `:` se interpretaba como parte del nombre.

### Causa Raíz
`debug.cpp` línea 7965 comparaba `processname` directamente con `ln_Name`:
```cpp
!stricmp(name, processname)  // "a.exe" vs ":a.exe" → FALLA
```

Mientras que `barto_gdbserver.cpp` sí tenía lógica de `:` en `find_process_by_name()`.

### Solución
Añadida función `gdb_match_process_name()` en `debug.cpp` que maneja el prefijo `:`:
- Detecta `:` inicial y extrae el patrón
- Compara como suffix match: `name` termina con el patrón?
- Aplica la misma lógica a comandos CLI (BSTR)

### Archivos Modificados
- `WinUAE-DBG/debug.cpp`: Nueva función + actualización del match condition (commit `d38fc0ab`)

## Contacto

Para problemas o mejoras, consultar el historial de cambios y la documentación de arquitectura.

## Documentación Relacionada
- [SUMMARY.md](SUMMARY.md) - Documento completo de arquitectura y diagnóstico
