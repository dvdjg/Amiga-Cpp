# HOST-392 — leases DMA retenidas por los planes

Valida la retención de assets Chip mediante `FramePlan::retain_dma_asset` y `copper::Plan::retain_dma_asset`.

## Qué cubre

- Un `FramePlan` mantiene vivo el owner Chip al cruzar varios frames y lo suelta en `release_dma_assets()`.
- Un `copper::Plan` mantiene vivo el owner mientras la lista publicada pueda volver a leerlo y lo suelta en teardown explícito.
- La caché rechaza `shutdown()` mientras cualquiera de los planes conserve una lease y lo permite tras liberar ambas.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/392_dma_plan_asset_leases
```
