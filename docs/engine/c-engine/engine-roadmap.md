# Roadmap: Motor de juegos estable con menú de demos

Documento maestro para alcanzar un engine estable y verificable, con una aplicación de menú de demos como banco de integración. Las fases aquí descritas cubren **engine + app**. El seguimiento detallado de herramientas MCP, batería de hardware y pruebas IA vive en [amiga-implementation-roadmap.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md) y [amiga-test-battery-spec.md](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md).

## Parent Links

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Índice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Arquitectura del engine](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-architecture.md)

## 1. Principios fundamentales

### 1.1 Verificación en cada fase

- Cualquier cambio importante debe verificarse: compilar, ejecutar en emulador y comprobar que el programa sigue funcionando.
- Cada fase define criterios de éxito concretos.
- La automatización debe crecer de forma incremental: build, run, captura, análisis visual y postmortem cuando sea posible.

### 1.2 Separación Engine vs Aplicación

- **Engine** (`engine/`): biblioteca reutilizable, sin lógica de menú ni de juego.
- **App** (`app/`): aplicación que usa el engine, con menú principal, navegación, estados y efectos.
- **Mismo menú para juegos**: la infraestructura de estados debe servir tanto para demos como para juego real.

### 1.3 Batería de pruebas como feeder del engine

- **Batería** (`tests/amiga-battery/`): entorno controlado para probar una técnica hardware concreta, capturar evidencia y decidir si debe entrar al engine.
- **Regla de promoción**: si una técnica se repite o demuestra valor general, su implementación reusable debe extraerse a `engine/`.
- **Regla de convergencia**: un test maduro no debería mantener código duplicado indefinidamente; lo ideal es que termine usando la API reusable del engine.
- **Criterio de calidad**: el roadmap no está sano si añadimos pruebas y efectos nuevos pero no consolidamos capacidades compartidas.
- **Regla de refactor**: una API ya promovida al engine sigue siendo refactorizable. Si aparecen consumidores nuevos o una forma más reusable, debe evolucionar apoyándose en la batería.

Ver política detallada en:

- [engine-api-maturity-and-refactor-policy.md](engine-api-maturity-and-refactor-policy.md)

### 1.4 Aplicación de menú de demos

- Menú principal visible al arrancar.
- Selección de efecto por ratón.
- Ambos botones del ratón para volver al menú desde cualquier efecto.
- Primera opción del menú: demo actual, usada como referencia de regresión.

## 2. Estructura objetivo del proyecto

```text
Cursor-Amiga-C/
├── engine/
├── support/
├── app/
│   ├── main.c
│   ├── menu.c
│   ├── state.c
│   └── effects/
├── tests/amiga-battery/
├── doc/
├── scripts/
└── Makefile
```

**Criterio de diseño**: separación clara entre engine y app. El menú valida el engine, pero no forma parte del engine.

**Papel de la batería**: `tests/amiga-battery/` no sustituye esta estructura. Es el laboratorio donde se exploran capacidades concretas de OCS/ECS, se recopila evidencia y se decide qué debe subir a `engine/` como API reusable.

## 3. Fases del roadmap

### Fase 0: Estado actual

**Estado**: demo funcional y engine básico.

**Regla de diseÃ±o**: al promover capacidad reusable, separar cuando haga falta:

- primitivas low-level parametricas
- wrappers retained o scene-level

No forzar una sola API rigida si el contexto real cambia la implementacion, por ejemplo numero de bitplanes, mascara, clipping, ownership del buffer o clase de sprite.

Objetivo explícito de arquitectura:

- capa low-level para control fino y especialización por contexto hardware
- capa high-level retained para gestión de escena (objetos, orden de dibujo, dirty rects, redraw/restore y anchoring)

**Test**:

1. Compilar.
2. Ejecutar en WinUAE.
3. Verificar display, audio, interacción y salida.

### Fase 1: Estructura `app/` y efecto 1

**Objetivo**: mover la demo actual a `app/effects/` y mantener comportamiento idéntico.

**Test**:

1. Build sin errores.
2. Ejecución correcta en emulador.
3. Misma salida visual y funcional que el baseline.

### Fase 2: Menú principal y navegación por ratón

**Objetivo**: menú visible y transición menú ↔ efecto.

**Test**:

1. Menú visible.
2. Entrada al efecto desde clic.
3. Vuelta al menú con ambos botones.

### Fase 3: Carga y descarga limpia de efectos

**Objetivo**: `create/loop/destroy` bien definidos y sin fugas.

**Test**:

1. Entrar/salir repetidamente.
2. Mantener estabilidad.
3. Validar que no quedan restos de estado.

### Fase 4: Efecto placeholder

**Objetivo**: validar arquitectura multi-efecto.

**Test**:

1. Menú con al menos dos opciones.
2. Alternancia repetida entre efectos sin errores.

### Fase 5: Expansión del engine

**Objetivo**: mover lógica reusable de copper/screen al engine y reducir acceso directo a hardware desde efectos.

**Test**:

