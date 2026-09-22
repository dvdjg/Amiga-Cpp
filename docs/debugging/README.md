# Documentación de depuración (sistema WinUAE-DBG + investigaciones)

Esta carpeta reúne la documentación de depuración, en dos subcarpetas:

- **`system/`** — el **sistema de depuración** WinUAE-DBG (gdbserver, canal lateral, relocalización, entorno): cómo está montado y cómo operarlo. Procede del repo hermano `Cursor-Amiga-C` y es bitácora. Índice: [`system/README.md`](system/README.md).
- **`investigaciones/`** — **hallazgos** concretos: bloqueos abiertos, post-mortems/lecciones, consultas a IA externa y laboratorios de escenarios. Índice: [`investigaciones/README.md`](investigaciones/README.md).

> Todos los documentos están indexados en el README de su subcarpeta (lo verifica `tools/check/doc-index.mjs`). El estado vigente del engine está en `docs/engine/`; las rarezas del emulador, en `docs/reference/emulators/`.

## Documentación Relacionada

- Hallazgos del emulador (fuente WinUAE, `fichero:línea`): [`docs/reference/emulators/`](../reference/emulators/README.md).
- Mapa «voy a hacer X → documentación»: [`DOC-MAP-PRINCIPAL.md`](../ai-dev-environment/DOC-MAP-PRINCIPAL.md).
