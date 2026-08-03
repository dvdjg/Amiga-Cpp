# Motor de juegos Amiga (y retro) - Arquitectura

Motor en C para juegos y demos en Amiga 500 (OCS/ECS) y, en el futuro, otras plataformas retro. API de alto nivel, abstracciones reutilizables y acceso controlado al hardware. Objetivo: **soportar todo tipo de juegos que admita la máquina**, incluidos **efectos visuales avanzados, audio y 3D básico con aceleración de polígonos por blitter**.

## Parent Links

- [README principal](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/README.md)
- [Índice de documentación](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-docs-index.md)
- [Roadmap del engine](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/engine-roadmap.md)
- [Battery spec](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/amiga-test-battery-spec.md)

## Visión y alcance

- **Plataforma principal**: Amiga 500 (68000, OCS/ECS); código usable en AGA donde tenga sentido.
- **Tipos de juego**: desde demos y shoot'em up hasta plataformas, RPG, beat'em up y **gráficos 3D básicos** (polígonos rellenos/acelerados por blitter).
- **Multiplataforma retro (futuro)**: diseño que permita ports a otras máquinas retro mediante capa de abstracción y targets de compilación.
- **Filosofía**: no es un "game maker"; la lógica de juego y gran parte del diseño corren por tu cuenta. El motor ofrece HAL del hardware, primitivas y helpers reutilizables para que puedas optimizar y recortar lo que no uses.
- **Dirección de evolución**: las pruebas de `tests/amiga-battery/` sirven para demostrar que una técnica hardware es viable, documentarla y convertirla después en funcionalidad reusable de `engine/`. La batería no compite con el engine: le alimenta.

## Estructura del proyecto

```text
Cursor-Amiga-C/
├── engine/                 # Biblioteca reusable del engine
│   ├── include/            # API pública
│   └── src/                # Implementación de bajo nivel y servicios comunes
├── support/                # Soporte runtime / toolchain Amiga
├── app/                    # Aplicación principal e integración de estados
├── tests/amiga-battery/    # Laboratorio de técnicas hardware y harness de pruebas
├── scripts/                # Build, ADF, captura, verificación, LM Studio
├── doc/                    # Arquitectura, roadmaps, workflow y referencias
├── gfx/                    # Assets y utilidades gráficas
├── out/                    # Artefactos generados localmente
└── Makefile
```

### Papel de cada área

- **`engine/`**: aquí deben vivir las capacidades estables y reutilizables del proyecto.
- **`app/`**: integración de alto nivel, demo principal, menú, estados y casos de uso reales del engine.
- **`tests/amiga-battery/`**: casos pequeños y controlados para validar una técnica hardware concreta, recopilar evidencia y decidir si merece entrar en el engine.
- **`scripts/`**: automatización de build, carga, evidencia, postmortem y análisis visual.
- **`doc/`**: fuente de verdad del proceso, el roadmap y el objetivo del motor.

## Evolución arquitectónica prevista

La estructura actual es válida, pero el destino deseado del engine es más explícito que el árbol actual. Conforme las técnicas se estabilicen, `engine/` debería tender a módulos reutilizables y bien delimitados:

- **`engine/system`**: control del sistema, DMA, interrupciones, timing y lifecycle.
- **`engine/video`**: display, copper, bitplanes, blitter, sprites y helpers de playfield.
- **`engine/audio`**: reproducción de samples, música y utilidades de sincronía audiovisual.
- **`engine/input`**: ratón, teclado, joystick y automatización de entrada para test/debug.
- **`engine/memory`**: allocators, pools y utilidades Chip/Fast RAM.
- **`engine/debug`**: overlays, instrumentación, telemetría y postmortem.

No hace falta reestructurarlo todo hoy. La regla práctica es: primero se prueba una técnica en la batería; cuando se repite o demuestra valor general, se promueve a un módulo reusable del engine.

## Conceptos de organización del motor

Ideas habituales en motores retro para mantener el código escalable sin inflar el núcleo.

### Low-level parametrico vs capa retained

Muchas capacidades del engine no deberian fijarse como una sola API "universal". En Amiga, el contexto real cambia la implementacion correcta:

