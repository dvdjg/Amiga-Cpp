# Plan de implementación del engine (pasos ejecutables)

Plan operativo para llegar a un engine funcional según [engine-roadmap.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-roadmap.md). Cada etapa tiene **criterio de éxito** y **test reproducible**.

## Parent Links

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Índice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Arquitectura del engine](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-architecture.md)
- [Roadmap del engine](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-roadmap.md)

## Regla de convergencia

La batería y la app pueden explorar técnicas antes de que exista una API estable, pero el destino deseado es siempre el mismo:

1. una técnica se demuestra en un caso controlado;
2. se documenta su procedimiento y su evidencia;
3. la parte reusable se extrae a `engine/`;
4. el caso pasa a consumir esa API del engine.

Una fase no se considera madura si seguimos acumulando inicialización de hardware duplicada en tests o efectos y no consolidamos capacidades comunes.

## Tests reproducibles

| Test | Comando | Éxito |
|------|---------|--------|
| Build | `bash scripts/verify-build.sh` | Exit 0, genera `out/a.elf` y `out/a.exe` |
| Clean + build | `make clean` y luego `bash scripts/verify-build.sh` | Exit 0 |
| Battery case | `node scripts/run-battery-case.mjs --case <ruta>` | Build/evidencia según `case.json` |

La verificación visual en WinUAE puede apoyarse en capturas, MCP, análisis LM Studio y postmortem, pero sigue habiendo casos donde hace falta validación manual o sesiones visibles del emulador.

## Fase 0: Baseline

**Estado**: demo principal funcional y engine básico.

**Test**:

1. `bash scripts/verify-build.sh`
2. Ejecutar en WinUAE
3. Verificar display, audio e interacción

## Fase 1: Estructura `app/` y efecto 1

**Objetivo**: mover la demo actual a `app/effects/` y mantener comportamiento equivalente.

**Estado**: implementada.

## Fase 2: Menú principal y navegación

**Objetivo**: menú visible al arrancar y transición menú ↔ efecto.

**Estado**: implementada.

## Fase 3: Carga y descarga limpia de efectos

**Objetivo**: contrato `create/loop/destroy` estable y sin restos de estado.

## Fase 4: Efecto placeholder

**Objetivo**: validar arquitectura multi-efecto con un segundo caso simple.

## Fase 5: APIs de display/copper reutilizables

**Objetivo**: mover lógica reusable de display, copper y setup de pantalla desde efectos/tests hacia `engine/`.

**Avance actual**:

- `T01` ya consume un helper reusable del engine en [engine/src/display.c](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/src/display.c) para construir una copper list planar lores interleaved con ventana y paleta básicas.
- Esto no cierra todavía la fase, pero sí materializa la política “test → engine reusable”.

**Siguiente criterio fuerte**:

1. más de un caso o efecto usando esos helpers;
2. menos acceso directo a `custom`/`offsetof` en código de alto nivel;
3. documentación pública de la API resultante.

## Fase 6: Efectos adicionales

**Objetivo**: integrar nuevos efectos sin romper arquitectura ni duplicar capacidades generales.

**Regla**: si un efecto necesita una técnica generalizable, debe documentarse y promoverse a `engine/` cuando se estabilice.

## Fase 7: Automatización de tests

**Objetivo**: pipeline reproducible con build, run, captura, análisis visual y depuración asistida por MCP/WinUAE/Coppenheimer cuando sea posible.

## Fase 8: Preparación para juego

**Objetivo**: reutilizar la misma infraestructura de estados para fases reales de juego.

## Subsistemas de gameplay aún no integrados (lotes BOB, tilemap, copper dinámico, audio modular)

Quedan fuera del núcleo mínimo hasta que el disparador del [engine-roadmap.md](engine-roadmap.md) (sección “Criterio para portar subsistemas grandes”) se cumpla: al menos dos consumidores o un bloqueo real de producto. La aritmética fija y utilidades de [`engine_suite.h`](../engine/include/engine_suite.h) ya forman parte de la Fase 1 en [engine-feature-phases.md](engine-feature-phases.md).

## Resumen operativo

| Fase | Objetivo | Señal de cierre |
|------|----------|-----------------|
| 0 | Baseline | Build + run correctos |
| 1 | `app/` + efecto 1 | Comportamiento equivalente |
| 2 | Menú | Navegación estable |
| 3 | Lifecycle | Entrar/salir sin residuos |
| 4 | Placeholder | Multi-efecto validado |
| 5 | APIs reusable display/copper | Casos migrados al engine |
| 6 | Más efectos | Integración sin deuda duplicada |
| 7 | Automatización | Evidencia reproducible |
| 8 | Juego | Estados de juego sobre la misma base |
