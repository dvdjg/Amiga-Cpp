# Roadmap: curso AMC (Mark Wrobel), documentación local, auditoría del engine y depuración automática

Plan por fases para **ingerir** el material del *Amiga Machine Code Course* (Mark Wrobel), **enriquecer** la documentación del repo **sin sustituir** texto salvo corrección clara, **revisar** el `engine/` frente a los temas del curso, y **alinear** capacidades de WinUAE / MCP / DAP con tests y depuración automática.

## Fuentes (rutas de referencia)

| Origen | Ruta / URL | Notas |
|--------|------------|--------|
| Índice local (ejemplo) | `D:/Amiga/books_and_tutorials/Amiga Machine Code Course - Mark Wrobel/web/www.markwrobel.dk/project/amigamachinecode/index.html` | Equivalente a `file:///D:/Amiga/.../index.html` |
| Sitio publicado | `https://www.markwrobel.dk/post/...` | Muchos enlaces del índice apuntan solo aquí; el espejo local puede estar **incompleto** |
| Espejo parcial comprobado | `.../www.markwrobel.dk/post/*/index.html` | En el entorno de desarrollo solo constan **16** `index.html` bajo `post/`; faltan cartas solo-online hasta completar `wget`/HTTrack |

**Atribución:** el contenido del curso es de **Mark Wrobel**; la documentación derivada en este repo debe ser **resumen, mapa y enlace**, no copia literal de largos extractos sin permiso. Preferir paraísos legales: tablas de equivalencia, “ver lección X para diagrama Y”, y síntesis propia.

---

## Fase 1 — Inventario y normalización del material

**Objetivo:** saber exactamente qué lecciones existen y cuáles están offline.

1. **Parsear** `index.html` del curso y extraer todas las entradas `(título, href)` a un fichero versionado, p. ej. `doc/reference/amc-wrobel/lesson-index.tsv` *(crear en esta fase)*.
2. **Clasificar** cada `href`:
   - `relativo` → comprobar si existe bajo `web/www.markwrobel.dk/post/`.
   - `https://www.markwrobel.dk/...` → marcar “solo web” hasta descargar.
3. **Completar espejo** *(opcional pero recomendado)*: script documentado (wget/HTTrack) con lista de exclusión de CDN, guardando solo HTML necesario para lectura offline; no commitear binarios pesados sin política de repo.
4. **Duplicado** `www.markwrobel.dk-1`: decidir cuál es canónico y documentar en el mismo TSV.

**Criterio de cierre:** índice TSV generado y revisado; lista explícita de lecciones **pendientes de espejo**.

---

## Fase 2 — Mapa curso ↔ documentación local ↔ engine

**Objetivo:** cada bloque del curso apunta a **dónde** está ya cubierto en nuestro árbol y **qué** falta.

Crear `doc/reference/amc-wrobel/topic-map.md` con tabla del estilo:

| Tema (lección aprox.) | Archivo(s) doc repo | Módulo engine / app | Batería / test | Acción |
|----------------------|---------------------|---------------------|----------------|--------|
| Setup, ensamblador, depurador carta I | `debug-with-ai.md`, `mcp-live-coding-workflow.md` | — | — | Añadir subsección “equivalente WinUAE-GDB vs FS-UAE del curso” |
| Copper, branching | `techniques/`, `engine-subsystems.md` | `display.c`, `copper_list.c` | C01, C02 plan | Completar notas HRM + enlace lección |
| DMA, bitplanes | `engine-architecture.md`, AHRM index | `display.c`, `system.c` | T0x, B0x | — |
| Sprites | `engine-subsystems.md` | `sprite.c` | S01 | — |
| Blitter (3 partes) | técnicas + roadmap tests | `blitter.c` | B01–B06 | Tabla minterm / shift vs curso |
| Scroll, colorcycling | `demoscene-effects-integration.md` | `view.c`, copper | plan V04 | — |
| Audio, wavetable | `engine-subsystems.md` (audio) | `audio.c` | A01 plan | — |
| Interrupciones | `m68k-stack-and-calling-notes.md` | `system.c` | — | Cruzar con VBL handler |
| Memoria, ficheros, CLI, trackdisk | `amiga-kernel-loader-notes.md`, `engine_dos_io` | `memory.c`, `dos_io.c` | M0x, D01 plan | — |
| Letter 11 printing, 12 linedraw/rotate | *(según índice)* | `engine_blit_line`, futuro | B03, plan | Gaps explícitos |

**Criterio de cierre:** `topic-map.md` con ≥ 80 % de las entradas del índice del curso filas asignadas (aunque sea “sin doc local — proponer párrafo en X”).

---

## Fase 3 — Complementar documentación (política **aditiva**)

**Objetivo:** más contexto y enlaces, **no** reescrituras agresivas.

1. **Regla:** no eliminar párrafos existentes salvo **error factual** o duplicado literal intolerable; en caso de duda, añadir nota “Ver también” o subsección *Complemento (AMC)*.
2. **Dónde escribir resúmenes del curso:** preferir `doc/reference/amc-wrobel/*.md` (notas por lección o por tema) y enlazar desde `engine-architecture.md`, `engine-subsystems.md`, `techniques/README.md`, etc.
3. **API pública (`engine/include/*.h`):**
   - Añadir bloques breves `/** ... */` donde falten: parámetros, precondiciones CHIP, límites OCS, interacción con `TakeSystem`.
   - El proyecto no usa “clases” en C++; la palabra del usuario se interpreta como **tipos** (`typedef struct`) y **módulos**; documentar structs públicos en su `.h`.
