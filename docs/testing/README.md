# Validación y testing

Pirámide de validación **determinista** del engine: contrato de píxeles, secuencias de
frames, FrameScope (análisis temporal sin IA visual) y Vision Review (inspección con VLM
OpenAI-compatible, opcional). La regresión completa las encadena por demo.

> La batería de pruebas y la taxonomía de tests del **engine C** (histórico) están en
> [../engine/c-engine/](../engine/c-engine/README.md), no en esta carpeta.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [TAXONOMY.md](TAXONOMY.md) | Organización de los tests por plataforma, nivel y categoría (árbol, numeración y cómo añadir/correr). |
| [PIXEL_FRAME_ASSERTIONS.md](PIXEL_FRAME_ASSERTIONS.md) | Contratos declarativos `pixel-contract.json` (checks por ROI) y arquitectura de asserts. |
| [FRAMESCOPE_ROADMAP.md](FRAMESCOPE_ROADMAP.md) | Subproyecto FrameScope: análisis visual temporal determinista (métricas, grids, contact-sheet). |
| [VISION_REVIEW_ROADMAP.md](VISION_REVIEW_ROADMAP.md) | Capa de inspección con IA visual sobre pocos frames (perfiles y proveedores). |

## Flujo de validación (por demo)

1. `build-demo.ps1` -> `run-demo.ps1` (espera `READY` por canal lateral) -> `analyze-demo.ps1`.
2. `analyze-sequence.ps1` (por demo) verifica animación/estático, negro interno y telemetría.
3. `assert-pixel-contract.ps1` + `pixel-contract.json` para checks deterministas por región.
4. `frame-scope.ps1` con perfil `amiga-scroll` para correlacionar movimiento observado con la cámara.
5. Opcional: `vision-review.ps1` con LM Studio (`-VisionReview`).
6. `tools/test-regression.ps1` ejecuta todo el pipeline por demo y genera el informe en `out/regression/`.
7. **Frames esenciales** (`<demo>/vision-points.json`): si Ollama está disponible, `tools/vision-review/essential-frames.mjs` describe con un modelo de visión los frames de **transición** que declara la demo y compara con lo esperado. La regresión añade la columna `Vision` (no falla salvo `--require-essential-ok`). Ver `tools/vision-review/README.md`.
8. **Parpadeo / glitch** (`tools/vision-review/flicker-check.mjs`): detecta zonas con oscilación temporal en **frames consecutivos** y pide al modelo que las describa → informe accionable en `out/vision-review/<demo>/flicker-report.md`. En la regresión, `--flicker` añade la columna `Flicker` (descriptiva).

## Reglas obligatorias de tests y verificación

Estas reglas aplican a cualquier API o cambio del engine. `AGENTS.md` las enruta aquí.

- **Toda API reutilizable del engine debe tener tests que la respalden.** No se promueve código a `engine/` sin una forma de verificar su corrección.
- Forma preferida: ejercitar la API dentro de una **demo** (aunque sea paramétrica o por fases, recorriendo variantes con `g_eng_run_status.detail` y aserciones). Las demos ya corren en el pipeline `build -> run -> analyze`, así que ofrecen evidencia viva de hardware.
- Si la API no tiene demo que la ejercite, **crear una** o, como mínimo, un **test unitario** en `tests/` que la respalde y corra en la regresión.
- Las APIs de matemáticas/algoritmos puros (sin hardware) deben tener además **test unitario host** (`tests/host/`, compilado con el `g++` del entorno del toolchain del proyecto) para validarlas rápido y de forma determinista, sin depender de MSVC ni de WSL.
- **Regla de cierre:** una API sin test (demo o unitario) no se considera terminada.

### Convención de tests host

- Los tests host viven en `tests/host/NNN_<nombre>/` y su prefijo `NNN` es **único y no reutilizable**: un test nuevo toma el **siguiente número libre** (máximo + 1) y, ante una colisión, se renumera el **más nuevo** (actualizando título, rutas internas, referencias y catálogo).
- El catálogo canónico (ID → directorio → qué cubre) y el detalle de compilación están en [../../tests/host/README.md](../../tests/host/README.md).

### Verificación por demo

- Una API del engine (o un cambio en él) solo se considera **verificada** si lo ejercita una **demo exitosa** (`build -> run -> analyze` OK; y, cuando toque render, el gate visual/estructural de [DEMO_VISUAL_DEBUG.md](../guides/methodology/DEMO_VISUAL_DEBUG.md)).
- Todo lo que **no** esté cubierto por una demo exitosa se marca explícitamente como **NO VERIFICADA** en su comentario de cabecera, indicando el motivo (`sin consumidor`, `demo descartada`, `solo test host`, …).
- Una API marcada NO VERIFICADA puede cambiar o eliminarse sin aviso y **no** se documenta en la referencia como si estuviera validada.
- Al descartar o romper la demo que cubría una API, hay que degradar su marca a **NO VERIFICADA** en la misma pasada (y al revés: al validarla con una demo, quitarla).

## Enlaces relacionados

- Operativa completa: [../build/BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md).
- Reglas de demos y de validación visual/render: [../guides/methodology/DEMO_VISUAL_DEBUG.md](../guides/methodology/DEMO_VISUAL_DEBUG.md).
- Invariantes de hardware que validan los tests: [../reference/amiga/hardware/amiga-hardware-invariants-microtests.md](../reference/amiga/hardware/amiga-hardware-invariants-microtests.md).
