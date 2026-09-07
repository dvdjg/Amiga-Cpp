# Estructura canónica del repositorio

Este documento es la **especificación autoritativa** de cómo se organizan los
archivos y directorios de este repositorio. Toda ruta nueva que se cree, toda
demo, asset, tool, documento, juego o experimento debe seguir estas reglas.
`AGENTS.md` referencia este documento; si una instrucción de `AGENTS.md`
contradice algo de aquí, prevalece la regla más reciente entre ambos y debe
quedar anotada la discrepancia.

## 1. Mapa conceptual de directorios raíz

```
Amiga-Cpp/
├── AGENTS.md          → reglas operativas para agentes (enlaza a docs/STRUCTURE.md)
├── engine/            → 1. Código del engine (capa de abstracción + backends)
├── demos/             → 2. Demos por plataforma (ejemplos de técnicas)
├── assets/            → 3. Assets FUENTE (raw, editables, con licencia)
├── tools/             → 4. Herramientas de desarrollo host (TypeScript/bash, compiladas a dist/)
├── scripts/           → 4. Scripts de apoyo de entorno (instalación, utilidades)
├── support/           → 4. Código de apoyo al linkado/compilación (ASM/C)
├── host-tools/        → 5. Programas de apoyo INDEPENDIENTES del engine (Go/C++ para PC)
├── playground/        → 6. Playground de pruebas de algoritmos/calidad
├── games/             → 7. Juegos generados con el engine (dentro de este repo)
├── tests/             → Tests host + L0 bare metal
├── legacy/            → Proyecto C legado (congelado, no tocar)
├── config/            → Configuración del emulador/entorno (.uae)
├── docs/              → 8. Documentación (índice en docs/README.md)
├── out/               → TODO lo generado (gitignored; reglas en out/README.md)
├── obj/               → Intermedios de compilación (gitignored)
├── dist/              → TypeScript compilado (gitignored)
└── artifacts/         → Resultados/evidencia CANÓNICOS commiteados (congelados)
```

```
                        ┌─────────────────────────────────────────┐
                        │              docs/STRUCTURE.md          │
                        │   (especificación: qué va en cada sitio) │
                        └───────────────────┬─────────────────────┘
      ┌──────────────┬─────────────┬────────┴───────┬──────────────┬─────────────┐
      ▼              ▼             ▼                ▼              ▼             ▼
   engine/        demos/        assets/          tools/       playground/    games/
   (código        (demos/       (fuente raw      (script+     (experimentos  (juegos
    + backends)    amiga/…)       por platform)    tools host)  de calidad)    hechos)
```

## 2. Reglas transversales

### 2.1 Lo generado nunca mezcla con lo fuente
- **`out/`** es el único lugar para salidas de herramientas, builds, capturas,
  informes y assets *generados*. Es gitignored y se puede borrar con seguridad.
- **`obj/`** guarda intermedios de compilación; la estructura refleja la fuente.
- **`dist/`** guarda el TypeScript compilado (`npm run build`); `tsconfig.json`
  genera `dist/` con el mismo árbol que las fuentes de `tools/`, `tests/`,
  `demos/`, `scripts/`.
- **`artifacts/`** guarda resultados de referencia del pipeline para lectura
  histórica. **Sólo se versionan metadatos pequeños** (README, informes de
  texto): la media generada (PNG/JPG/binarios) **NO se sube a git** (son
  reproducibles con las tools hacia `out/assets/` y saturarían el repositorio).
  Un tool solo escribe ahí si se le pide explícitamente.
- **Media y binarios (regla general):** archivos binarios y media masiva quedan
  excluidos de git mediante las reglas de `.gitignore`. La excepción son los
  **assets fuente** de `assets/` (ver §5), que sí se versionan como material de
  trabajo del pipeline.

### 2.2 Convención de nombres
- Directorios y archivos en **minúsculas con guiones bajos** (`snake_case`); sin
  espacios ni caracteres acentuados en rutas de código.
- Las demos y los juegos llevan **prefijo numérico de tres dígitos** con orden de
  dificultad/concepto (`0xx` = boilerplate/hardware base, `1xx` = scroll/tiles,
  `2xx` = pipelines de assets a escena completa). Los números no se reutilizan.
- Los **nombres de archivo de salida** deben ser autoexplicativos
  (`demo201_reconstruct_32c_kmeans_floyd_720x416.png`), nunca crípticos
  (`out/final.png`).

### 2.3 Rutas relativas al repo
- Las herramientas se invocan desde la raíz del repo con rutas relativas:
  `demos/amiga/107_xlimited_corkscrew`, `tools/build/build-demo.sh`.
