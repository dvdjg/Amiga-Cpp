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

### Reparto real medido (secciones `ENG_PROF_*`, contador de ciclos del Amiga)

**Esta es la medida fiable.** Contador de ciclos del Amiga (`0xB7E928`), 37 frames, 086 en `A500_debug` con 256 bandas de cielo:

```
total 1.156.019 ciclos/frame (8,15 campos; 1 frame PAL = 141.876)
copper      793.439  68,6%   build_frame completo (el Plan)
  materialize  534.004  46,2%   ordenar + emitir
    sort_lines   105.637   9,1%   counting sort por 256 lineas
    sort_prio     43.857   3,8%   prioridad dentro de linea
    emit         387.674  33,5%   traduccion a WAIT/MOVE (el mayor coste unico)
  sky           166.475  14,4%   construir las 256 intenciones del cielo
  static         41.817   3,6%   begin_frame + display + paleta
  objcopper      28.161   2,4%   necesidades de copper de los objetos
actors         130.363  11,3%   colocacion + actor_emit
blits          109.223   9,4%   Blitter con esperas
calib           18.250   1,6%   bucle de calibracion (conocido)
```

Comprobacion cruzada que descarta el perfil nativo: con `K_086_SKY_BANDS=1` (de 256 intenciones a 1) el frame solo baja un 3 % (8,2 → 8,0 lineas), no lo que predeciria un reparto dominado por la ordenacion. Ver §5.

### El `.amigaprofile` del plugin NO es fiable para ciclos (trampa)

`tools/analyze/profile-report.mjs` sobre el `.amigaprofile` exportado por el plugin daba `Plan::sort_by_top` 39 % y `add_prioritized` 31 %, **contradiciendo** la medida por secciones y la prueba de `K_086_SKY_BANDS=1`. Causas:

- Los `timeDeltas` del perfil son **microsegundos del host** (el emulador en el PC), no ciclos del Amiga; suman 142.096 µs cuando el frame Amiga son 141.876 **ciclos**, una coincidencia numérica que no significa correspondencia.
- La atribucion a funciones usa muestreo con desenrollado de pila, que en codigo con `always_inline` y bloques `volatile` (instrumentacion) coloca el PC en el bloque equivocado.

Uso valido del `.amigaprofile`: reparto **cualitativo** (que funciones aparecen y su orden de magnitud) y registros custom del frame (`$amiga`). Para decisiones de optimizacion, usar las secciones del contador Amiga.

### Reparto (referencia historica, perfil del plugin, un frame)

Se conserva solo como ejemplo de salida de `profile-report.mjs`; **no usar sus porcentajes para decidir**:

```
  4828 muestras  eng::copper::Plan::sort_by_top()
  2884 muestras  eng::copper::Plan::add_prioritized(...)
  1904 muestras  BobObjectsDemo::build_frame()
   672 muestras  eng::scene::actor_add_copper(...)
   601 muestras  eng::copper::Plan::raster_key(...) (inlined)
  --- por archivo ---
  copper/plan.hpp dominante | src/main.cpp | scene/actor.hpp
```

Anomalía abierta: la misma demo con `--release` mide **2,4× más lento** que en debug (2,48 fps / 20,2 campos frente a 6,14 / 8,1). Es un problema de generación de código, no de `-O1`: **no fiarse de medidas en release hasta resolverlo**.

## 5. Trampas conocidas (revisar antes de concluir)