4. **Referencias cruzadas:** desde cada módulo `engine/src/*.c`, comentario de una línea opcional `/* Ver doc/reference/amc-wrobel/... */` solo si aporta valor pedagógico.

**Criterio de cierre:** checklist en `topic-map.md` con casillas “doc cabecera OK” por archivo `engine.h`, `engine_extensions.h` y cabeceras de subsistemas prioritarios (blitter, display, sprite, system).

---

## Fase 4 — Auditoría de rutinas del engine (funcionalidad importante)

**Objetivo:** detectar huecos respecto a lo que el curso enseña como “mínimo útil” en demo/juego.

Metodología sugerida:

1. Por cada fila de `topic-map.md` con componente engine, preguntar: **¿la API expresa la operación del curso o obliga a ensamblador / custom crudo?**
2. Lista de revisión rápida (ejemplos no exhaustivos):
   - Blitter: ¿minterms documentados, cookie cut, líneas, espera A1000, tamaños?
   - Copper: ¿WAIT seguro, doble buffer, interacción COP2?
   - Sprites: ¿adjuntos, prioridad frente a playfield, desactivación?
   - Audio: ¿solo mute o también camino documentado para una nota/sample?
   - Interrupciones: ¿contrato claro VBL + `engine_frame_tick`?
   - Memoria: ¿flags CHIP/Fast, trazas de fugas?
3. Salida: issue o sección **Gaps** en `doc/engine-roadmap.md` / `engine-feature-phases.md` enlazada desde el mapa AMC.

**Criterio de cierre:** documento `doc/reference/amc-wrobel/engine-gap-analysis.md` con tabla *tema → estado (OK / parcial / falta) → propuesta*.

---

## Fase 5 — Depuración, WinUAE y tests automáticos (cruce explícito)

El curso incluye material sobre **depurador** (p. ej. entrada *Letter I - Debugger* en el índice). En este repo la realidad operativa está repartida:

| Capacidad | Hoy (resumen) | Beneficio para tests automáticos | Mejora posible |
|-----------|---------------|-----------------------------------|----------------|
| Breakpoints en C, variables, stack | DAP + extensión Amiga ([`debug-with-ai.md`](debug-with-ai.md)) | Poco en CI desatendido; útil para depurar fallos de test manualmente | Mantener `launch.json` alineado con binarios batería |
| GDB RSP sobre WinUAE | **MCP `winuae-emu`** (repo `mcp-winuae-emu`) | `winuae_breakpoint_*`, `memory_read`, `custom_registers`, `screenshot`, `bitmap_decode` | Documentar en `amiga-test-battery-spec.md` el flujo “test falla → MCP lee `g_battery_evidence_log`” |
| Perfil y capturas `.amigaprofile` | Extensión Amiga; la IA **analiza** el fichero | Correlación frame-a-frame con [`evidence-sequence-and-profiling.md`](evidence-sequence-and-profiling.md) | Automatizar export si la extensión/API lo permite en el futuro |
| Traza ratón / input | WinUAE-DBG (`-winmouselog`, ver `doc/DEBUGGING-STRATEGY.md` en WinUAE-DBG) | Validar casos UI y batería con input simulado + log | Exponer lectura de log vía MCP *(plan)* o script post-run |
| Traza interna MCP | `WINUAE_TRACE=1`, logs en `%TEMP%\winuae-mcp\` | Depurar conexión GDB y órdenes | Incluir en pipeline de batería un paso “adjuntar mcp-trace si falla timeout” |

**Acciones concretas del roadmap:**

1. Añadir subsección en [`mcp-live-coding-workflow.md`](mcp-live-coding-workflow.md) o [`debug-with-ai.md`](debug-with-ai.md): *“Relación con el depurador descrito en el curso AMC (FS-UAE/AmiDB): nuestro equivalente es WinUAE+GDB+MCP”*.
2. En [`engine-unified-test-roadmap.md`](engine-unified-test-roadmap.md), referencia cruzada: fallo automático = **(1)** leer memoria evidencia **(2)** screenshot **(3)** opcional breakpoint en símbolo de etapa.
3. **WinUAE-DBG / fork:** evaluar si hace falta nuevo comando monitor para volcar estado del **automation buffer** del engine sin GDB symbols *(tarea técnica en repo WinUAE-DBG o mcp-winuae-emu)*.
4. **MCP:** lista de deseos versionada en `mcp-winuae-emu` (p. ej. “leer `g_battery_evidence_log` por nombre de símbolo desde ELF”, “watchpoint en chip address range”, “export copper list a fichero desde sesión”).

**Criterio de cierre:** una página `doc/reference/amc-wrobel/debugging-crosswalk.md` que enlace curso → doc local → herramienta concreta.

---

## Fase 6 — Mantenimiento

- Revisar el índice del curso **anualmente** o si cambia `markwrobel.dk`.
- Al añadir API nueva al engine: actualizar `topic-map.md` y, si aplica, una línea en notas AMC.

---

## Orden de ejecución recomendado

1. Fase 1 (inventario TSV)  
2. Fase 2 (topic map)  
3. Fase 5 (crosswalk depuración — desbloquea mejoras de test ya existentes)  
4. Fase 4 (gap engine)  
5. Fase 3 (comentarios y notas aditivas) en paralelo incremental  

---

## Enlaces útiles en este repo

- [engine-docs-index.md](engine-docs-index.md)
- [debug-with-ai.md](debug-with-ai.md)
- [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md)
- [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md)
- [amiga-test-battery-spec.md](amiga-test-battery-spec.md)