- El código C++ de las demos que incrusta assets generados usa rutas relativas
  a la raíz del repo con el número exacto de `../` según su profundidad, o
  `incbin` con ruta relativa al cwd del build (la raíz del repo).
- Nunca usar rutas absolutas de la máquina local en código, docs o tools.

## 3. El engine (`engine/`)

Áreas del engine, separadas por concepto:

```
engine/
├── include/eng/          → API del engine (header-only, sin backend)
│   ├── core/             → algoritmos y utilidades genéricas (fast_div, sinetable, span, tipos, ct_array)
│   ├── memory/           → gestión de memoria (arena)
│   ├── graphics/         → abstracción de gráficos
│   │   ├── copper/       →   generación/programación de copperlists
│   │   ├── drivers/      →   drivers de escena (tile_scroll, ehb_scene, …)
│   │   ├── effects/      →   efectos (palette_cycle, …)
│   │   └── tilemap/      →   tile schedulers / upload (tile_scroll.hpp)
│   ├── field/            → playfield / scroll / X-Limited (escena 2D)
│   ├── scene/            → escena virtual / cámara
│   ├── debug/            → telemetría, run_status, periférico de depuración
│   └── platform/         → interfaz de hardware (ABI de backend)
└── src/
    └── platform/         → implementaciones de backend por máquina
        ├── amiga_minimal/   → backend Amiga OCS/ECS (amiga_minimal.cpp)
        ├── atarist/         → (futuro)
        └── megadrive/       → (futuro)
```

Reglas:
- **`include/eng/`** es la capa de abstracción: APIs paramétricas, agnósticas de
  hardware, sin registros ni DMA concretos. Los registros/drivers específicos de
  cada máquina viven en las capas backend de `src/platform/` y en
  `graphics/drivers/`.
- **`src/`** contiene solo implementaciones de backend; el resto del engine es
  header-only para minimizar acoplamiento y permitir inline en las demos.
- Algoritmos nuevos: si son genéricos (no dependen de hardware) van a `core/` o
  `memory/`; si dependen del chipset, a la capa/backend correspondiente.

## 4. Demos (`demos/`)

Agrupadas **por plataforma objetivo**; dentro de cada plataforma, numeradas:

```
demos/
├── amiga/                → demos Amiga OCS/ECS (A500)
│   ├── 000_toolchain_cpp23/        → boilerplate/toolchain, hola mundo
│   ├── 010_chip_slow_memory/       → conceptos de Chip RAM
│   ├── 020_copper_basic/ …         → copper, paletas, blitter, bobs…
│   ├── 100_virtual_tile_scene_scroll/ … → scroll y tile fields
│   ├── 200_… / 201_ehb_map/ / 202_xlimited_dpf/ → escenas X-Limited con pipeline de assets
├── atarist/              → (futuro) técnicas Atari ST
└── megadrive/            → (futuro) técnicas Megadrive
```

Estructura interna de una demo (obligatoria):

```
demos/<platform>/NNN_tema/
├── src/                  → código fuente C++ de la demo (main.cpp + unidad.…)
├── README.md             → qué enseña, invariantes, comandos build/run/analyze
├── analyze-sequence.sh   → (opcional) secuencia de verificación temporal de la demo
├── analyze-screenshot.sh → analizador visual específico (o ausente si usa el genérico)
└── <otros scripts/json de análisis propios de la demo>
```

Notas:
- La demo solo contiene **código y análisis propio**; los **assets que usa van en
  `assets/<platform>/`** (fuente) y los **generados van en `out/assets/<pipeline>/`**,
  incrustados por `incbin` o include con ruta relativa al repo.
- `tests/l0_bare_metal/` es el nivel 0 de verificación de hardware/display; no es
  demo y no se promueve a `demos/`.

## 5. Assets (`assets/`)

Assets **fuente**: editable, con licencia, listos para alimentar un pipeline
(cuantización, tiling, sprites, audio). Nunca se escribe aquí automáticamente.

```
assets/
├── README.md             → qué hay, licencias conocidas, de dónde salió cada fuente
├── amiga/
│   ├── tiles-reference/  → imágenes de referencia reales para probar el pipeline
│   ├── sprites/          → sprites/bobs fuente
│   ├── audio/            → módulos/efectos fuente
│   └── maps/             → mapas editables (TMX, texto planimetría, …)
├── atarist/
└── megadrive/
```

Regla: un pipeline de generación **lee** de `assets/<platform>/<dominio>/` y
**escribe** en `out/assets/<pipeline>/...`, nunca al revés. Los assets generados
que son canónicos (p. ej. resultado de referencia de una demo) pueden congelarse
en `artifacts/` con un commit explícito.

## 6. Herramientas de desarrollo (`tools/`, `scripts/`, `support/`)

