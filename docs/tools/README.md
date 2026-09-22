# Documentación de herramientas

Aquí vive la documentación agregada de las **tools de desarrollo** del repo
(tools/scripts/support). La documentación específica de cada herramienta suele
estar en su propio `README.md` junto al código en `tools/`; este índice enlaza y
explica dónde está cada cosa.

Reglas principales (ver también `docs/STRUCTURE.md` §6):
- Toda tool reutilizable debe aceptar `--help` y documentar sus opciones.
- Toda tool que genere archivos debe aceptar `--out` con un defecto canónico en
  `out/` (ver `out/README.md`).
- Los helpers compartidos de TypeScript viven en `tools/lib/` (`paths.ts`,
  `cli.ts`, `image.ts`) y se compilan a `dist/` con `npm run build`.

## Índice por área

| Área | Documentación | Herramientas |
|---|---|---|
| Build de demos | `docs/build/BUILD_AND_RUN.md` | `tools/build/build-demo.sh`, `tools/build/build-all-demos.sh`, `tools/test-regression.sh` |
| Ejecución (runner/emulador) | `docs/build/BUILD_AND_RUN.md`, `docs/debugging/system/debug-winuae-v2-guide.md` | `tools/run/run-demo.sh`/`.ts` |
| Análisis de demos | `docs/testing/PIXEL_FRAME_ASSERTIONS.md`, `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` | `tools/analyze/*` |
| Depuración (WinUAE-DBG/DAP) | `docs/debugging/system/debug-winuae-v2-guide.md`, `tools/dap-test/README.md` | `tools/debug/*`, `tools/dap-test/*` |
| Profiling | `tools/profile/README.md`, `docs/tools/PROFILING_FROM_AGENT.md`, `docs/guides/optimization/METODOLOGIA_PROFILING.md` | `tools/profile/*`, `tools/debug/{measure-fps,record-fps,check-fps,profile,winuae-profile}.mjs`, `tools/analyze/profile-{samples,report}.mjs`, `tools/run-fps-gate.sh` |
| Codegen 68000 | `docs/engine/architecture/EXPRESSION_TEMPLATES.md`, `docs/engine/architecture/MATH_LIBRARY.md` §4 | `tools/analyze/codegen-report.mjs`, `tools/analyze/expr-asm-compare.mjs` |
| Verificación visual | `tools/vision-review/README.md`, `docs/testing/VISION_REVIEW_ROADMAP.md` | `tools/vision-review/*` |
| Pipeline de tiles/sprites | `tools/amiga-tiles/README.md`, `docs/demos/tile-pipeline/` | `tools/amiga-tiles/*`, `tools/ehb/*`, `tools/demo202/*` |
| Assets (UAF-R) | `docs/tools/UAF_PACK.md` | `tools/assets/uaf-pack.ts` |
| Audio (muestras del mixer) | `tools/audio/README.md` | `tools/audio/*.ts` |
| FrameScope | `docs/testing/FRAMESCOPE_ROADMAP.md` | `tools/framescope/*` |
| Entrada (mouse) | `tools/input/*`, `docs/emulation/MOUSE_AUTOMATION.md` | `tools/input/mouse-path.*` |
| Programas independientes para PC | `host-tools/README.md`, `playground/README.md` | `host-tools/`, `playground/` |
| Comprobaciones estáticas | esta sección (abajo) | `tools/check/*` |

## Comprobaciones estáticas (`tools/check/`)

Checks baratos que corren en `tools/test-regression.sh` y en la pasada completa de
`tools/run-host-tests.sh`. Todas aceptan `--help` salvo las de shell.

| Tool | Comprueba |
|---|---|
| `encoding.mjs` | Que los archivos de texto sean UTF-8 válido y sin mojibake. Admite `[raices...]`. |
| `links.mjs` | Que los enlaces relativos de la documentación resuelvan a ficheros/directorios existentes. Admite `[raíces...]`, `--quiet`, `--json` y `--update-baseline`. |
| `type-tagging.mjs` | Que no aparezca un `MemoryBlock` crudo convertido a tipo de dominio (INTERNAL_TYPE_SYSTEM.md §1). |
| `scalar-support.mjs` | Que la tabla función × escalar de `SCALAR_LIBRARY.md` esté sincronizada con su fuente única. |
| `math-diagnostics.sh` | Diagnósticos del vocabulario matemático fixed-point. |

Uso típico de `links.mjs`:

```bash
node tools/check/links.mjs                       # documentación por defecto
node tools/check/links.mjs docs/reference        # un árbol concreto
node tools/check/links.mjs --json                # resultado legible por máquina
node tools/check/links.mjs --update-baseline     # acepta la deuda actual como baseline
```

Por defecto revisa la documentación mantenida **y** los árboles importados (`docs/engine/c-engine`, `docs/legacy`). El check falla ante cualquier enlace roto; `tools/check/links-baseline.txt` permite aceptar deuda histórica de forma explícita (solo contiene las cabeceras mientras no haya deuda). Al arreglar enlaces, regenerar el baseline con `--update-baseline`.

## Gate de fps (deriva de rendimiento)

`tools/debug/` mide fps en tiempo emulado y detecta deriva contra la tabla trazable de
`docs/guides/roadmap/BITACORA_SCROLL_TILES.md`:

