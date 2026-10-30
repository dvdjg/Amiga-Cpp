# HOST-504: modulo de BOB de un plano

Valida `bob_draw_plane_opaque` sobre un destino interleaved de 5 planos: un BOB de 12 px con desplazamiento fino procesa 2 palabras por fila y debe avanzar **200 bytes** por fila (`40 * 5`), por lo que `BLTDMOD` es **196** (`200 - 4`), no 36. También comprueba el minterm `D=A`, el stride de plano y la caja beam.

```bash
bash tools/run-host-tests.sh tests/host/graphics/504_bob_plane_modulo
```