- **`tools/`**: herramientas host del pipeline de desarrollo (TypeScript
  compilado a `dist/` y wrappers bash). Orquestan build/run/analyze/verify de las
  demos y generan assets. Se organizan por dominio:
  `tools/build/`, `tools/run/`, `tools/analyze/`, `tools/debug/`, `tools/profile/`,
  `tools/framescope/`, `tools/input/`, `tools/vision-review/`, `tools/amiga-tiles/`,
  `tools/ehb/`, `tools/demo202/`, `tools/lib/` (helpers compartidos: `paths.ts`,
  `cli.ts`, `image.ts`), `tools/test-regression.sh`.
- **`scripts/`**: scripts de apoyo de entorno/instalación (p. ej.
  `install-winuae-dbg-to-extension.sh`).
- **`support/`**: código de apoyo al compilador/linker de la plataforma (stubs de
  GCC 8 ASM/C, depackers, etc.), incluido en los builds de las demos.

### 6.1 Criterio común de directorios de salida (obligatorio para tools)
Toda tool genera sus ficheros en un subdirectorio canónico de `out/`, con un
**nombre auto-descriptivo** y el mismo esquema invocación → destino *siempre*:

| Dominio | Ruta canónica de salida |
|---|---|
| Builds de demos/tests | `out/demos/<demo>/<CONFIG_ID>/` y `obj/demos/<demo>/<CONFIG_ID>/` |
| Capturas del runner | `out/run/<demo>/[<CONFIG_ID>/]` |
| Assets generados por pipelines | `out/assets/<pipeline>/…` |
| Resultados de herramientas de análisis | `out/<area>/…` |
| Profiling | `out/profile/<nombre>/` |
| Regresión | `out/regression/<YYYYmmdd-HHMMSS>/` |
| FrameScope | `out/framescope/<demo>/` |
| Depuración interactiva | `out/debug-current/` |
| Playground/experimentos | `out/playground/<experimento>/` |
| Salida temporal o ad-hoc | `out/tmp/<experimento>/` (nunca directorios sueltos en `out/`) |

La regla de oro: **cada job que genere archivos debe admitir `--out <ruta>` con
una ruta por defecto ya canónica**, y el valor por defecto debe ser el mismo
independientemente de quién lo invoque (humano o IA). Si un experimento necesita
una salida ad-hoc, debe ir a `out/playground/<experimento>/` o `out/tmp/`, nunca
crear una carpeta nueva directamente en `out/`.

## 7. Programas de apoyo independientes (`host-tools/`)

Programas completos que corren en la máquina de desarrollo **sin depender del
engine ni del toolchain Amiga** (p. ej. una utilidad Go o C++ para PC). No forman
parte del pipeline de `tools/`; se compilan/ejecutan por separado.

```
host-tools/
├── README.md             → qué hay y cómo se compila cada programa
└── <programa>/           → cada programa su propio subdirectorio (go.mod, CMake, README, salida en out/tmp o playground)
```

## 8. Playground (`playground/`)

Zona para probar algoritmos, medir calidades de ejecución y validar ideas antes
de formalizarlas como tool o demo. El criterio de medición debe ser **unificado**:

```
playground/
├── README.md             → criterio de medición unificado (ver sección 8.2)
└── <experimento>/        → código + script del experimento
```

### 8.1 Reglas
- Cada experimento tiene un directorio propio con su código; los **resultados y
  medidas se escriben en `out/playground/<experimento>/`**, nunca en el alias.
- Los experimentos que maduran se promueven a `tools/<area>/` (o `engine/` si es
  un algoritmo) y se documentan; los que se abandonan se borran o se archivan.

### 8.2 Criterio unificado de medición
- Fijar **inputs** (fijaciones/imágenes) en `assets/<platform>/…` o en el propio
  `playground/<experimento>/` (nombres deterministas).
- Registrar en cada salida: la **configuración completa** (flags, versión de la
  tool, fecha/hora, hash de inputs) en un `run.json` o cabecera del informe.
- Comparar **en el mismo espacio de referencia** (misma geometría, misma paleta,
  mismo assert) — ver `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`.
- Las medidas quedan como **evidencia**: un valor sin la forma de reproducirlo
  no es una medida válida.

## 9. Juegos generados con el engine (`games/`)

El engine está en continua evolución; los **primeros juegos no se hacen en
repositorios separados**, sino en este repo bajo `games/`, para iterar engine +
juego juntos. Cuando un juego madure podrá extraerse a su propio repo.

```
games/
├── README.md             → qué juegos hay, estado y a qué demo/mecánica corresponden
└── NNN_nombre/           → mismo esqueleto que una demo (src/, README.md, analizadores)
```

