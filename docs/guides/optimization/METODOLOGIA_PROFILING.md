# Metodología de profiling y optimización de demos

Documento de referencia para **cualquier sesión que persiga rendimiento** en las demos del engine. Describe el pipeline de medida, las herramientas, cómo leer los resultados, las trampas conocidas y las optimizaciones ya identificadas con su evidencia. No narra sesiones pasadas: describe el procedimiento vigente y los resultados de referencia medidos.

```
   build            run                medida               lectura                 decision
┌──────────┐   ┌───────────┐   ┌──────────────────┐   ┌────────────────────┐   ┌──────────────┐
│ build-   │──▶│ run-demo  │──▶│ measure-fps.mjs  │──▶│ ¿faltan fps?       │──▶│ ¿CPU o bus?  │
│ demo.sh  │   │ --config  │   │ (campos/frame)   │   │ (141.876 ciclos)   │   └──────┬───────┘
└──────────┘   └───────────┘   └──────────────────┘   └────────────────────┘          │
                                │                                                      │
                                ├──▶ profile.mjs  (secciones ENG_PROF_* del engine) ──┤ CPU
                                └──▶ winuae-profile.mjs + profile-samples.mjs ────────┤ bus
                                        (ciclos ocupada/libre, DMA por tipo,           │
                                         top de rutinas por muestras de CPU)           │
                                                                                       ▼
                                                                         cambio → re-medir (mismo camino)
```

## 1. Qué mide cada herramienta

| Herramienta | Comando | Qué devuelve | Cuándo usarla |
|---|---|---|---|
| `tools/debug/measure-fps.mjs` | `node tools/debug/measure-fps.mjs <demo> [CONFIG]` | Campos por frame (y fps) del binario publicado | Primera medida y tras cada cambio |
| `tools/debug/profile.mjs` | `node tools/debug/profile.mjs <demo> [CONFIG]` | Secciones instrumentadas con `ENG_PROF_*`: ciclos por sección y por elemento | Localizar la fase culpable dentro del frame |
| `tools/debug/winuae-profile.mjs` | `node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]` | Captura binaria del profiler de WinUAE + ciclos de CPU ocupada/libre y DMA por tipo | Saber si el frame lo consume la CPU o lo espera el bus |
| `tools/analyze/profile-samples.mjs` | `node tools/analyze/profile-samples.mjs <perfil.bin> <demo> [CONFIG] [--top N] [--json]` | Top de rutinas por **muestras de CPU**, resolviendo los PCs con el `.map` | Saber **qué función** se lleva el tiempo |
| `tools/analyze/profile-report.mjs` | `node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N] [--json]` | Top de rutinas/archivos de un `.amigaprofile` JSON exportado desde VSCode | Analizar un perfil tomado con el depurador del plugin |
| MCP (`winuae_profile`, `machine-snapshot`, `trace`, `conditional-breakpoint`, `memory-pattern-search`, `postmortem`) | vía el servidor MCP | Estado de CPU/chipset, memoria, DMA por scanline, recursos de Blitter, screenshot | Cuando hace falta ver hardware, no tiempo |
| `winuae_profile_ollama` (MCP) | vía MCP | Resumen del perfil pasado a un modelo local | Resumen en lenguaje natural |

`measure-fps.mjs` y los perfiles se ejecutan sobre lo ya publicado por `tools/run/run-demo.sh`, que acepta `--config <id>` **como flag** (no posicional), `--warp`, `--sequence-frames N`, `--sequence-interval-ms M`, `--keep-running`.

## 2. Instrumentación por secciones (`engine/include/eng/debug/prof.hpp`)

El engine expone `ENG_PROF_INIT/FRAME/BEGIN/END` y un bloque `inline volatile` de contadores; **fuera de `__m68k__` las macros son no-ops**, de modo que el código se instrumenta una sola vez y compila igual en host. Las secciones del engine viven en `copper/plan.hpp` (`prof_sort_lines=7`, `prof_sort_prio=8`, `prof_emit=9`).

Reglas:

