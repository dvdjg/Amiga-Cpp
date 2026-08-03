# Build y toolchain

Compilación, artefactos y formatos de disco del proyecto. Aquí está la operativa del flujo
nuevo (PowerShell/Node) y el conocimiento heredado del toolchain Amiga.

> **Procedencia:** [build.md](build.md), [amiga-binary-and-disk-formats.md](amiga-binary-and-disk-formats.md)
> y [amiga-dev-harness-loader.md](amiga-dev-harness-loader.md) proceden del repo hermano
> `Cursor-Amiga-C` y describen el flujo de aquel proyecto (scripts `build.sh`, Makefile con
> `BATTERY_CASE`, etc.). Se conservan como referencia del toolchain.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [BUILD_AND_RUN.md](BUILD_AND_RUN.md) | **Guía operativa del flujo actual** del engine (este repo): build-demo, run-demo, analyze-demo y test-regression. |
| [build.md](build.md) | Build CLI del engine C (opciones `--debug/--size/--speed`, `--machine`, relación con el Makefile). |
| [amiga-binary-and-disk-formats.md](amiga-binary-and-disk-formats.md) | Qué produce el build: `.o`, `.elf`, `.exe` (Hunk), `.map`, ADF/HDF/RDB. |
| [amiga-dev-harness-loader.md](amiga-dev-harness-loader.md) | Estrategias de cargador de desarrollo (OS-loader, carga directa, harness/metal). |

## Proyecto C legado

El proyecto C legado tiene su propio Makefile en `legacy/Makefile` (targets `all`, `debug`,
`clean`, salida en `legacy/out/a.exe`). No usar para el engine: `build-demo.ps1` es el
camino correcto. Los detalles de ese Makefile y sus arreglos están comentados en el propio
archivo.

## Enlaces relacionados

- Emulación y despliegue en WinUAE: [../emulation/](../emulation/README.md).
- Fuente autoritativa de formatos: [../reference/](../reference/README.md).