- numero de bitplanes
- si hay mascara o no
- ownership del buffer destino
- clipping
- scroll o viewport
- sprite hardware frente a sprite CPU o BOB blitter

La direccion recomendada es separar:

- primitivas low-level, explicitas y parametricas
- helpers retained o scene-level que simplifican la gestion normal

Ejemplo: una rutina de sprite CPU o blit por CPU no deberia ocultar si el caso es `1bpl`, `2bpl` o `4bpl` si eso cambia el hot path y el coste real. La capa retained puede exponer una interfaz mas comoda, pero debe apoyarse sobre primitivas que mantengan posible la especializacion.

Nota de diseno y evaluacion C vs C++: [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md).

### Utils vs Managers

- **Utils**: recursos que se crean y destruyen muchas veces, como bitmaps, fuentes o viewports. La API suele tener forma `thingCreate(...)` / `thingDestroy(...)`.
- **Managers**: servicios globales por programa, como blitter, copper, audio o input. Se inicializan una vez y luego se consumen vía funciones del manager.

### View y Viewports

- **View**: contenedor de todo lo que se muestra; solo una activa a la vez.
- **Viewports**: ventanas desplazables dentro de la view, con bitmap, resolución y paleta propios.
- **Buffer managers**: modelos de backing store para scroll, tilemaps o playfields grandes.

### Máquina de estados del juego

- Estados con **creación**, **bucle** y **destrucción**.
- `stateChange` para cambios completos.
- `statePush` / `statePop` para menús de pausa o overlays.
- Posible `generic main` para reducir boilerplate.

### Build Debug vs Release

- **Debug**: logging, checks defensivos, breakpoints fiables y herramientas de inspección.
- **Release**: sin sobrecarga innecesaria y con rutas críticas optimizadas.

### Uso del sistema operativo

Para máximo rendimiento, el juego toma el sistema (`TakeSystem`) y evita usar el OS en el bucle principal. Las operaciones que requieren OS deberían concentrarse en fases de creación/destrucción o encapsularse con una API de engine controlada.

## Relación entre batería y engine

La batería y el engine no tienen el mismo papel, pero sí un flujo de trabajo común:

1. **Exploración**: un caso de batería demuestra una técnica concreta en un contexto pequeño y medible.
2. **Documentación**: el caso deja README, evidencia, postmortem si hace falta y análisis visual.
3. **Extracción**: cuando la técnica es estable, la implementación reusable se mueve a `engine/`.
4. **Consumo**: el propio caso de batería pasa a usar la nueva API del engine en vez de mantener lógica duplicada.

Este flujo es especialmente importante para gráficos, audio e input, donde es fácil acumular soluciones ad hoc si no se promueven pronto las partes compartibles.

## API actual del engine (`engine.h`)

### Sistema

- `engine_init()` / `engine_shutdown()`
- `TakeSystem()` / `FreeSystem()`
- `engine_wait_vbl()` / `engine_wait_line(line)`

### Display / hardware

- `engine_ack_vbl()`
- `engine_set_copper1(list)` / `engine_set_copper2(list)`
- `engine_start_copper()`
- `engine_set_dma(mask)` / `engine_set_intena(mask)` / `engine_clear_intreq(mask)`
- `engine_display_blank()`
- `engine_copper_screen_window(...)` / `engine_copper_bitplane_config(...)`
- `engine_copper_set_interleaved_planes(...)` / `engine_copper_set_palette(...)`

### Blitter

- `engine_blit_wait()` / `engine_blit_is_idle()` (lectura previa de `DMACONR`, A1000 / HRM)
- `engine_blit_copy_rect(...)` (A→D interleaved, minterm p. ej. `ENGINE_BLIT_MINTERM_COPY`)
- `engine_blit_clear(...)`
- `engine_blit_bob(...)` / `engine_blit_cookie_cut(...)` / `engine_blit_line(...)`

### Input

- `engine_mouse_*`, `engine_key_get()`, `engine_key_held(raw)`
- Flancos y rectángulo: `engine_input_edges.h` (incluido vía [engine_suite.h](engine/include/engine_suite.h))

