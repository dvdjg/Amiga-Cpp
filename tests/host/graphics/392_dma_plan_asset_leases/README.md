# HOST-392: leases de assets DMA en planes

Comprueba que `FramePlan` retiene una lease Chip entre `clear()` de frames y libera solo en teardown explícito; `copper::Plan` retiene el asset mientras rota/publica listas y lo suelta en `release()`.

```bash
bash tools/run-host-tests.sh tests/host/graphics/392_dma_plan_asset_leases
```