- **El perfilador se mide a sí mismo**: cada marca lee el contador y su tiempo entra en la cuenta; con muchas secciones llega a verse un 14 % en `prof_clock() (inlined)`. Mantener 2-3 secciones gruesas y descontar. La atribución `(inlined)` del perfil culpa además al bloque inlined completo.
- **Perfilar con el depurador enganchado** añade IRQs y el stub GDB (17 % en `[IRQ]`). Perfilar con `run-demo.sh` + MCP, no desde el depurador del plugin.
- **El `.amigaprofile` del plugin no da ciclos del Amiga**: sus `timeDeltas` son µs del host y su atribución por desenrollado coloca mal el PC en código con `always_inline`/`volatile`. Da un reparto que **contradice** la medida por secciones (`sort_by_top` 39 % frente al 9,1 % real). Ver §4: usar las secciones del contador Amiga para decidir.
- **Las muestras de CPU necesitan tabla de unwind**: `profile N "" bin` produce 0 muestras; hace falta el `.unwind` que `tools/debug/winuae-profile.mjs` construye. Aun con él, en la 086 el binario salió con 0 muestras (opción de muestreo de WinUAE pendiente): no dar por hecho que el camino nativo funciona sin comprobarlo.
- **El canal lateral no refresca el PC**: el campo `pc` de `state` (2346) devuelve el PC de cuando se entró en `observe`, así que muestrear por esa vía repite símbolo. `tools/profile/hotspots.mjs` avisa de ello y queda deprecado para reparto por rutina.
- **Con la CPU corriendo, el GDB no responde `g`**: muestrear por pausa/lectura/reanudación produce ~1 muestra cada varios segundos. No es práctica.
- **`parseProfile` del MCP**: corregido para conservar `profileArray`; `profile-ollama` ya resume por sección/PC. Para nombres de función hace falta el `.map` (`tools/analyze/profile-samples.mjs`).
- **El assert visual por defecto (`analyze-screenshot.sh`) exige `min-white=10`** aunque la demo no use blanco. La 086 (paleta sin `0xfff`) da `white=0` y la regresión falla con `FAIL : white=0 < 10` **sin que nada esté roto**: es un falso positivo del threshold por defecto, no una rotura de la demo ni del build.
- **La regresión con `--release-build` compila en release pero ejecuta la config `A500_debug`**: `run-demo.sh` se invoca sin `--config` y elige por prioridad. Para medir/validar release de verdad hay que pasar `--config A500_release` a `run-demo.sh`/`measure-fps.mjs`.
- **Shell Windows**: PowerShell 5.1 rompe `&&`, `|`, `(`, `$`, comillas y regex con backslashes. Usar la tool de edición del agente para ficheros y `bash fichero.sh` (script en `out/tmp/`) para secuencias; `Get-Content`/`Set-Content` de PowerShell re-encoda y genera mojibake.
- **Configs de run**: `--config` es flag (`run-demo.sh demo --config A500_release`), no argumento posicional.

## 6. Optimizaciones identificadas (priorizadas, con evidencia)

Reparto fiable de referencia (`ENG_PROF_*`, contador Amiga): `copper` 68,6 %, dentro `emit` 33,5 % + `sky` 14,4 % + `sort_lines` 9,1 % + `sort_prio` 3,8 %; `actors` 11,3 %; `blits` 9,4 %.

1. **`emit` (33,5 %, 387.674 ciclos)** — traducción de intención a WAIT/MOVE. Ya aplicado: `always_inline` en `write_pair`/`move`/`wait_line`/`wait_position`/`emit_single_intent`/`emit_palette` (el asm a `-O1` recargaba `m_ok`/`m_used_words`/`m_capacity_words` y recomputaba el puntero base en cada palabra). Efecto medido: 1.188.131 → 1.156.019 ciclos (−2,7 %); `emit` bajó de ~460k a 387k (−16 %). Siguiente paso: emitir en lote (WAIT+MOVEs de una línea de una pasada) en lugar de 1 intención por iteración con `m_perm` indirecto.
2. **`sky` (14,4 %, 166.475 ciclos)** — la demo construye las 256 intenciones del cielo cada frame aunque el degradado es idéntico. Reutilizarlas (o construirlas una sola vez fuera del bucle) ataca ese 14,4 % de raíz.
3. **`sort_lines` + `sort_prio` (12,9 %)** — counting sort sobre 256 líneas cada frame. Ya aplicado: arrays del sort como miembros (no 1 KB en pila). Mejora menor medida; si el cielo se precomputa (2), este coste cae con él porque hay menos intenciones que ordenar.
4. **`actors` (11,3 %) + `blits` (9,4 %)** — 8 BOBs cuestan ~21 % combinados. Aplanar la ruta de actor y agrupar arranques de Blitter.
5. **`calib` (1,6 %)** — quitarla cuando no se esté midiendo la velocidad del CPU.
6. **Resolver el 2,4× de release** antes de fiarse de cualquier optimización en release.

No es prioridad con la evidencia actual: `materialize` como fase (ya cubierto por sus partes), `static` (3,6 %) y `objcopper` (2,4 %).

## 7. Referencias

- Reglas de optimización y port a asm: §12 de `OPTIMIZACION_GPP_68000.md`.
- Perfilador y secciones: `engine/include/eng/debug/prof.hpp`, `engine/include/eng/graphics/copper/plan.hpp`.
- Ciclos y temporización de pantalla: `docs/reference/ahrm/` y `docs/engine/architecture/DISPLAY_COMPOSITION.md`.
- Plan de copper y buffers: §6 de `docs/engine/architecture/DISPLAY_COMPOSITION.md`.