### Interrupciones y tiempo

- `engine_set_interrupt_handler(handler)`
- `engine_frame_tick()` / `engine_get_frame_counter()`
- `engine_clock_reset()` / `engine_clock_elapsed_frames()` ([engine_clock.h](engine/include/engine_clock.h))

### Memoria

- `engine_alloc(size, flags)` / `engine_free(ptr, size)`; con `make ENGINE_MEM_TRACE=1`, macros con línea/archivo y `engine_mem_trace_report()`
- `engine_mem_trace_report()` (no-op sin traza)

### Diagnóstico opcional

- `make ENGINE_DIAG=1`: `ENGINE_LOG` / `ENGINE_ASSERT` en [engine_diag.h](engine/include/engine_diag.h) (KPrintF)
- Bloques y promedios: [engine_trace.h](engine/include/engine_trace.h) (`ENGINE_TRACE_*`)

### Suite ampliada (`engine_suite.h`)

Incluye: [engine_bitmap.h](engine/include/engine_bitmap.h), [engine_rand.h](engine/include/engine_rand.h), [engine_fixmath.h](engine/include/engine_fixmath.h), [engine_tag.h](engine/include/engine_tag.h), además de clock, edges y trace. Plan de evolución: [engine-feature-phases.md](engine-feature-phases.md).

### Extensiones opcionales (`engine_extensions.h`)

Copper en doble buffer, sprites hardware, joystick, viewport/tilemap lógicos, peek/poke custom, fuente 8×8 mínima, lectura ficheros/directorio DOS y capa de audio (backends enlazables). Detalle y límites: [engine-subsystems.md](engine-subsystems.md). Cobertura por tests: [engine-test-battery-matrix.md](engine-test-battery-matrix.md).

### Debug (WinUAE)

- `engine_debug_overlay_clear()`
- `engine_debug_rect()`
- `engine_debug_filled_rect()`
- `engine_debug_text()`

## Capacidades objetivo que deben emerger desde la batería

Algunas áreas aún están verdes y es preferible madurarlas primero en `tests/amiga-battery/` antes de fijar API pública:

- **Video avanzado**: copper helpers, sprites, doble buffer y validación de bitplanes.
- **Audio**: samples, música, temporización y sincronía con VBL/CIA.
- **Input**: teclado, joystick, ratón y automatización de entrada para test.
- **Debug/postmortem**: volcados de registros, estado del sistema, captura visual y crash records.
- **Técnicas especiales**: blitter line/fill, rotozoom, starfields, tile streaming y 3D básico.

## Interioridades ocultas

El código de aplicación **no debe** incluir `engine_internal.h` ni acceder a `custom`, `SysBase` o `GfxBase` directamente salvo excepciones muy justificadas y documentadas. El motor encapsula el hardware; las extensiones avanzadas se exponen vía API estable.

## Regla de respaldo técnico

Toda API del engine o test que toque hardware/kernel debe estar respaldada por fuente técnica objetiva (AHRM, RKM/NDK, autodocs). Referencia operativa:

- [Fuentes técnicas objetivas](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/doc/reference/amiga-authoritative-sources.md)

## Añadir un nuevo juego, demo o técnica

Guía operativa y checklist: [engine-new-project-guide.md](engine-new-project-guide.md).

1. Crear el caso o módulo en `app/` o `tests/amiga-battery/`, según sea integración real o experimento controlado.
2. Incluir `engine.h` y el soporte necesario.
3. Añadir build y documentación mínima enlazada con sus padres.
4. Si la técnica demuestra valor general, extraer la parte reusable a `engine/`.

## Estructura de documentación

- **Instalación y build**: compilador, dependencias y flujos de verificación.
- **Tutorial**: pasos ordenados para las capacidades principales.
- **Referencia**: API pública y contratos de uso.
- **Batería hardware**: casos, harness, evidencia, LM Studio y postmortem.
- **Herramientas**: conversión de assets y utilidades de soporte.
- **Contribución**: estilo de código y proceso de cambios.

## Referencias

- [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug)
- `doc/amigaprofile-format.md`
- `doc/debug-with-ai.md`