1. Compilar y ejecutar.
2. Efecto 1 idéntico visualmente al estado anterior.
3. Uso prioritario de API engine en lugar de acceso directo a `custom`.

**Relación con la batería**: antes o durante esta fase pueden aparecer casos de batería que prueben copper, sprites, bitplanes o blitter de forma aislada. El criterio correcto es usar esas pruebas para afinar la API y luego hacer que la app consuma la implementación reusable resultante.

### Fase 6: Efectos adicionales

**Objetivo**: integrar nuevos efectos sin romper arquitectura ni duplicar capacidades generales.

**Regla de disciplina**: si un efecto nuevo necesita una técnica generalizable, no debería quedarse enterrada solo en `app/effects/<nombre>/`. Debe documentarse y, cuando se estabilice, promoverse a `engine/`.

**Captura de funcionalidad externa**: además de efectos propios, esta fase contempla destilar técnicas y librerías de repos de referencia (`demoscene-repo`, `ACE`, `Sevgi_Engine`, `amiga-stuff`) para incorporarlas como capacidades reusables del engine. La unidad de integración es la **capacidad** (API + contrato + batería), no el port literal de un efecto completo. Inventario operativo inicial: [engine-external-capability-ingestion.md](engine-external-capability-ingestion.md).

### Fase 7: Automatización de tests

**Objetivo**: automatizar build, run, captura, análisis visual y pruebas asistidas por MCP/WinUAE/Coppenheimer cuando sea viable.

**Batería alineada con el engine**: la matriz [engine-test-battery-matrix.md](engine-test-battery-matrix.md) define qué subsistema debe tener caso reproducible y evidencia (`evidence/`, capturas). Cada nueva API estable en `engine/` debería añadir o actualizar una fila y, cuando sea posible, un caso **(plan)** → implementado. El Makefile de batería enlaza `$(wildcard engine/src/*.c)` para que los casos usen el mismo árbol de fuentes que la app principal.

**Roadmap de pruebas del motor**: [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md) fija el contrato de trazas (`g_battery_evidence_log`, etapas), la huella visual para modelos de visión y el desglose por fases (micro, funcional, técnica de juego) para no explotar la combinatoria.

### Fase 8: Preparación para juego

**Objetivo**: reutilizar la misma infraestructura de estados y menú para fases reales de juego.

### Criterio para portar subsistemas grandes

No se integran de serie lotes de BOB con restauración de fondo, tilebuffer con cámara, viewport/scroll managers, copper de doble buffer dinámico ni reproductor tracker como parte obligatoria del núcleo. **Sí se evalúan** cuando se cumple al menos uno de:

1. La misma técnica aparece en **dos o más** casos de `tests/amiga-battery/` o efectos de `app/effects/` con lógica duplicada significativa.
2. Un objetivo de roadmap (p. ej. juego con scroll ancho o copper por línea dinámico) queda **bloqueado** sin esa pieza.
3. La API encaja con el contrato test → engine como módulo opcional claramente acotado.

Las ampliaciones ya disponibles como cabeceras y `.c` auxiliares se agrupan en [`engine_suite.h`](../engine/include/engine_suite.h); subsistemas opcionales mayores (copper doble buffer, sprites, joy, view, tilemap, DOS, audio, etc.) en [`engine_extensions.h`](../engine/include/engine_extensions.h) y [engine-subsystems.md](engine-subsystems.md). El plan por fases está en [engine-feature-phases.md](engine-feature-phases.md).

## 4. Verificación y herramientas

### 4.1 Por defecto

| Acción | Herramienta |
|--------|-------------|
| Compilar | `bash scripts/verify-build.sh` |
| Ejecutar | WinUAE / integración de depuración |
| Verificar display | Captura + análisis visual + validación humana cuando haga falta |
| Depurar | MCP, GDB, postmortem, LM Studio |

### 4.2 Plan B

Coppenheimer + Playwright pueden servir como complemento para inspección en navegador cuando el flujo lo permita.

## 5. Resumen de fases

| Fase | Objetivo | Test clave |
|------|----------|------------|
| 0 | Baseline actual | Build + run + display + audio + salida |
| 1 | Estructura `app/`, efecto 1 | Mismo comportamiento que baseline |
| 2 | Menú + navegación | Menú visible, entrar/salir efecto |
| 3 | Carga/descarga limpia | Repetición sin fugas ni fallos |
| 4 | Placeholder | Dos efectos alternables |
| 5 | APIs engine de display/copper | Menos acceso directo a hardware |
| 6 | Efectos adicionales | Nuevos efectos sin romper existentes |
| 7 | Automatización | Pipeline reproducible con evidencia |
| 8 | Preparación para juego | Estados de juego sobre la misma base |

## 6. Referencias

- [Arquitectura del engine](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-architecture.md)
- [Battery spec](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)
- [Roadmap de implementación Amiga](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-implementation-roadmap.md)
- [Battery README](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/tests/amiga-battery/README.md)
- `.cursor/rules/verify-build-and-display.mdc`
