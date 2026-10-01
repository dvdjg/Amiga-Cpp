# L1-001 — App display startup y teardown

Test de integración on-target para la propiedad de display de `App`. Arranca un display pequeño
mediante `App::start()`, corre ocho frames y sale del scope. Después verifica por el canal lateral
que `App::~App()` detuvo los canales DMA de display y devolvió al pool Chip todas las reservas de
la escena.

```bash
bash tests/amiga/l1_backend/001_app_display_teardown/verify.sh
bash tools/analyze/analyze-demo.sh tests/amiga/l1_backend/001_app_display_teardown
```

El estado final `READY/detail=7` significa que la app arrancó, el readback de `DMACONR` no muestra
ningún canal DMA habilitado y la capacidad libre del banco Chip coincide con la inicial. WinUAE de
este proyecto modela el A500/OCS; no es evidencia de ejecución en A1200/AGA físico.