- Instrumentar **pocas secciones y gruesas**: cada marca lee el contador de ciclos del depurador y ese coste entra en la medida (se ha medido hasta un 14 % del frame en `prof_clock()` dentro de un perfil con muchas marcas).
- Una sección **nunca** debe rodear trabajo de otro hilo de medida (nada de marcas dentro de bucles de calibración).
- El coste por elemento se obtiene dividiendo los ciclos de la sección entre el número de elementos que procesa (intenciones de copper, BOBs, arranques de Blitter). Es la cifra que revela si el problema es el algoritmo o la falta de inline.
- Añadir una sección nueva al `SECTION_NAMES` de `tools/debug/profile.mjs` para que el informe la nombre.

## 3. Cómo leer los resultados

1. **CPU ocupada vs libre** (`winuae-profile.mjs`). Libre ≈ 0 % significa que el frame no espera: el coste es trabajo real y hay que reducir instrucciones, no reordenar esperas. Libre alto con fps bajos apunta a espera de Blitter/Copper o a DMA que roba bus.
2. **Secciones** (`profile.mjs`). Ordenar por ciclos y dividir por elemento. Un `emit` de cientos de instrucciones por intención señala bucles con llamadas no inlinadas; un `blits` de cientos por BOB señala lanzamientos de Blitter mal agrupados.
3. **Top de rutinas** (`profile-samples.mjs`). Las muestras de CPU dan el reparto real por función. Si el top coincide con las secciones, la medida es coherente; si no, las secciones están mal colocadas o la instrumentación domina la medida.
4. **DMA por tipo** (`winuae-profile.mjs`). Reparte el bus: bitplane, copper, blitter y sprite. Un bitplane altísimo indica ancho de banda de pantalla, no CPU.

## 4. Resultados de referencia (demo 086, config debug, camino dinámico)

Frame PAL = 141.876 ciclos. Calibración medida: bucle de 1000 iteraciones del contador del depurador ≈ 18.250 ciclos/frame ⇒ ≈4,5 ciclos por instrucción ⇒ la CPU corre a la velocidad esperada y las medidas por sección son trabajo real, no artefacto de temporización.

Medida de fps: **8,1-9,3 campos** con 8 BOBs + 256 intenciones de copper (objetivo: 1 campo). Reparto por secciones:

| Sección | Ciclos | Por elemento | Lectura |
|---|---|---|---|
| `copper`/`build_frame` | ~869.000 (73-84 %) | — | Fase dominante |
| `emit` | ~460.000 | ~1.597 c/intención (≈355 instr) | Escribir WAIT+MOVE por intención con llamadas no inlinadas |
| `sky` (construir + `add`) | ~162.000 | ~634 c/intención (≈140 instr) | Reconstruye cada frame el cielo idéntico |
| `sort_lines` | ~98.000 | — | Counting sort por scanline |
| `static` | ~48.000 | — | Camino estático de diagnóstico |
| `sort_prio` | ~44.000 | — | Orden por prioridad dentro de línea |
| `actors` | ~135.000 | ~16.800 c/BOB (≈3.700 instr) | Ruta del actor por objeto |
| `blits` | ~113.000 | ~14.000 c/BOB (≈500 instr/arranque) | Arranques de Blitter |
| `calib` | ~18.000 | — | Bucle de calibración |

### Reparto real por rutinas (perfil de CPU de Chrome DevTools, un frame)

El `.amigaprofile` del plugin es un **CPU profile de Chrome DevTools**: un único frame agregado con `nodes` (jerarquía + `callFrame.functionName`), `samples` (índice de nodo por muestra), `timeDeltas` (µs entre muestras) y `$amiga` (registros custom). `tools/analyze/profile-report.mjs` agrega el **tiempo propio** por rutina y archivo. Un perfil de la 086 (12.391 muestras, 1.007.999 ciclos ≈ 7 campos) da:

```
  35,1%  354.306 ciclos  eng::copper::Plan::add_prioritized   @ copper/plan.hpp
  30,3%  305.331 ciclos  eng::copper::Plan::sort_by_top       @ copper/plan.hpp
  20,1%  202.840 ciclos  BobObjectsDemo::build_frame          @ src/main.cpp
   6,6%   66.072 ciclos  eng::scene::actor_add_copper         @ scene/actor.hpp
   4,3%   43.343 ciclos  eng::copper::Plan::raster_key        @ copper/plan.hpp
   0,8%    7.647 ciclos  eng::scene::actor_screen_rect
   0,8%    7.633 ciclos  eng::scene::actor_current_frame
   0,7%    7.463 ciclos  eng::scene::ActorStore::get
   0,7%    7.136 ciclos  eng::scene::ActorStore::valid_id
  --- por archivo ---
  69,8% copper/plan.hpp | 20,1% src/main.cpp | 9,5% scene/actor.hpp | 0,4% core/span.hpp
```

Conclusiones que este reparto impone:

- **El cuello es el algoritmo del `Plan`, no la emisión**: `add_prioritized` (35 %) + `sort_by_top` (30 %) + `raster_key` (4 %) suman **≈70 %** del frame. La emisión de la copperlist (`emit`/`materialize`, instrumentada por secciones) es marginal (<0,2 %): las secciones `ENG_PROF_*` atribuían mal el coste porque el perfilador `prof_clock()` es `(inlined)` y su tiempo se contaba contra el bloque que lo rodea.
- **256 intenciones de cielo + 8 BOBs cuestan 660.000 ciclos solo en ordenarlas-insertarlas** ⇒ el `Plan` necesita insertar por línea sin recorrer/sortear toda la colección por intención (bucket por scanline con `add` en O(1) y emisión secuencial).
- La ruta de actor completa (`actor_add_copper`, `actor_screen_rect`, `actor_current_frame`, `ActorStore::get/valid_id`) es **≈9,5 %**: relevante pero secundaria frente al `Plan`.
- `core/span.hpp` ya es marginal (0,4 %): el temor a los `Span`/range-for por elemento no se confirma.
- **El perfil tomado con el depurador** añade `[IRQ]` y el `prof_clock` instrumentado; en este perfil limpio `[IRQ]` = 0,0 % y `debug/prof.hpp` = 0,1 % ⇒ coherente con un run sin depurador.

Medida por secciones (para comparar): `copper`/`build_frame` ~869k (73-84 %), `emit` ~460k (~1.597 c/intención), `sky` 162k (~634 c/intención), `sort_lines` 98k, `sort_prio` 44k, `actors` 135k (~16.800 c/BOB), `blits` 113k (~14.000 c/BOB), `calib` 18k.

Anomalía abierta: la misma demo con `--release` mide **2,4× más lento** que en debug (2,48 fps / 20,2 campos frente a 6,14 / 8,1). Es un problema de generación de código, no de `-O1`: **no fiarse de medidas en release hasta resolverlo**.

## 5. Trampas conocidas (revisar antes de concluir)

