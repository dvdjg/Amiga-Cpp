# HOST-257: streaming de chunks (`eng/os/stream.hpp`)

Test host de `ChunkStream<NumBuffers>`: la política de **doble/triple buffer** de un flujo
alimentado por E/S asíncrona (Paula consume mientras el disco llena el siguiente).

## Qué comprueba

1. Al inicio pide **todos** los buffers (`request_mask`); sin datos no hay buffer para reproducir.
2. Al marcar un chunk `on_chunk_ready`, ese buffer deja de pedirse; `advance` consume el actual y
   pasa al siguiente.
3. Si el siguiente **no** estaba listo → **underrun** (y `clear_underrun` lo limpia).
4. Con `eof` no se piden más chunks; `finished` cuando no quedan buffers llenos.

## Salida de referencia

```
OK: streaming de chunks (doble buffer, underrun, eof) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/257_os_stream
```
