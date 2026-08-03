# Validación y testing

Pirámide de validación **determinista** del engine: contrato de píxeles, secuencias de
frames, FrameScope (análisis temporal sin IA visual) y Vision Review (inspección con VLM
OpenAI-compatible, opcional). La regresión completa las encadena por demo.

> La batería de pruebas y la taxonomía de tests del **engine C** (histórico) están en
> [../c-engine/](../c-engine/README.md), no en esta carpeta.

## Documentos

| Documento | Contenido |
|-----------|-----------|
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

## Enlaces relacionados

- Operativa completa: [../build/BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md).
- Invariantes de hardware que validan los tests: [../hardware/amiga-hardware-invariants-microtests.md](../hardware/amiga-hardware-invariants-microtests.md).