- `measure-fps.mjs <demo> [config] [--json]` — una medición (contador de ciclos).
- `record-fps.mjs <demo> [config] [--samples N] [--dry-run]` — mide y anexa/actualiza la fila de la bitácora (fecha, commit, config, `detail`). Registra la **mejor de N** muestras (def. 2) para no depender de la fase.
- `check-fps.mjs [--demo <substr>] [--threshold 0.9] [--samples N] [--warn-only] [--json]` — compara la **mejor de N** de cada fila con el baseline; falla si baja del umbral.
- `tools/run-fps-gate.sh [--samples N] [--threshold X] [--warn-only] [--no-prepare]` — **job programable**: asegura build + `runner.uae`, ejecuta el gate y guarda `out/fps-gate/<timestamp>/report.txt`. Pensado para lanzarse periódicamente (Task Scheduler/cron), no en cada regresión.

La regresión lo lleva como opt-in `--fps-gate` (añade ~25 s de emulador por muestra y demo).

## Perfil por secciones (¿dónde se va el frame?)

`tools/debug/profile.mjs` lee `eng::debug::g_eng_prof` (contadores de ciclos por sección,
`engine/include/eng/debug/prof.hpp`) de la demo en marcha y lo traduce a ciclos/frame, % del total y
llamadas/frame. La diferencia *total − suma(secciones)* es la espera de VBlank + `render` + bucle, así
que distingue **trabajo propio** de **espera** sin instrumentar el motor.

- En la demo: `ENG_PROF_INIT(n)` una vez, `ENG_PROF_FRAME()` por frame y
  `ENG_PROF_BEGIN/END(seccion)` alrededor de los tramos a medir. La demo define el bloque
  (`g_eng_prof`, con enlace C para que salga sin manglar en el `.map`).
- Medir: `bash tools/run/run-demo.sh <demo>` y luego
  `node tools/debug/profile.mjs <demo> [config] [segundos]`.
- Coste: ~2 lecturas del contador por sección y frame (el contador es el mismo `0xB7E928` que usa
  `measure-fps.mjs`).

## Análisis de perfiles (muestras de CPU por rutina)

El perfilador de la extensión (**F5 → Frame Profiler**) y el MCP capturan un binario con
**muestras de CPU**, DMA por scanline y recursos. Resolver esas muestras a funciones es lo que
dice **qué código se lleva el frame**; el procedimiento completo (tabla de unwind, comandos y
trampas) está en `docs/tools/PROFILING_FROM_AGENT.md` y la metodología general en
`docs/guides/optimization/METODOLOGIA_PROFILING.md`.

| Tool | Devuelve |
|---|---|
| `tools/debug/winuae-profile.mjs <demo> [config] [frames]` | Captura binaria + ciclos de CPU ocupada/libre por frame + DMA por tipo. Construye la tabla `.unwind` que WinUAE necesita para muestrear. |
| `tools/analyze/profile-samples.mjs <perfil.bin> <demo> [config] [--top N] [--json]` | Top de rutinas por muestras de CPU, resolviendo los PCs con el `.map` del build. |
| `tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N] [--json]` | Top de rutinas/archivos de un perfil JSON exportado desde VSCode (incluye además `[IRQ]` del depurador). |

`--json` emite una tabla compacta apta para pasársela a un modelo local sin gastar contexto.

## Codegen 68000 (qué genera g++)

Compilan una sonda a ensamblador del 68000 (`-S`) y la inspeccionan; sirven para decidir si
una construcción es aceptable antes de adoptarla, no solo para depurar.

| Tool | Devuelve |
|---|---|
| `tools/analyze/codegen-report.mjs` | Compila una sonda con una función por construcción (fixed, `Vec`/`Mat`, escalares, IA…) y reporta instrucciones, `muls.w`, desplazamientos, libcalls y si el bucle queda plegado. **Falla** si aparecen libcalls de libgcc de 32/64 bits o instrucciones 68020 en los caminos calientes. |
| `tools/analyze/expr-asm-compare.mjs` | Compara la MISMA expresión escrita con operadores sueltos y con `eng::math::et` (escalar `MiniFloat16` y `Vec<3>`), y reporta instrucciones, escrituras a pila, llamadas y si el bucle se desenrolla. Es la evidencia de "qué ahorra la fusión" (ver `docs/engine/architecture/EXPRESSION_TEMPLATES.md` §5). |

## Sondas de emisión de sprites (depuración de custom chips)

Leen del emulador lo que el engine **programa** y lo que el chipset **tiene**, sin depender de la
captura de pantalla: registros de sprite, la copperlist instalada (`COP1LC`) decodificada, la DATA de
los 8 canales y la paleta. Requieren la demo preparada (`tools/run/run-demo.sh <demo>`) y su
`runner.uae`.

- `read-sprite-regs.mjs <demo> [config]` — `SPRxPT/POS/CTL` y `DMACON`.
- `decode-copper.mjs <demo> [config] [words]` — decodifica la lista desde `COP1LC` (WAITs y MOVEs).
- `probe-sprite-data.mjs <demo> [config]` — DATA de los 8 canales + `COLOR00..31` + registro de display.
- `probe-sprite-emission.mjs <demo> [config]` — **todo en una sola ejecución** (lista + config por
  canal + DATA + paleta), que es lo que permite separar «no dibuja por la DATA» de «no dibuja por el DMA».

Reglas de método: no comparar lecturas de custom registers **entre ejecuciones** (la base de la arena
cambia por run) y tener presente que `SPRxPT`/`POS`/`CTL` son *write-only* (su lectura no es fiable).

## Dónde poner documentación nueva de tools
- Si es específica de una herramienta: `README.md` junto al código.
- Si es procedimiento/flujo de varias tools: `docs/tools/` (crea subcarpeta por
  tema) o el índice correspondiente (`docs/build/`, `docs/testing/`, etc.).