- **El perfilador se mide a sí mismo**: cada marca lee el contador; con muchas marcas el coste entra en la cuenta (14 % medido). Instrumentar grueso y descontar.
- **Perfilar con el depurador enganchado** añade IRQs y el stub GDB (17 % en `[IRQ]`). Perfilar con `run-demo.sh` + MCP, no desde el depurador del plugin.
- **La atribución `(inlined)`** del perfil culpa al bloque inlined completo: `prof_clock() (inlined)` incluye el código que lo rodea.
- **Muestras gruesas**: un perfil de pocos frames da tendencia, no detalle. Acumular frames antes de decidir.
- **Las muestras necesitan el perfil nativo, no el canal lateral**: el comando `profile N "" bin` produce 0 muestras si no se le pasa la tabla `.unwind`, que se construye desde las CFI de DWARF (`objdump --dwarf=frames-interp`) escribiendo `{ (cfaReg<<12)|cfaOfs, r13, ra }` como 3 int16 por cada 2 bytes de `.text`; `tools/debug/winuae-profile.mjs` la construye (portada del `UnwindTable` del plugin).
- **El canal lateral no sirve para muestrear el PC**: el campo `pc` de `state` (puerto 2346) **no se refresca** entre consultas (devuelve el PC de cuando se entró en `observe`), así que un sampler por esa vía da siempre el mismo símbolo. Además el `baseText` (`0x00c0cb88` en la 086) solo aparece cuando el emulador arranca por la ruta que envía `qOffsets` (arranque "extension-style" con `default.uae`); con `-f <runner.uae>` puede quedar a `0`. Medido: `tools/profile/hotspots.mjs` (que muestrea `state.pc`) **no es fiable** para reparto por rutina; usar el perfil nativo + `profile-report.mjs`.
- **Con la CPU corriendo, el GDB no responde `g`** (`Register reply too short: 35 chars`); muestrear por pausa/lectura/reanudación es tan lento que produce ~1 muestra cada varios segundos. No es una vía práctica.
- **`parseProfile` del MCP descarta las muestras**: recorría el `profileArray` con `o += profileCount * 4` sin leerlo, así que `winuae_profile`/`winuae_profile_ollama` no podían dar rutinas. Corregido en el MCP (`ProfileFrame.profileArray` conserva los PCs y `profile-ollama` los resume por sección/PC caliente); para nombres de función hace falta además el `.map` (lo hace `tools/analyze/profile-samples.mjs`).
- **El `.amigaprofile` del plugin es un CPU profile de Chrome DevTools** (no una lista de frames): `firstFrame.nodes[]` + `samples[]` + `timeDeltas[]` + `$amiga`. Analizarlo con `tools/analyze/profile-report.mjs` (tiempo propio por rutina y archivo); `hitCount` solo como respaldo si faltan `samples`.
- **Shell Windows**: PowerShell 5.1 rompe `&&`, `(`, `$`, comillas y regex con backslashes; usar ficheros (`out/tmp/*.py`, `git commit -F out/tmp/commit-msg.txt`) y comandos simples, o `bash -lc` con comillas simples. `Get-Content`/`Set-Content` de PowerShell re-encoda y genera mojibake: usar las herramientas del agente o `sed -i`.
- **Configs de run**: `--config` es flag (`run-demo.sh demo --config A500_release`), no argumento posicional.

## 6. Optimizaciones identificadas (priorizadas, con evidencia)

El perfil de CPU (un frame, 12.391 muestras) sitúa **≈70 % del frame en `copper/plan.hpp`**, y dentro de él el peso está en el **algoritmo de ordenación/inserción**, no en la emisión:

1. **Insertar por scanline en O(1)** (`add_prioritized` 35,1 % + `raster_key` 4,3 %). Con 256 intenciones de cielo + 8 BOBs, resolver la línea destino y anexar a un bucket por línea debe ser cálculo directo, sin búsqueda ni recorrido de la colección por intención.
2. **Eliminar o abaratar `sort_by_top`** (30,3 %). Si las líneas ya quedan ordenadas por el bucket de (1), la fase de ordenación desaparece; si hace falta estabilidad por prioridad, ordenar solo el bucket de cada línea (y solo si tiene más de una intención).
3. **Reutilizar la escena estática entre frames** (`build_frame` 20,1 %): el cielo de 256 líneas es idéntico cada frame y hoy se reconstruye entero.
4. **Aplanar la ruta de actor** (9,5 % en total: `actor_add_copper` 6,6 %, `actor_screen_rect`, `actor_current_frame`, `ActorStore::get/valid_id`): una pasada con punteros directos en lugar de `ActorStore::get` + validación por actor.
5. **Instrumentación**: el perfil limpio muestra `debug/prof.hpp` en 0,1 % y `[IRQ]` en 0,0 %, pero con el perfilador cargado de secciones su coste se vuelve visible y **su atribución `(inlined)` engaña** (§5). Reducir a 2-3 secciones o medir por muestreo.
6. **Resolver el 2,4× de release** antes de fiarse de cualquier optimización en release.

No es prioridad, con la evidencia actual: `emit`/`materialize` (<0,2 %) y los `Span` (`core/span.hpp` 0,4 %).

## 7. Referencias

- Reglas de optimización y port a asm: §12 de `OPTIMIZACION_GPP_68000.md`.
- Perfilador y secciones: `engine/include/eng/debug/prof.hpp`, `engine/include/eng/graphics/copper/plan.hpp`.
- Ciclos y temporización de pantalla: `docs/reference/ahrm/` y `docs/engine/architecture/DISPLAY_COMPOSITION.md`.
- Plan de copper y buffers: §6 de `docs/engine/architecture/DISPLAY_COMPOSITION.md`.
