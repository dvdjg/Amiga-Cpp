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
| Build de demos | `docs/build/BUILD_AND_RUN.md` | `tools/build/build-demo.sh`, `tools/test-regression.sh` |
| Ejecución (runner/emulador) | `docs/build/BUILD_AND_RUN.md`, `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md` | `tools/run/run-demo.sh`/`.ts` |
| Análisis de demos | `docs/testing/PIXEL_FRAME_ASSERTIONS.md`, `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` | `tools/analyze/*` |
| Depuración (WinUAE-DBG/DAP) | `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`, `tools/dap-test/README.md` | `tools/debug/*`, `tools/dap-test/*` |
| Profiling | `tools/profile/README.md` | `tools/profile/*` |
| Verificación visual | `tools/vision-review/README.md`, `docs/testing/VISION_REVIEW_ROADMAP.md` | `tools/vision-review/*` |
| Pipeline de tiles/sprites | `tools/amiga-tiles/README.md`, `docs/demos/tile-pipeline/` | `tools/amiga-tiles/*`, `tools/ehb/*`, `tools/demo202/*` |
| Assets (UAF-R) | `docs/tools/UAF_PACK.md` | `tools/assets/uaf-pack.ts`, `tools/audio/prep-sample.ts` |
| FrameScope | `docs/testing/FRAMESCOPE_ROADMAP.md` | `tools/framescope/*` |
| Entrada (mouse) | `tools/input/*`, `docs/emulation/MOUSE_AUTOMATION.md` | `tools/input/mouse-path.*` |
| Programas independientes para PC | `host-tools/README.md`, `playground/README.md` | `host-tools/`, `playground/` |

## Dónde poner documentación nueva de tools
- Si es específica de una herramienta: `README.md` junto al código.
- Si es procedimiento/flujo de varias tools: `docs/tools/` (crea subcarpeta por
  tema) o el índice correspondiente (`docs/build/`, `docs/testing/`, etc.).