El juego es un "demo productiva": reutiliza las mismas mecánicas de build/run
(`tools/build/build-demo.sh games/100_nombre`), y su código vive en `src/` como
en las demos. Los assets propios del juego: fuente en `assets/<platform>/…` y
generados en `out/assets/<juego>/…`.

## 10. Documentación (`docs/`)

Estructura de crates **física** que debe respetar todo documento nuevo:

```
docs/
├── README.md             → índice maestro de la documentación
├── STRUCTURE.md          → ESTE documento (organización del repo)
├── engine/
│   ├── architecture/     → arquitectura del engine (layers, memoria, rendering)
│   └── c-engine/         → doc del engine C legado (contexto histórico)
├── demos/
│   ├── effects/          → doc de demos y efectos (demoscene, qué enseña cada demo)
│   └── tile-pipeline/    → pipeline de assets/tiles/EHB (docs + reglas + informes IA)
├── tools/                → doc de herramientas: build/run/analyze/debug/profile/verify
├── reference/
│   ├── amiga/
│   │   ├── hardware/     → hardware Amiga (chipset, DMA, copper, manuales)
│   │   └── techniques/   → técnicas de programación Amiga
│   ├── atarist/          → (futuro) referencia Atari ST
│   └── megadrive/        → (futuro) referencia Megadrive
├── guides/
│   ├── roadmap/          → roadmaps y planes vigentes
│   ├── optimization/     → guías de optimización 68000/C++
│   └── methodology/      → metodología, runbooks de agentes, flujo de trabajo
├── debugging/            → específico de depuración (WinUAE, DAP, historiales)
├── build/                → build del entorno y de las demos
├── emulation/            → emulación y herramientas del emulador
├── testing/              → testing/verificación
├── ai-dev-environment/   → entorno de desarrollo con IA
└── legacy/               → documentación del proyecto C legado (congelada)
```

### 10.1 Dónde va cada documento nuevo
| Tema | Destino |
|---|---|
| Tutorial "cómo crear una demo" | `docs/guides/roadmap/` o `docs/guides/methodology/` (plantilla en `docs/demos/effects/`) |
| Guía de estilo de código | `docs/engine/architecture/CODING_STYLE.md` |
| Guía de optimización | `docs/guides/optimization/` |
| Documentación de una tool concreta | `docs/tools/` + `README.md` junto al código de la tool |
| Referencia de hardware/registros | `docs/reference/<platform>/hardware/` |
| Técnicas de programación | `docs/reference/<platform>/techniques/` |
| Roadmap / plan vigente | `docs/guides/roadmap/` |
| Reportes de IA / informes de demos | `docs/demos/tile-pipeline/` (o el propio `demos/<platform>/NNN/README.md`) |

### 10.2 Formato (obligatorio)
- Español con ortografía correcta (tildes, eñes, puntuación).
- Párrafos en una sola línea lógica (word wrap), sin saltos a mitad de frase.
- Diagramas ASCII cuando aclaren capas, flujos, geometrías de buffers o zonas de
  hardware.
- No duplicar: todo hecho debe estar descrito en un solo sitio y enlazado.

## 11. `out/` en detalle

Consúltese también `out/README.md`. Resumen de las reglas canónicas:

```
out/
├── README.md             → reglas de salida (este criterio)
├── demos/<demo>/<CONFIG_ID>/      → builds (elf/exe/map/listing)
├── run/<demo>[/<CONFIG_ID>]/      → capturas, sequence/, run-report.json, screenshot.png
├── assets/<pipeline>/…            → assets generados (ehb/, demo202/, tile-demos/)
├── profile/<name>/                → profiling (bin + frames + informes)
├── regression/<timestamp>/        → informes de regresión
├── framescope/<demo>/             → informes FrameScope
├── analysis/<demo>/…              → asserts/pixel-assert
├── debug-current/                 → build de depuración interactiva (F5)
├── playground/<experimento>/      → salidas de playground
└── tmp/                           → salidas temporales/ad-hoc efímeras
```

Prohibido: directorios ad-hoc de un solo nivel directamente en `out/` (p. ej.
`out/auto_green`, `out/assets/demo202` suelto), salvo que sean el destino por defecto
de una tool canónica y esté documentado.

## 12. Herramientas de verificación y orden canónico

- `build → run → analyze` es el orden canónico; `tools/test-regression.sh` ya lo
  impone por demo (ver AGENTS.md y `docs/build/BUILD_AND_RUN.md`).
- Toda tool reutilizable debe aceptar `--help` y documentar sus opciones en su
  propio `README.md` dentro de `tools/`.
- Cuando una IA genere archivos (assets de prueba, capturas, informes), debe usar
  la estructura canónica de este documento y los `--out` por defecto de las
  tools; nunca inventar directorios.