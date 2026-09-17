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
| Ejecución (runner/emulador) | `docs/build/BUILD_AND_RUN.md`, `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md` | `tools/run/run-demo.sh`/`.ts` |
| Análisis de demos | `docs/testing/PIXEL_FRAME_ASSERTIONS.md`, `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` | `tools/analyze/*` |
| Depuración (WinUAE-DBG/DAP) | `docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`, `tools/dap-test/README.md` | `tools/debug/*`, `tools/dap-test/*` |
| Profiling | `tools/profile/README.md` | `tools/profile/*`, `tools/debug/measure-fps.mjs`, `tools/debug/record-fps.mjs` |
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
| `links.mjs` | Que los enlaces relativos de la documentación canónica resuelvan a ficheros/directorios existentes. Admite `[raices...]`, `--quiet` y `--json`. |
| `type-tagging.mjs` | Que no aparezca un `MemoryBlock` crudo convertido a tipo de dominio (INTERNAL_TYPE_SYSTEM.md §1). |
| `scalar-support.mjs` | Que la tabla función × escalar de `SCALAR_LIBRARY.md` esté sincronizada con su fuente única. |
| `math-diagnostics.sh` | Diagnósticos del vocabulario matemático fixed-point. |

Uso típico de `links.mjs`:

```bash
node tools/check/links.mjs                       # documentación canónica (por defecto)
node tools/check/links.mjs docs/engine/c-engine  # un árbol concreto
node tools/check/links.mjs --json                # resultado legible por máquina
```

Por defecto revisa la documentación mantenida; quedan fuera los árboles históricos/importados (`docs/engine/c-engine`, `docs/legacy`) por conservar enlaces al repo de origen. Pasa la raíz explícita para revisarlos.

## Dónde poner documentación nueva de tools
- Si es específica de una herramienta: `README.md` junto al código.
- Si es procedimiento/flujo de varias tools: `docs/tools/` (crea subcarpeta por
  tema) o el índice correspondiente (`docs/build/`, `docs/testing/`, etc.).