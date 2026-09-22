# Investigación sobre emuladores

Resultados de leer el **código fuente de los emuladores** que usamos (no solo la documentación oficial) para resolver discrepancias de comportamiento. Cada emulador puede implementar los chips de forma distinta; aquí se documenta lo **observado**, con referencias al **fuente** (fichero y línea).

> Para qué sirve: cuando algo «no funciona» en emulador y la doc del hardware no lo explica, la implementación del emulador es la referencia de facto (y a veces revela una errata de la doc, como el bit `ATTACH` de `SPRxCTL`).

Cada emulador tiene su **índice de fichas** (una ficha por **tema**):

| Emulador | Ruta local | Índice de fichas |
|---|---|---|
| WinUAE-DBG | `../WinUAE-DBG/` (fuente) | [`winuae/README.md`](winuae/README.md) |

## Cómo añadir una ficha

- Una ficha por **tema** y **emulador**: `<emulador>/<tema>.md`.
- Citar **fichero y línea** del fuente del emulador.
- Separar lo **observado** (código) de lo **inferido** (hipótesis).
- Si el hallazgo afecta al engine, enlazar la demo/test que lo valida.
- Indexarla en el README del emulador (lo verifica `tools/check/doc-index.mjs`).
