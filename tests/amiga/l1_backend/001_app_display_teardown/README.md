# L1-001 — App display startup y teardown

Test de integración on-target para la materialización de `World` y la propiedad de display de `App`.
Arranca un display pequeño mediante `App::start()`, añade un background Fill con cámara desplazada,
corre ocho frames y sale del scope. Después verifica por el canal lateral que la capa se materializó,
`App::shutdown()` detuvo los canales DMA de display y la escena devolvió al pool Chip sus reservas.

```bash
bash tests/amiga/l1_backend/001_app_display_teardown/verify.sh
bash tools/analyze/analyze-demo.sh tests/amiga/l1_backend/001_app_display_teardown
```

El estado final `READY/detail=31` confirma: la capa Fill con cámara se materializó, la app arrancó, el readback de
`DMACONR` no muestra canales DMA habilitados y el banco Chip recuperó su capacidad inicial. WinUAE de
este proyecto modela A500/OCS; no es evidencia de ejecución en A1200/AGA físico.
